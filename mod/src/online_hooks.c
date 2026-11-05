#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "online_hooks.h"
#include "online_fingerprint.h"
#include "determinism.h"
#include "log.h"
#include "MinHook.h"

#define GAME_FRAME (*(volatile uint32_t *)0x00a18fac)
#define GAME_MUTEX_NAME "HicGameLibMutex"
#define RING 256
#define REPORTS_PER_PACKET 16
#define MAX_INPUT_DELAY (REPORTS_PER_PACKET - 1)
#define MIN_AUTO_DELAY 2
#define FRAME_MS 16.667
#define HASH_PARTS DETERMINISM_HASH_PARTS
#define NO_HASH 0xffffffffu
#define PACKET_MAGIC 0x4c444f54
#define PROTOCOL_VERSION 2
#define DEFAULT_PORT 7845
#define SEND_INTERVAL_MS 20
#define SAVE_REQUEST_INTERVAL_MS 60
#define PING_PHASE_MS 700
#define SUMMARY_FRAMES 1800
#define TITLE_FRAMES 120
#define SAVE_CHUNK_SIZE 1024
#define SAVE_CHUNKS ((SAVE_SIZE + SAVE_CHUNK_SIZE - 1) / SAVE_CHUNK_SIZE)
#define SAVE_CHUNKS_PER_REQUEST 32
#define SAVE_CACHE_MS 2000
#define MAX_PACKET 1400
#define TEST_QUEUE 512
#define KEY_DOWN_BIT 0x8000

enum { ROLE_OFF, ROLE_HOST, ROLE_JOIN };
enum { PACKET_INPUT = 1, PACKET_SAVE_REQUEST, PACKET_SAVE_CHUNK };

typedef struct {
    uint32_t magic;
    uint8_t version, kind, role, reserved;
    uint32_t room;
    uint32_t boot_id;
} packet_header;

typedef struct {
    packet_header header;
    uint32_t fingerprint[FINGERPRINT_PARTS];
    uint32_t sent_ms, echo_ms, echo_age_ms;
    uint32_t first_frame, report_count;
    unsigned char reports[REPORTS_PER_PACKET][REPORT_SIZE];
    uint32_t hash_frame;
    uint32_t hash[HASH_PARTS];
} input_packet;

typedef struct {
    packet_header header;
    uint32_t first_chunk, chunk_count;
} save_request_packet;

typedef struct {
    packet_header header;
    uint32_t file_size, file_hash, chunk, length;
    unsigned char data[SAVE_CHUNK_SIZE];
} save_chunk_packet;

typedef struct {
    uint32_t frame;
    int have_local, have_remote;
    uint32_t local[HASH_PARTS], remote[HASH_PARTS];
} frame_hash;

typedef struct {
    uint32_t release_ms;
    int size;
    struct sockaddr_in from;
    unsigned char data[MAX_PACKET];
} delayed_packet;

typedef void(__cdecl *set_active_mode_fn)(int active);
typedef HANDLE(WINAPI *create_mutex_fn)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);

static set_active_mode_fn original_set_active_mode;
static create_mutex_fn original_create_mutex;

static int role;
static int connected, started, gave_up;
static SOCKET peer_socket = INVALID_SOCKET;
static int network_ready;
static struct sockaddr_in send_address;
static struct sockaddr_in accepted_address;
static int accept_any_address;
static int send_address_known;
static int auto_delay = 1;
static int input_delay = MIN_AUTO_DELAY;
static int connect_timeout_ms = 120000;
static int peer_timeout_ms = 20000;
static int port = DEFAULT_PORT;
static char peer_host[128] = "127.0.0.1";
static char relay_host[128] = "";
static char room_name[64] = "";
static uint32_t room_hash;
static uint32_t boot_id;
static uint32_t peer_boot_id;
static uint32_t local_fingerprint[FINGERPRINT_PARTS];
static int fingerprint_checked;

