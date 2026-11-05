#include <windows.h>
#include <string.h>
#include "import_profile.h"
#include "log.h"

#define MAX_PROFILED_IMPORTS 256
#define STUB_BYTES 16
#define PROFILE_FRAMES 1800
#define FRAME_MARKER_ORDINAL 209

typedef struct {
    char module[16];
    unsigned ordinal;
    void *original;
    volatile LONG calls;
} profiled_import;

static profiled_import imports[MAX_PROFILED_IMPORTS];
static int import_count;
static int frame_import = -1;
static unsigned char *stubs;

static int is_code_address(void *address) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof info)) return 0;
    return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static void write_stub(unsigned char *stub, profiled_import *import) {
    stub[0] = 0xF0;
    stub[1] = 0xFF;
    stub[2] = 0x05;
    *(volatile LONG **)(stub + 3) = &import->calls;
    stub[7] = 0xFF;
    stub[8] = 0x25;
    *(void ***)(stub + 9) = &import->original;
}

static void patch_slot(void **slot, void *value) {
    DWORD old_protection;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old_protection);
    *slot = value;
    VirtualProtect(slot, sizeof *slot, old_protection, &old_protection);
}

static void profile_module_imports(unsigned char *base, IMAGE_IMPORT_DESCRIPTOR *descriptor) {
    const char *module = (const char *)(base + descriptor->Name);
    if (_stricmp(module, "HglNa.dll") && _stricmp(module, "HglCore.dll")) return;
    IMAGE_THUNK_DATA32 *names = (IMAGE_THUNK_DATA32 *)(base + descriptor->OriginalFirstThunk);
    void **slots = (void **)(base + descriptor->FirstThunk);
    for (int i = 0; names[i].u1.Ordinal && import_count < MAX_PROFILED_IMPORTS; i++) {
        if (!IMAGE_SNAP_BY_ORDINAL32(names[i].u1.Ordinal) || !is_code_address(slots[i])) continue;
        profiled_import *import = &imports[import_count];
        lstrcpynA(import->module, module, sizeof import->module);
        import->ordinal = IMAGE_ORDINAL32(names[i].u1.Ordinal);
        import->original = slots[i];
        if (!_stricmp(module, "HglNa.dll") && import->ordinal == FRAME_MARKER_ORDINAL) frame_import = import_count;
        write_stub(stubs + import_count * STUB_BYTES, import);
        patch_slot(&slots[i], stubs + import_count * STUB_BYTES);
        import_count++;
    }
}

static DWORD WINAPI report_profile(LPVOID unused) {
    (void)unused;
    static LONG previous[MAX_PROFILED_IMPORTS];
    for (int window = 1;; window++) {
        while (frame_import < 0 || imports[frame_import].calls - previous[frame_import] < PROFILE_FRAMES) Sleep(500);
        LONG frames = imports[frame_import].calls - previous[frame_import];
        log_line("imports: window %d, calls per frame over %ld frames (module ordinal: calls/frame)", window, frames);
        for (int i = 0; i < import_count; i++) {
            LONG calls = imports[i].calls - previous[i];
            previous[i] += calls;
            if (calls) log_line("imports: %s %u: %.2f", imports[i].module, imports[i].ordinal, (double)calls / frames);
        }
    }
    return 0;
}

void install_import_profile(const char *ini_path) {
    if (!GetPrivateProfileIntA("debug", "profile_imports", 0, ini_path)) return;
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS32 *headers = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *directory = &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    stubs = VirtualAlloc(NULL, MAX_PROFILED_IMPORTS * STUB_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stubs || !directory->VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress);
         descriptor->Name; descriptor++) {
        profile_module_imports(base, descriptor);
    }
    log_line("imports: profiling %d engine imports; report after %d frames", import_count, PROFILE_FRAMES);
    CloseHandle(CreateThread(NULL, 0, report_profile, NULL, 0, NULL));
}
