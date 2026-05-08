#include <windows.h>
#include <winstring.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <set>
#include "logger.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

// --- HELPERS ---
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Base64 URL Decoder (JWT Format)
std::string DecodeBase64URL(std::string input) {
    // URL-Safe Base64 zu Standard Base64 konvertieren
    for (auto& c : input) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
    }
    while (input.length() % 4) input += '=';

    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[b64[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

void AnalyzeJWT(const std::string& token) {
    size_t firstDot = token.find('.');
    size_t secondDot = token.find('.', firstDot + 1);
    if (firstDot != std::string::npos && secondDot != std::string::npos) {
        std::string payload = token.substr(firstDot + 1, secondDot - firstDot - 1);
        std::string decoded = DecodeBase64URL(payload);
        Logger::Log("[JWT-DECODED] " + decoded);
    }
}

// --- MINHOOK ---
typedef enum MH_STATUS { MH_OK = 0 } MH_STATUS;
extern "C" {
    MH_STATUS WINAPI MH_Initialize();
    MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
    MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);
}

// --- DETOURS ---
typedef HRESULT (STDMETHODCALLTYPE *GetNamedString_t)(void* This, HSTRING name, HSTRING *value);
GetNamedString_t pOriginal_GetNamedString = nullptr;

HRESULT STDMETHODCALLTYPE Detour_GetNamedString(void* This, HSTRING name, HSTRING *value) {
    HRESULT hr = pOriginal_GetNamedString(This, name, value);
    if (SUCCEEDED(hr) && name && value && *value) {
        PCWSTR nStr = WindowsGetStringRawBuffer(name, nullptr);
        PCWSTR vStr = WindowsGetStringRawBuffer(*value, nullptr);
        if (nStr && vStr) {
            std::string key = WStringToString(nStr);
            std::string val = WStringToString(vStr);
            Logger::Log("[JSON-STRING] " + key + " = \"" + (val.length() > 60 ? val.substr(0, 60) + "..." : val) + "\"");

            if (key == "key" && val.find("ey") == 0) AnalyzeJWT(val);

            // Manipulation: Wenn status abgefragt wird und der Wert negativ ist
            if (key == "status" && (val == "expired" || val == "none")) {
                WindowsDeleteString(*value);
                WindowsCreateString(L"active", 6, value);
                Logger::Log("[SPOOF] status '" + val + "' -> 'active'");
            }
        }
    }
    return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *GetNamedBoolean_t)(void* This, HSTRING name, bool *value);
GetNamedBoolean_t pOriginal_GetNamedBoolean = nullptr;

HRESULT STDMETHODCALLTYPE Detour_GetNamedBoolean(void* This, HSTRING name, bool *value) {
    HRESULT hr = pOriginal_GetNamedBoolean(This, name, value);
    if (name) {
        PCWSTR nStr = WindowsGetStringRawBuffer(name, nullptr);
        if (nStr) {
            std::wstring ws(nStr);
            if (ws == L"IsProtected") {
                *value = true;
                Logger::Log("[SPOOF] IsProtected -> TRUE");
            }
        }
    }
    return hr;
}

// --- ACTIVATION ---
typedef HRESULT (WINAPI *RoActivateInstance_t)(HSTRING activatableClassId, IInspectable** instance);
RoActivateInstance_t pOriginal_RoActivateInstance = nullptr;

HRESULT WINAPI Detour_RoActivateInstance(HSTRING activatableClassId, IInspectable** instance) {
    HRESULT hr = pOriginal_RoActivateInstance(activatableClassId, instance);
    if (SUCCEEDED(hr) && instance && *instance && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr && std::wstring(classStr) == L"Windows.Data.Json.JsonObject") {
            void** vtable = *(void***)*instance;
            MH_CreateHook(vtable[10], &Detour_GetNamedString, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedString));
            MH_EnableHook(vtable[10]);
            MH_CreateHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
            MH_EnableHook(vtable[12]);
        }
    }
    return hr;
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v9.0 'CLAIM ANALYST' gestartet ===");
    MH_Initialize();
    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoAct = GetProcAddress(hCombase, "RoActivateInstance");
        MH_CreateHook(pRoAct, &Detour_RoActivateInstance, reinterpret_cast<LPVOID*>(&pOriginal_RoActivateInstance));
        MH_EnableHook(pRoAct);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