static uint32_t frame;
static unsigned char local_reports[RING][REPORT_SIZE];
static unsigned char remote_reports[RING][REPORT_SIZE];
static uint32_t remote_report_tag[RING];
static uint32_t remote_frames_known;
static frame_hash hashes[RING];
static uint32_t current_hash[HASH_PARTS];
static int current_hash_valid;
static uint32_t frames_checked, mismatches;
static uint32_t waited_ms, last_send_ms, last_receive_ms;
static uint32_t peer_sent_ms, peer_sent_received_ms;
static double smoothed_rtt;
static int rtt_samples;
static uint32_t best_rtt = 0xffffffffu;

static unsigned char save_data[SAVE_SIZE];
static unsigned char save_chunk_received[SAVE_CHUNKS];
static int save_chunks_received, save_file_size = -1;
static uint32_t save_file_hash;
static int using_host_save;
static unsigned char host_save_cache[SAVE_SIZE];
static int host_save_cache_size;
static uint32_t host_save_cache_hash, host_save_cache_ms;

static int test_lag_ms, test_jitter_ms, test_loss_percent;
static delayed_packet test_queue[TEST_QUEUE];
static int test_queue_count;
static uint32_t test_random = 0x9e3779b9u;

static FILE *hash_log;
static HWND game_window;
static char original_title[256];

static const char *role_name(int which) {
    return which == ROLE_HOST ? "host" : which == ROLE_JOIN ? "join" : "off";
}

static const char *peer_name(void) {
    return role == ROLE_HOST ? "the joining player" : "the host";
}

static int parse_role(const char *text) {
    if (!lstrcmpiA(text, "host")) return ROLE_HOST;
    if (!lstrcmpiA(text, "join")) return ROLE_JOIN;
    return ROLE_OFF;
}

int online_role_from_settings(const char *ini_path) {
    char text[16] = "";
    if (GetEnvironmentVariableA("TODMOD_ONLINE", text, sizeof text) && text[0]) return parse_role(text);
    GetPrivateProfileStringA("online", "mode", "off", text, sizeof text, ini_path);
    return parse_role(text);
}

const char *online_log_name(int which) {
    return which == ROLE_JOIN && GetEnvironmentVariableA("TODMOD_ONLINE", NULL, 0) ? "todmod_join.log" : "todmod.log";
}

int online_lockstep_active(void) {
    return role != ROLE_OFF && !gave_up;
}

static uint32_t now_ms(void) {
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint32_t)(counter.QuadPart * 1000 / frequency.QuadPart);
}

static uint32_t hash_bytes(const void *data, size_t size) {
    const unsigned char *bytes = data;
    uint32_t hash = 0x811c9dc5u;
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * 0x01000193u;
    return hash;
}

static uint32_t next_test_random(void) {
    test_random ^= test_random << 13;
    test_random ^= test_random >> 17;
    test_random ^= test_random << 5;
    return test_random;
}

static BOOL CALLBACK find_game_window(HWND window, LPARAM found) {
    if (IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
        *(HWND *)found = window;
        return FALSE;
    }
    return TRUE;
}

static void show_status(const char *format, ...) {
    if (!game_window && determinism_game_thread()) {
        EnumThreadWindows(determinism_game_thread(), find_game_window, (LPARAM)&game_window);
        if (game_window) GetWindowTextA(game_window, original_title, sizeof original_title);
    }
    if (!game_window) return;
    char status[256], title[512];
    va_list args;
    va_start(args, format);
    _vsnprintf(status, sizeof status, format, args);
    va_end(args);
    status[sizeof status - 1] = '\0';
    _snprintf(title, sizeof title, "%s - online %s: %s", original_title, role_name(role), status);
    title[sizeof title - 1] = '\0';
    SetWindowTextA(game_window, title);
}

static void stop_online(const char *reason) {
    if (gave_up) return;
    gave_up = 1;
    log_line("online: %s at frame %u; the game continues offline", reason, frame);
    show_status("offline (%s)", reason);
}

static void __cdecl set_active_mode_hook(int active) {
    if (!active) log_line("online: the game keeps running while its window is in the background");
    original_set_active_mode(1);
}

