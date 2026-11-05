#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "speech.h"
#include "typing_stats.h"
#include "dictionary_hooks.h"
#include "glyph_tokens.h"
#include "log.h"

#define MAX_SPOKEN 128
#define QUEUE_LENGTH 16
#define SPEAK_SLOT 20
#define SET_RATE_SLOT 28
#define SET_VOLUME_SLOT 30
#define SPF_ASYNC 1
#define SPF_PURGEBEFORESPEAK 2
#define SPF_IS_NOT_XML 0x10
#define HALF_WIDTH_CELL GLYPH_HALF_WIDTH_CELL

typedef HRESULT(__stdcall *speak_fn)(void *self, const wchar_t *text, DWORD flags, ULONG *stream);
typedef HRESULT(__stdcall *set_rate_fn)(void *self, long rate);
typedef HRESULT(__stdcall *set_volume_fn)(void *self, unsigned short volume);

static const GUID CLSID_SP_VOICE = {0x96749377, 0x3391, 0x11d2, {0x9e, 0xe3, 0x00, 0xc0, 0x4f, 0x79, 0x73, 0x96}};
static const GUID IID_SP_VOICE = {0x6c44df74, 0x72b9, 0x4992, {0xa1, 0xec, 0xef, 0x99, 0x6e, 0x04, 0x22, 0xd4}};

static wchar_t queue[QUEUE_LENGTH][MAX_SPOKEN];
static volatile LONG queue_head, queue_tail;
static HANDLE queue_event;
static int speech_rate, speech_volume = 100;

static void *com_slot(void *object, int slot) {
    return (*(void ***)object)[slot];
}

static DWORD WINAPI speech_thread(LPVOID unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    void *voice = NULL;
    HRESULT result = CoCreateInstance(&CLSID_SP_VOICE, NULL, CLSCTX_ALL, &IID_SP_VOICE, &voice);
    log_line("speech: Windows text-to-speech %s (%08lX)", SUCCEEDED(result) ? "ready" : "not available", (unsigned long)result);
    if (FAILED(result) || !voice) return 0;
    ((set_rate_fn)com_slot(voice, SET_RATE_SLOT))(voice, speech_rate);
    ((set_volume_fn)com_slot(voice, SET_VOLUME_SLOT))(voice, (unsigned short)speech_volume);
    for (;;) {
        WaitForSingleObject(queue_event, INFINITE);
        while (queue_tail != queue_head) {
            wchar_t text[MAX_SPOKEN];
            lstrcpynW(text, queue[queue_tail % QUEUE_LENGTH], MAX_SPOKEN);
            InterlockedIncrement(&queue_tail);
            ((speak_fn)com_slot(voice, SPEAK_SLOT))(voice, text, SPF_IS_NOT_XML, NULL);
        }
    }
}

static void decode_display(const unsigned char *display, wchar_t *out, int size) {
    int used = 0;
    for (const unsigned char *c = display; *c && used < size - 2;) {
        int lead = (*c >= 0x81 && *c <= 0x9f) || (*c >= 0xe0 && *c <= 0xfc);
        if (!lead || !c[1]) {
            out[used++] = *c++;
            continue;
        }
        unsigned code = (unsigned)c[0] << 8 | c[1];
        if (code == HALF_WIDTH_CELL) {
            int cell = 0;
            const wchar_t *text = dictionary_half_width_cell(c, &cell);
            if (text) {
                lstrcpynW(out, text, size);
                return;
            }
        }
        if (glyph_token_is_token(code)) {
            uint32_t codepoint = glyph_token_codepoint(code);
            if (codepoint && codepoint < 0x10000) out[used++] = (wchar_t)codepoint;
        } else {
            wchar_t wide[2];
            if (MultiByteToWideChar(932, 0, (const char *)c, 2, wide, 2) == 1) {
                if (wide[0] >= 0xff01 && wide[0] <= 0xff5e) wide[0] = (wchar_t)(wide[0] - 0xfee0);
                if (wide[0] == 0x3000) wide[0] = L' ';
                out[used++] = wide[0];
            }
        }
        c += 2;
    }
    out[used] = 0;
}

static void speak_finished_word(int player, const unsigned char *word, const char *typed, int typos) {
    (void)player;
    (void)typos;
    if (queue_head - queue_tail >= QUEUE_LENGTH) return;
    wchar_t *slot = queue[queue_head % QUEUE_LENGTH];
    const unsigned char *display = word ? *(const unsigned char *const *)word : NULL;
    slot[0] = 0;
    if (display && !IsBadStringPtrA((const char *)display, 256)) decode_display(display, slot, MAX_SPOKEN);
    if (!slot[0]) MultiByteToWideChar(CP_ACP, 0, typed, -1, slot, MAX_SPOKEN);
    if (!slot[0]) return;
    InterlockedIncrement(&queue_head);
    SetEvent(queue_event);
}

void install_speech(const char *ini_path) {
    if (!GetPrivateProfileIntA("fun", "speak_words", 0, ini_path)) return;
    speech_rate = GetPrivateProfileIntA("fun", "speech_rate", 0, ini_path);
    speech_volume = GetPrivateProfileIntA("fun", "speech_volume", 100, ini_path);
    if (speech_rate < -10) speech_rate = -10;
    if (speech_rate > 10) speech_rate = 10;
    queue_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE thread = CreateThread(NULL, 0, speech_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    typing_stats_on_word(speak_finished_word);
    log_line("speech: finished words are read aloud (rate %d, volume %d)", speech_rate, speech_volume);
}
