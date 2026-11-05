#include <windows.h>
#include "timing_hooks.h"
#include "log.h"
#include "MinHook.h"

#define HGLCORE_GHIDRA_BASE 0x10000000
#define LIMITER_IDLE_CALLBACK_VA 0x10019530
#define TIMER_HIGH_RESOLUTION_FLAG 0x00000002
#define SLEEP_MARGIN_MS 1.5f
#define STATS_INTERVAL_MS 10000.0

typedef void(__cdecl *limiter_idle_fn)(float remaining_ms);
typedef int(__cdecl *vsync_fn)(void);

static vsync_fn original_vsync;
static HANDLE wait_timer;
static LARGE_INTEGER counter_frequency, stats_start, last_frame;
static int frames_in_interval;
static double longest_frame_ms;
static ULONGLONG cpu_at_stats_start;

static void __cdecl sleep_most_of_frame_wait(float remaining_ms) {
    if (remaining_ms <= SLEEP_MARGIN_MS + 0.5f) return;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)((remaining_ms - SLEEP_MARGIN_MS) * 10000.0f);
    if (SetWaitableTimer(wait_timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(wait_timer, INFINITE);
}

static double elapsed_ms(LARGE_INTEGER from, LARGE_INTEGER to) {
    return (double)(to.QuadPart - from.QuadPart) * 1000.0 / (double)counter_frequency.QuadPart;
}

static ULONGLONG process_cpu_100ns(void) {
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    return ((ULONGLONG)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) +
           ((ULONGLONG)user.dwHighDateTime << 32 | user.dwLowDateTime);
}

void timing_count_presented_frame(void) {
    frames_in_interval++;
}

static void record_frame(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (stats_start.QuadPart == 0) {
        stats_start = last_frame = now;
        cpu_at_stats_start = process_cpu_100ns();
        return;
    }
    double frame_ms = elapsed_ms(last_frame, now);
    if (frame_ms > longest_frame_ms) longest_frame_ms = frame_ms;
    last_frame = now;
    frames_in_interval++;
    double interval_ms = elapsed_ms(stats_start, now);
    if (interval_ms < STATS_INTERVAL_MS) return;
    ULONGLONG cpu = process_cpu_100ns();
    log_line("timing: %.2f frames/s presented, longest game update %.1f ms, CPU %.0f%% of one core", frames_in_interval * 1000.0 / interval_ms,
             longest_frame_ms, (double)(cpu - cpu_at_stats_start) / 10000.0 / interval_ms * 100.0);
    stats_start = now;
    cpu_at_stats_start = cpu;
    frames_in_interval = 0;
    longest_frame_ms = 0.0;
}

static int __cdecl vsync_hook(void) {
    int result = original_vsync();
    record_frame();
    return result;
}

static HANDLE create_wait_timer(void) {
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, TIMER_HIGH_RESOLUTION_FLAG, TIMER_ALL_ACCESS);
    if (timer) return timer;
    timeBeginPeriod(1);
    log_line("timing: high-resolution timer unavailable, using a 1 ms system timer");
    return CreateWaitableTimerW(NULL, FALSE, NULL);
}

void install_timing_hooks(const char *ini_path) {
    HMODULE hglcore = GetModuleHandleA("HglCore.dll");
    if (!hglcore) return;
    QueryPerformanceFrequency(&counter_frequency);
    char limiter[16];
    GetPrivateProfileStringA("timing", "limiter", "timer", limiter, sizeof limiter, ini_path);
    if (lstrcmpiA(limiter, "spin") != 0) {
        wait_timer = create_wait_timer();
        if (wait_timer) {
            limiter_idle_fn *slot =
                (limiter_idle_fn *)((unsigned char *)hglcore + (LIMITER_IDLE_CALLBACK_VA - HGLCORE_GHIDRA_BASE));
            *slot = sleep_most_of_frame_wait;
        }
    }
    log_line("timing: frame limiter %s", wait_timer ? "sleeps on a waitable timer" : "spins (original)");
    if (GetPrivateProfileIntA("timing", "stats", 0, ini_path)) {
        void *vsync = (void *)GetProcAddress(hglcore, "VSync");
        MH_STATUS status = vsync ? MH_CreateHook(vsync, vsync_hook, (void **)&original_vsync) : MH_ERROR_FUNCTION_NOT_FOUND;
        if (status == MH_OK) status = MH_EnableHook(vsync);
        log_line("hook VSync at %p: %s", vsync, MH_StatusToString(status));
    }
}