static HANDLE WINAPI create_mutex_hook(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCSTR name) {
    char unique[MAX_PATH];
    if (name && strstr(name, GAME_MUTEX_NAME)) {
        _snprintf(unique, sizeof unique, "%s_todmod_%s", name, role_name(role));
        unique[sizeof unique - 1] = '\0';
        log_line("online: single-instance mutex renamed to %s", unique);
        return original_create_mutex(attributes, owner, unique);
    }
    return original_create_mutex(attributes, owner, name);
}

static void compare_hash(frame_hash *entry) {
    if (!entry->have_local || !entry->have_remote) return;
    frames_checked++;
    if (memcmp(entry->local, entry->remote, sizeof entry->local)) {
        mismatches++;
        if (mismatches <= 20)
            log_line("online: DESYNC at frame %u: local %u/%u/%08X/%08X, peer %u/%u/%08X/%08X", entry->frame,
                     entry->local[0], entry->local[1], entry->local[2], entry->local[3], entry->remote[0],
                     entry->remote[1], entry->remote[2], entry->remote[3]);
        if (mismatches == 1) show_status("DESYNC at frame %u - the two games differ (see todmod.log)", entry->frame);
    }
    entry->have_local = entry->have_remote = 0;
}

static frame_hash *hash_slot(uint32_t which) {
    frame_hash *entry = &hashes[which % RING];
    if (entry->frame != which) {
        memset(entry, 0, sizeof *entry);
        entry->frame = which;
    }
    return entry;
}

static void record_local_hash(void) {
    frame_hash *entry = hash_slot(frame);
    determinism_hash(entry->local);
    memcpy(current_hash, entry->local, sizeof current_hash);
    current_hash_valid = 1;
    entry->have_local = 1;
    if (hash_log)
        fprintf(hash_log, "%u\t%u\t%u\t%08X\t%08X\n", frame, entry->local[0], entry->local[1], entry->local[2],
                entry->local[3]);
    compare_hash(entry);
}

static int resolve_address(const char *text, int default_port, struct sockaddr_in *address) {
    char host[128];
    int which_port = default_port;
    lstrcpynA(host, text, sizeof host);
    char *colon = strrchr(host, ':');
    if (colon) {
        *colon = '\0';
        which_port = atoi(colon + 1);
    }
    memset(address, 0, sizeof *address);
    address->sin_family = AF_INET;
    address->sin_port = htons((u_short)which_port);
    address->sin_addr.s_addr = inet_addr(host);
    if (address->sin_addr.s_addr != INADDR_NONE) return 1;
    struct hostent *entry = gethostbyname(host);
    if (!entry) return 0;
    memcpy(&address->sin_addr, entry->h_addr_list[0], sizeof address->sin_addr);
    return 1;
}

static int open_network(void) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 0;
    peer_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (peer_socket == INVALID_SOCKET) return 0;
    int use_relay = relay_host[0] != '\0';
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(role == ROLE_HOST && !use_relay ? (u_short)port : 0);
    if (bind(peer_socket, (struct sockaddr *)&local, sizeof local)) {
        log_line("online: cannot use UDP port %d (error %d; is another program using it?)", port, WSAGetLastError());
        return 0;
    }
    u_long non_blocking = 1;
    ioctlsocket(peer_socket, FIONBIO, &non_blocking);
    if (use_relay) {
        if (!resolve_address(relay_host, DEFAULT_PORT + 1, &send_address)) {
            log_line("online: cannot find the relay server %s", relay_host);
            return 0;
        }
        send_address_known = 1;
        accepted_address = send_address;
        log_line("online: %s through relay %s:%d, room \"%s\"", role_name(role), inet_ntoa(send_address.sin_addr),
                 ntohs(send_address.sin_port), room_name);
    } else if (role == ROLE_JOIN) {
        if (!resolve_address(peer_host, port, &send_address)) {
            log_line("online: cannot find the host %s", peer_host);
            return 0;
        }
        send_address_known = 1;
        accepted_address = send_address;
        log_line("online: joining %s:%d", inet_ntoa(send_address.sin_addr), ntohs(send_address.sin_port));
    } else {
        accept_any_address = 1;
        log_line("online: hosting on UDP port %d (forward this port, or use a VPN or a relay, for play over the "
                 "internet)", port);
    }
    return 1;
}

