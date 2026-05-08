#include <windows.h>
#include <winstring.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include "logger.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>

#pragma comment(lib, "runtimeobject.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

// --- CONFIG ---
bool g_ForcePremium = false;
std::mutex g_ConfigMutex;
DWORD g_LastConfigCheck = 0;

void UpdateConfig() {
    std::lock_guard<std::mutex> lock(g_ConfigMutex);
    DWORD now = GetTickCount();
    if (now - g_LastConfigCheck > 2000) {
        std::ifstream config("C:\\temp\\breezip_config.txt");
        std::string line;
        if (std::getline(config, line)) g_ForcePremium = (line == "true");
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

// --- HELPER ---
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// --- STORE API HOOKS (Sichern gegen alle Pfade) ---

// get_IsActive (Index 7)
typedef HRESULT (STDMETHODCALLTYPE *get_IsActive_t)(void* This, bool *value);
get_IsActive_t pOriginal_get_IsActive = nullptr;
HRESULT STDMETHODCALLTYPE Detour_get_IsActive(void* This, bool *value) {
    HRESULT hr = pOriginal_get_IsActive(This, value);
    UpdateConfig();
    if (g_ForcePremium) {
        *value = true;
        Logger::Log("[SPOOF] API: get_IsActive -> TRUE");
    }
    return hr;
}

// IStoreAppLicense / ILicenseInformation VTable Hooking
void HookLicenseObject(void* pLicense) {
    if (!pLicense) return;
    void** vtable = *(void***)pLicense;
    if (MH_CreateHook(vtable[7], &Detour_get_IsActive, reinterpret_cast<LPVOID*>(&pOriginal_get_IsActive)) == MH_OK) {
        MH_EnableHook(vtable[7]);
        Logger::Log("[HOOK] IsActive Hook installiert.");
    }
}

// --- JSON HOOKS (SINGULARITY EDITION) ---

typedef HRESULT (STDMETHODCALLTYPE *GetNamedBoolean_t)(void* This, HSTRING name, bool *value);
GetNamedBoolean_t pOriginal_GetNamedBoolean = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetNamedBoolean(void* This, HSTRING name, bool *value) {
    HRESULT hr = pOriginal_GetNamedBoolean(This, name, value);
    if (SUCCEEDED(hr) && name) {
        PCWSTR nStr = WindowsGetStringRawBuffer(name, nullptr);
        if (nStr) {
            std::wstring ws(nStr);
            UpdateConfig();
            Logger::Log("[JSON-BOOL] " + WStringToString(ws) + " = " + std::string(*value ? "TRUE" : "FALSE"));
            if (g_ForcePremium && (ws.find(L"Premium") != std::wstring::npos || ws.find(L"active") != std::wstring::npos || ws.find(L"License") != std::wstring::npos)) {
                *value = true;
                Logger::Log("[MANIPULATION] '" + WStringToString(ws) + "' -> TRUE");
            }
        }
    }
    return hr;
}

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
            if (g_ForcePremium) {
                if (wv == L"none" || wv == L"expired" || wv == L"free" || wv == L"trial" || wv == L"expired_trial") {
                    WindowsDeleteString(*value);
                    WindowsCreateString(L"active", 6, value); // Oder "Premium"
                    Logger::Log("[MANIPULATION] '" + WStringToString(wn) + "' manipuliert zu 'active'.");
                }
            }
        }
    }
    return hr;
}

// --- ACTIVATION / COM ---

typedef HRESULT (WINAPI *RoActivateInstance_t)(HSTRING activatableClassId, IInspectable** instance);
RoActivateInstance_t pOriginal_RoActivateInstance = nullptr;
HRESULT WINAPI Detour_RoActivateInstance(HSTRING activatableClassId, IInspectable** instance) {
    HRESULT hr = pOriginal_RoActivateInstance(activatableClassId, instance);
    if (SUCCEEDED(hr) && instance && *instance && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            Logger::Log("[ACTIVATE] " + WStringToString(ws));
            void** vtable = *(void***)*instance;
            if (ws == L"Windows.Data.Json.JsonObject") {
                MH_CreateHook(vtable[10], &Detour_GetNamedString, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedString));
                MH_EnableHook(vtable[10]);
                MH_CreateHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
                MH_EnableHook(vtable[12]);
            } else if (ws.find(L"StoreAppLicense") != std::wstring::npos || ws.find(L"LicenseInformation") != std::wstring::npos) {
                HookLicenseObject(*instance);
            }
        }
    }
    return hr;
}

typedef HRESULT (WINAPI *CoCreateInstance_t)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
CoCreateInstance_t pOriginal_CoCreateInstance = nullptr;
HRESULT WINAPI Detour_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv) {
    HRESULT hr = pOriginal_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv) {
        LPOLESTR clsidStr = NULL;
        StringFromCLSID(rclsid, &clsidStr);
        if (clsidStr) {
            std::wstring ws(clsidStr);
            // Bekannte CLSIDs prüfen
            if (ws == L"{00000339-0000-0000-C000-000000000046}") { /* PropertySet */ }
            Logger::Log("[COM] CLSID: " + WStringToString(ws));
            CoTaskMemFree(clsidStr);
        }
    }
    return hr;
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v6.0 'SINGULARITY' GESTARTET ===");
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
    Logger::Log("[ULTIMATE] Singularity Mode scharfgeschaltet.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
