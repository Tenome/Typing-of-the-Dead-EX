#include <windows.h>
#include "background_mute.h"
#include "log.h"

#define POLL_MILLISECONDS 200
#define ENUMERATOR_GET_DEFAULT_ENDPOINT 4
#define DEVICE_ACTIVATE 3
#define SESSION_MANAGER_GET_SIMPLE_VOLUME 4
#define SIMPLE_VOLUME_SET_MUTE 5
#define COM_RELEASE 2
#define RENDER_FLOW 0
#define MULTIMEDIA_ROLE 1

typedef HRESULT(__stdcall *get_default_endpoint_fn)(void *self, int flow, int role, void **device);
typedef HRESULT(__stdcall *activate_fn)(void *self, const GUID *iid, DWORD context, void *parameters, void **out);
typedef HRESULT(__stdcall *get_simple_volume_fn)(void *self, const GUID *session, BOOL cross_process, void **out);
typedef HRESULT(__stdcall *set_mute_fn)(void *self, BOOL mute, const GUID *context);
typedef ULONG(__stdcall *release_fn)(void *self);

static const GUID CLSID_DEVICE_ENUMERATOR = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID IID_DEVICE_ENUMERATOR = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const GUID IID_SESSION_MANAGER = {0xbfa971f1, 0x4d5e, 0x40bb, {0x93, 0x5e, 0x96, 0x70, 0x39, 0xbf, 0xbe, 0xe4}};

static void *slot(void *object, int index) {
    return (*(void ***)object)[index];
}

static void release(void *object) {
    if (object) ((release_fn)slot(object, COM_RELEASE))(object);
}

static int set_process_mute(int mute) {
    void *enumerator = NULL, *device = NULL, *manager = NULL, *volume = NULL;
    HRESULT result = CoCreateInstance(&CLSID_DEVICE_ENUMERATOR, NULL, CLSCTX_ALL, &IID_DEVICE_ENUMERATOR, &enumerator);
    if (SUCCEEDED(result))
        result = ((get_default_endpoint_fn)slot(enumerator, ENUMERATOR_GET_DEFAULT_ENDPOINT))(enumerator, RENDER_FLOW,
                                                                                            MULTIMEDIA_ROLE, &device);
    if (SUCCEEDED(result))
        result = ((activate_fn)slot(device, DEVICE_ACTIVATE))(device, &IID_SESSION_MANAGER, CLSCTX_ALL, NULL, &manager);
    if (SUCCEEDED(result))
        result = ((get_simple_volume_fn)slot(manager, SESSION_MANAGER_GET_SIMPLE_VOLUME))(manager, NULL, FALSE, &volume);
    if (SUCCEEDED(result)) result = ((set_mute_fn)slot(volume, SIMPLE_VOLUME_SET_MUTE))(volume, mute, NULL);
    release(volume);
    release(manager);
    release(device);
    release(enumerator);
    return SUCCEEDED(result);
}

static int game_in_front(void) {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

static DWORD WINAPI background_mute_thread(LPVOID unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int muted = 0, logged = 0;
    for (;;) {
        int want_mute = !game_in_front();
        if (want_mute != muted) {
            int ok = set_process_mute(want_mute);
            if (logged < 6) {
                log_line("background mute: %s (%s)", want_mute ? "muted" : "sound back", ok ? "ok" : "failed");
                logged++;
            }
            if (ok) muted = want_mute;
        }
        Sleep(POLL_MILLISECONDS);
    }
}

void install_background_mute(const char *ini_path) {
    if (!GetPrivateProfileIntA("sound", "mute_in_background", 0, ini_path)) return;
    HANDLE thread = CreateThread(NULL, 0, background_mute_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    log_line("background mute: the game is silent while another window is in front");
}