static int ensure_network(void) {
    if (network_ready) return 1;
    if (gave_up) return 0;
    if (!open_network()) {
        stop_online("network setup failed");
        return 0;
    }
    network_ready = 1;
    return 1;
}

static void fill_header(packet_header *header, int kind) {
    header->magic = PACKET_MAGIC;
    header->version = PROTOCOL_VERSION;
    header->kind = (uint8_t)kind;
    header->role = (uint8_t)role;
    header->reserved = 0;
    header->room = room_hash;
    header->boot_id = boot_id;
}

static void send_packet(const void *packet, int size) {
    if (!send_address_known) return;
    sendto(peer_socket, (const char *)packet, size, 0, (const struct sockaddr *)&send_address, sizeof send_address);
}

static void send_input_state(void) {
    input_packet packet;
    memset(&packet, 0, sizeof packet);
    fill_header(&packet.header, PACKET_INPUT);
    memcpy(packet.fingerprint, local_fingerprint, sizeof packet.fingerprint);
    uint32_t now = now_ms();
    packet.sent_ms = now;
    packet.echo_ms = peer_sent_ms;
    packet.echo_age_ms = peer_sent_ms ? now - peer_sent_received_ms : 0;
    if (started) {
        uint32_t last = frame + input_delay;
        uint32_t first = last + 1 >= REPORTS_PER_PACKET ? last + 1 - REPORTS_PER_PACKET : 0;
        packet.first_frame = first;
        packet.report_count = last - first + 1;
        for (uint32_t i = 0; i < packet.report_count; i++)
            memcpy(packet.reports[i], local_reports[(first + i) % RING], REPORT_SIZE);
    }
    packet.hash_frame = current_hash_valid ? frame : NO_HASH;
    memcpy(packet.hash, current_hash, sizeof packet.hash);
    send_packet(&packet, sizeof packet);
    last_send_ms = now;
}

static void load_host_save_cache(void) {
    uint32_t now = now_ms();
    if (host_save_cache_ms && now - host_save_cache_ms < SAVE_CACHE_MS) return;
    host_save_cache_size = determinism_read_save_file(host_save_cache);
    host_save_cache_hash = hash_bytes(host_save_cache, host_save_cache_size);
    host_save_cache_ms = now;
    log_line("online: sending this game's save (%d bytes) to the joining player", host_save_cache_size);
}

static void answer_save_request(const save_request_packet *request) {
    load_host_save_cache();
    save_chunk_packet chunk;
    memset(&chunk, 0, sizeof chunk);
    fill_header(&chunk.header, PACKET_SAVE_CHUNK);
    chunk.file_size = (uint32_t)host_save_cache_size;
    chunk.file_hash = host_save_cache_hash;
    uint32_t count = request->chunk_count > SAVE_CHUNKS_PER_REQUEST ? SAVE_CHUNKS_PER_REQUEST : request->chunk_count;
    if (!host_save_cache_size) {
        send_packet(&chunk, sizeof chunk);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = request->first_chunk + i;
        uint32_t offset = index * SAVE_CHUNK_SIZE;
        if (offset >= (uint32_t)host_save_cache_size) break;
        chunk.chunk = index;
        chunk.length = (uint32_t)host_save_cache_size - offset < SAVE_CHUNK_SIZE ? (uint32_t)host_save_cache_size - offset
                                                                                 : SAVE_CHUNK_SIZE;
        memcpy(chunk.data, host_save_cache + offset, chunk.length);
        send_packet(&chunk, sizeof chunk);
    }
}

static void store_save_chunk(const save_chunk_packet *chunk) {
    if (role != ROLE_JOIN) return;
    if (chunk->file_size > SAVE_SIZE) return;
    save_file_size = (int)chunk->file_size;
    save_file_hash = chunk->file_hash;
    if (!chunk->file_size || chunk->chunk >= SAVE_CHUNKS || chunk->length > SAVE_CHUNK_SIZE) return;
    if (chunk->chunk * SAVE_CHUNK_SIZE + chunk->length > chunk->file_size) return;
    if (save_chunk_received[chunk->chunk]) return;
    memcpy(save_data + chunk->chunk * SAVE_CHUNK_SIZE, chunk->data, chunk->length);
    save_chunk_received[chunk->chunk] = 1;
    save_chunks_received++;
}

