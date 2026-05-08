#include <windows.h>
#include <winstring.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include "logger.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

// --- HELPER ---
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// --- CONFIG CACHE ---
bool g_ForcePremium = false;
std::mutex g_ConfigMutex;
DWORD g_LastConfigCheck = 0;

void UpdateConfig() {
    std::lock_guard<std::mutex> lock(g_ConfigMutex);
    DWORD now = GetTickCount();
    if (now - g_LastConfigCheck > 3000) {
        std::ifstream config("C:\\temp\\breezip_config.txt");
        std::string line;
        if (std::getline(config, line)) {
            g_ForcePremium = (line == "true");
        }
        g_LastConfigCheck = now;
    }
}

// --- MINHOOK ---
typedef enum MH_STATUS { MH_OK = 0 } MH_STATUS;
extern "C" {
    MH_STATUS WINAPI MH_Initialize();
    MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
    MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);
}

// --- JSON DETOURS (THE DATA MINER) ---

// GetNamedString (Index 10)
typedef HRESULT (STDMETHODCALLTYPE *GetNamedString_t)(void* This, HSTRING name, HSTRING *value);
GetNamedString_t pOriginal_GetNamedString = nullptr;

HRESULT STDMETHODCALLTYPE Detour_GetNamedString(void* This, HSTRING name, HSTRING *value) {
    HRESULT hr = pOriginal_GetNamedString(This, name, value);
    if (SUCCEEDED(hr) && name && value && *value) {
        PCWSTR nStr = WindowsGetStringRawBuffer(name, nullptr);
        PCWSTR vStr = WindowsGetStringRawBuffer(*value, nullptr);
        if (nStr && vStr) {
            std::wstring wn(nStr), wv(vStr);
            Logger::Log("[JSON-STRING] " + WStringToString(wn) + " = \"" + WStringToString(wv) + "\"");

            UpdateConfig();
            if (g_ForcePremium && (wn == L"Value" || wn == L"status")) {
                if (wv == L"none" || wv == L"false" || wv == L"expired") {
                    WindowsDeleteString(*value);
                    WindowsCreateString(L"active", 6, value);
                    Logger::Log("[MANIPULATION] JSON String '" + WStringToString(wn) + "' auf 'active' gesetzt!");
                }
            }
        }
    }
    return hr;
}

// GetNamedBoolean (Index 12)
typedef HRESULT (STDMETHODCALLTYPE *GetNamedBoolean_t)(void* This, HSTRING name, bool *value);
GetNamedBoolean_t pOriginal_GetNamedBoolean = nullptr;

HRESULT STDMETHODCALLTYPE Detour_GetNamedBoolean(void* This, HSTRING name, bool *value) {
    HRESULT hr = pOriginal_GetNamedBoolean(This, name, value);
    if (SUCCEEDED(hr) && name) {
        PCWSTR nStr = WindowsGetStringRawBuffer(name, nullptr);
        if (nStr) {
            std::wstring ws(nStr);
            Logger::Log("[JSON-BOOL] " + WStringToString(ws) + " = " + std::string(*value ? "TRUE" : "FALSE"));

            UpdateConfig();
            if (g_ForcePremium && (ws == L"IsProtected" || ws == L"active" || ws.find(L"Premium") != std::wstring::npos)) {
                *value = true;
                Logger::Log("[MANIPULATION] JSON Bool '" + WStringToString(ws) + "' -> TRUE");
            }
        }
    }
    return hr;
}

// --- ACTIVATION DETOURS ---
typedef HRESULT (WINAPI *RoActivateInstance_t)(HSTRING activatableClassId, IInspectable** instance);
RoActivateInstance_t pOriginal_RoActivateInstance = nullptr;

HRESULT WINAPI Detour_RoActivateInstance(HSTRING activatableClassId, IInspectable** instance) {
    HRESULT hr = pOriginal_RoActivateInstance(activatableClassId, instance);
    if (SUCCEEDED(hr) && instance && *instance && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            if (ws == L"Windows.Data.Json.JsonObject") {
                void** vtable = *(void***)*instance;
                MH_CreateHook(vtable[10], &Detour_GetNamedString, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedString));
                MH_EnableHook(vtable[10]);
                MH_CreateHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
                MH_EnableHook(vtable[12]);
                Logger::Log("[HOOK] JSON Data-Miner aktiv.");
            }
        }
    }
    return hr;
}

typedef HRESULT (WINAPI *CoCreateInstance_t)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
CoCreateInstance_t pOriginal_CoCreateInstance = nullptr;

HRESULT WINAPI Detour_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv) {
    LPOLESTR clsidStr = NULL;
    StringFromCLSID(rclsid, &clsidStr);
    if (clsidStr) {
        std::wstring ws(clsidStr);
        Logger::Log("[COM] CoCreateInstance CLSID: " + WStringToString(ws));
        CoTaskMemFree(clsidStr);
    }
    return pOriginal_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v4.4 'The Data Miner' gestartet ===");
    Sleep(2000);
    MH_Initialize();

    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoAct = GetProcAddress(hCombase, "RoActivateInstance");
        MH_CreateHook(pRoAct, &Detour_RoActivateInstance, reinterpret_cast<LPVOID*>(&pOriginal_RoActivateInstance));
        MH_EnableHook(pRoAct);
    }

    HMODULE hOle32 = GetModuleHandleA("ole32.dll");
    if (hOle32) {
        void* pCoCreate = GetProcAddress(hOle32, "CoCreateInstance");
        MH_CreateHook(pCoCreate, &Detour_CoCreateInstance, reinterpret_cast<LPVOID*>(&pOriginal_CoCreateInstance));
        MH_EnableHook(pCoCreate);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