static void check_fingerprint(const uint32_t *peer_fingerprint) {
    if (fingerprint_checked) return;
    fingerprint_checked = 1;
    int differences = 0;
    for (int i = 0; i < FINGERPRINT_PARTS; i++) {
        if (peer_fingerprint[i] == local_fingerprint[i]) continue;
        differences++;
        log_line("online: settings differ from %s: %s", peer_name(), online_fingerprint_part_name(i));
    }
    if (differences) {
        log_line("online: both games must use the same todmod version, word packs, typing, rules, Tod.ini game "
                 "settings and model packs, or they drift apart; copy the host's todmod.ini, dict\\ packs and "
                 "Tod.ini choices to the other PC");
        stop_online("settings differ from the other player's (see todmod.log)");
    } else {
        log_line("online: settings match %s", peer_name());
    }
}

static void record_rtt(const input_packet *packet, uint32_t now) {
    if (!packet->echo_ms) return;
    uint32_t rtt = now - packet->echo_ms - packet->echo_age_ms;
    if (rtt > 10000) return;
    if (rtt < best_rtt) best_rtt = rtt;
    smoothed_rtt = rtt_samples ? smoothed_rtt * 0.9 + rtt * 0.1 : rtt;
    rtt_samples++;
}

static void store_input_packet(const input_packet *packet) {
    uint32_t now = now_ms();
    peer_sent_ms = packet->sent_ms;
    peer_sent_received_ms = now;
    record_rtt(packet, now);
    check_fingerprint(packet->fingerprint);
    if (gave_up || packet->report_count > REPORTS_PER_PACKET) return;
    for (uint32_t i = 0; i < packet->report_count; i++) {
        uint32_t which = packet->first_frame + i;
        if (which < remote_frames_known || which >= remote_frames_known + RING / 2) continue;
        memcpy(remote_reports[which % RING], packet->reports[i], REPORT_SIZE);
        remote_report_tag[which % RING] = which + 1;
    }
    while (remote_report_tag[remote_frames_known % RING] == remote_frames_known + 1) remote_frames_known++;
    if (packet->hash_frame != NO_HASH && packet->hash_frame + RING / 2 > frame) {
        frame_hash *entry = hash_slot(packet->hash_frame);
        memcpy(entry->remote, packet->hash, sizeof entry->remote);
        entry->have_remote = 1;
        compare_hash(entry);
    }
}

static int same_address(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void handle_packet(const unsigned char *data, int size, const struct sockaddr_in *from) {
    const packet_header *header = (const packet_header *)data;
    if (size < (int)sizeof *header || header->magic != PACKET_MAGIC || header->room != room_hash) return;
    if (!accept_any_address && !same_address(from, &accepted_address)) return;
    if (header->version != PROTOCOL_VERSION) {
        stop_online("the other player's todmod speaks another online version");
        return;
    }
    if (header->role == role) {
        stop_online(role == ROLE_HOST ? "both players are set to host" : "both players are set to join");
        return;
    }
    if (accept_any_address) {
        accepted_address = *from;
        send_address = *from;
        send_address_known = 1;
        accept_any_address = 0;
    }
    if (peer_boot_id && header->boot_id != peer_boot_id) {
        stop_online("the other player restarted the game");
        return;
    }
    peer_boot_id = header->boot_id;
    last_receive_ms = now_ms();
    if (!connected) {
        connected = 1;
        log_line("online: connected to %s (%s:%d)", peer_name(), inet_ntoa(from->sin_addr), ntohs(from->sin_port));
        show_status("connected");
    }
    if (header->kind == PACKET_INPUT && size == (int)sizeof(input_packet)) store_input_packet((const input_packet *)data);
    else if (header->kind == PACKET_SAVE_REQUEST && size == (int)sizeof(save_request_packet) && role == ROLE_HOST)
        answer_save_request((const save_request_packet *)data);
    else if (header->kind == PACKET_SAVE_CHUNK && size == (int)sizeof(save_chunk_packet))
        store_save_chunk((const save_chunk_packet *)data);
}

static void queue_test_packet(const unsigned char *data, int size, const struct sockaddr_in *from) {
    if (test_loss_percent && (int)(next_test_random() % 100) < test_loss_percent) return;
    if (test_queue_count == TEST_QUEUE) return;
    delayed_packet *entry = &test_queue[test_queue_count++];
    entry->release_ms = now_ms() + test_lag_ms + (test_jitter_ms ? next_test_random() % (test_jitter_ms + 1) : 0);
    entry->size = size;
    entry->from = *from;
    memcpy(entry->data, data, size);
}

static void release_test_packets(void) {
    uint32_t now = now_ms();
    for (int i = 0; i < test_queue_count;) {
        if ((int32_t)(now - test_queue[i].release_ms) >= 0) {
            delayed_packet entry = test_queue[i];
            test_queue[i] = test_queue[--test_queue_count];
            handle_packet(entry.data, entry.size, &entry.from);
        } else {
            i++;
        }
    }
}

static void receive_packets(void) {
    unsigned char data[MAX_PACKET];
    struct sockaddr_in from;
    int from_size = sizeof from;
    int size;
    while ((size = recvfrom(peer_socket, (char *)data, sizeof data, 0, (struct sockaddr *)&from, &from_size)) > 0) {
        from_size = sizeof from;
        if (test_lag_ms || test_jitter_ms || test_loss_percent) queue_test_packet(data, size, &from);
        else handle_packet(data, size, &from);
        if (gave_up) return;
    }
    release_test_packets();
}

static void pump_network(void) {
    if (now_ms() - last_send_ms >= SEND_INTERVAL_MS) send_input_state();
    receive_packets();
}

static void wait_briefly(void) {
    MSG message;
    PeekMessageA(&message, NULL, 0, 0, PM_NOREMOVE);
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(peer_socket, &readable);
    struct timeval timeout = {0, 2000};
    select(0, &readable, NULL, NULL, &timeout);
}

static int escape_pressed(void) {
    return game_window && GetForegroundWindow() == game_window && (GetAsyncKeyState(VK_ESCAPE) & KEY_DOWN_BIT);
}

static int wait_until_connected(void) {
    if (connected) return !gave_up;
    uint32_t started_at = now_ms();
    log_line("online: waiting for %s", peer_name());
    show_status("waiting for %s (Esc = play offline)", peer_name());
    while (!connected && !gave_up) {
        pump_network();
        if (escape_pressed()) stop_online("waiting cancelled with Esc");
        else if (now_ms() - started_at > (uint32_t)connect_timeout_ms) stop_online("nobody connected");
        else wait_briefly();
    }
    return !gave_up;
}

static int delay_for_rtt(double rtt) {
    int delay = (int)(rtt / 2 / FRAME_MS + 0.999) + 1;
    if (delay < MIN_AUTO_DELAY) delay = MIN_AUTO_DELAY;
    return delay > MAX_INPUT_DELAY ? MAX_INPUT_DELAY : delay;
}

static void start_lockstep(void) {
    uint32_t started_at = now_ms();
    while (!gave_up && now_ms() - started_at < PING_PHASE_MS) {
        pump_network();
        wait_briefly();
    }
    if (gave_up) return;
    if (auto_delay) input_delay = rtt_samples ? delay_for_rtt(smoothed_rtt) : MIN_AUTO_DELAY + 1;
    started = 1;
    log_line("online: playing; ping %.0f ms (%d samples), input delay %d frames (%s)", smoothed_rtt, rtt_samples,
             input_delay, auto_delay ? "from the ping" : "from todmod.ini");
    show_status("playing, ping %.0f ms, delay %d", smoothed_rtt, input_delay);
}

static void wait_for_peer_input(void) {
    uint32_t started_at = now_ms();
    int warned = 0;
    send_input_state();
    while (!gave_up) {
        receive_packets();
        if (remote_frames_known > frame) break;
        uint32_t now = now_ms();
        if (now - last_send_ms >= SEND_INTERVAL_MS) send_input_state();
        if (now - last_receive_ms > (uint32_t)peer_timeout_ms) {
            stop_online("the other player stopped answering");
            break;
        }
        if (!warned && now - started_at > 1000) {
            warned = 1;
            show_status("waiting for %s...", peer_name());
        }
        if (warned && escape_pressed()) {
            stop_online("waiting cancelled with Esc");
            break;
        }
        wait_briefly();
    }
    if (warned && !gave_up) show_status("playing, ping %.0f ms, delay %d", smoothed_rtt, input_delay);
    waited_ms += now_ms() - started_at;
}

static void apply_reports(void) {
    const unsigned char *host = role == ROLE_HOST ? local_reports[frame % RING] : remote_reports[frame % RING];
    const unsigned char *join = role == ROLE_HOST ? remote_reports[frame % RING] : local_reports[frame % RING];
    memcpy(determinism_device_report(0), host, REPORT_SIZE);
    memcpy(determinism_device_report(1), join, REPORT_SIZE);
}

static void online_input(void) {
    if (gave_up || !ensure_network() || !wait_until_connected()) return;
    if (!started) start_lockstep();
    if (gave_up) return;
    memcpy(local_reports[(frame + input_delay) % RING], determinism_device_report(0), REPORT_SIZE);
    record_local_hash();
    wait_for_peer_input();
    if (gave_up) return;
    apply_reports();
    frame++;
    if (frame % TITLE_FRAMES == 0 && !mismatches) show_status("playing, ping %.0f ms, delay %d", smoothed_rtt, input_delay);
    if (frame % SUMMARY_FRAMES == 0)
        log_line("online: frame %u (game frame %u), %u frames compared, %u desyncs, ping %.0f ms (best %u), waited "
                 "%u ms for the other player",
                 frame, GAME_FRAME, frames_checked, mismatches, smoothed_rtt, best_rtt, waited_ms);
}

static void request_missing_save_chunks(void) {
    save_request_packet request;
    memset(&request, 0, sizeof request);
    fill_header(&request.header, PACKET_SAVE_REQUEST);
    int first = 0;
    while (first < SAVE_CHUNKS && save_chunk_received[first]) first++;
    request.first_chunk = (uint32_t)first;
    request.chunk_count = SAVE_CHUNKS_PER_REQUEST;
    send_packet(&request, sizeof request);
}

static int save_complete(void) {
    if (save_file_size <= 0) return 0;
    int needed = (save_file_size + SAVE_CHUNK_SIZE - 1) / SAVE_CHUNK_SIZE;
    return save_chunks_received >= needed;
}

static int load_host_save(unsigned char *save_buffer) {
    if (role != ROLE_JOIN || gave_up || !ensure_network() || !wait_until_connected()) return -1;
    log_line("online: loading the host's save instead of this PC's user.bin");
    show_status("receiving the host's save");
    memset(save_chunk_received, 0, sizeof save_chunk_received);
    save_chunks_received = 0;
    save_file_size = -1;
    uint32_t started_at = now_ms(), last_request = 0;
    while (!gave_up && save_file_size != 0 && !save_complete()) {
        uint32_t now = now_ms();
        if (!last_request || now - last_request >= SAVE_REQUEST_INTERVAL_MS) {
            request_missing_save_chunks();
            last_request = now;
        }
        pump_network();
        if (now - started_at > (uint32_t)connect_timeout_ms) stop_online("the host did not send its save");
        else wait_briefly();
    }
    if (gave_up) return -1;
    using_host_save = 1;
    determinism_block_save_writes();
    if (save_file_size == 0) {
        log_line("online: the host has no save yet; starting from a fresh one like the host");
        return 0;
    }
    if (save_file_size != SAVE_SIZE || hash_bytes(save_data, save_file_size) != save_file_hash) {
        stop_online("the host's save arrived damaged");
        return -1;
    }
    memcpy(save_buffer, save_data, SAVE_SIZE);
    log_line("online: host's save received (%d bytes in %u ms)", save_file_size, now_ms() - started_at);
    return 1;
}

static MH_STATUS hook_address(void *address, void *hook, void **original) {
    if (!address) return MH_ERROR_FUNCTION_NOT_FOUND;
    MH_STATUS status = MH_CreateHook(address, hook, original);
    return status == MH_OK ? MH_EnableHook(address) : status;
}

static FILE *open_role_file(const char *prefix) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    char *name = slash ? slash + 1 : path;
    _snprintf(name, MAX_PATH - (name - path), "%s_%s.tsv", prefix, role_name(role));
    path[MAX_PATH - 1] = '\0';
    return fopen(path, "w");
}

static void read_settings(const char *ini_path) {
    char text[32];
    GetPrivateProfileStringA("online", "input_delay", "auto", text, sizeof text, ini_path);
    auto_delay = !lstrcmpiA(text, "auto") || !text[0];
    if (!auto_delay) {
        input_delay = atoi(text);
        if (input_delay < 0) input_delay = 0;
        if (input_delay > MAX_INPUT_DELAY) input_delay = MAX_INPUT_DELAY;
    }
    port = GetPrivateProfileIntA("online", "port", DEFAULT_PORT, ini_path);
    connect_timeout_ms = GetPrivateProfileIntA("online", "connect_timeout", 120, ini_path) * 1000;
    peer_timeout_ms = GetPrivateProfileIntA("online", "peer_timeout", 20, ini_path) * 1000;
    GetPrivateProfileStringA("online", "peer", "127.0.0.1", peer_host, sizeof peer_host, ini_path);
    GetPrivateProfileStringA("online", "relay", "", relay_host, sizeof relay_host, ini_path);
    GetPrivateProfileStringA("online", "room", "", room_name, sizeof room_name, ini_path);
    room_hash = relay_host[0] ? hash_bytes(room_name, strlen(room_name)) | 1 : 0;
    test_lag_ms = GetPrivateProfileIntA("online", "test_lag_ms", 0, ini_path);
    test_jitter_ms = GetPrivateProfileIntA("online", "test_jitter_ms", 0, ini_path);
    test_loss_percent = GetPrivateProfileIntA("online", "test_loss_percent", 0, ini_path);
    if (test_lag_ms || test_jitter_ms || test_loss_percent)
        log_line("online: TEST network conditions: +%d ms (+0..%d ms jitter), %d%% of packets lost", test_lag_ms,
                 test_jitter_ms, test_loss_percent);
    if (GetPrivateProfileIntA("online", "log_hashes", 0, ini_path)) {
        hash_log = open_role_file("todmod_online");
        if (hash_log) fputs("frame\tgame_frame\trand_calls\trand\tstate\n", hash_log);
    }
}

void install_online_hooks(const char *ini_path, int which, const char *version) {
    role = which;
    if (role == ROLE_OFF) return;
    read_settings(ini_path);
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    boot_id = (uint32_t)counter.QuadPart ^ (GetCurrentProcessId() << 16) ^ GetTickCount();
    if (!boot_id) boot_id = 1;
    online_fingerprint(ini_path, version, local_fingerprint);
    log_line("online: settings fingerprint %08X %08X %08X %08X %08X %08X", local_fingerprint[0], local_fingerprint[1],
             local_fingerprint[2], local_fingerprint[3], local_fingerprint[4], local_fingerprint[5]);
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    void *create_mutex = NULL;
    MH_STATUS mutex_status =
        MH_CreateHookApiEx(L"kernel32", "CreateMutexA", create_mutex_hook, (void **)&original_create_mutex, &create_mutex);
    if (mutex_status == MH_OK) mutex_status = MH_EnableHook(create_mutex);
    install_determinism(ini_path, role_name(role));
    determinism_on_input(online_input);
    determinism_set_save_loader(load_host_save);
    log_line("online: role %s, input delay %s; hooks mutex %s, background running %s", role_name(role),
             auto_delay ? "auto" : "fixed", MH_StatusToString(mutex_status),
             MH_StatusToString(hook_address(hglcore ? (void *)GetProcAddress(hglcore, "hlSetActiveMode") : NULL,
                                            set_active_mode_hook, (void **)&original_set_active_mode)));
}
