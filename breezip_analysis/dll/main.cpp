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
#include <winrt/Windows.Services.Store.h>

#pragma comment(lib, "runtimeobject.lib")

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Services::Store;

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

// --- HOOKING KETTE (STORE API) ---

typedef HRESULT (STDMETHODCALLTYPE *get_IsActive_t)(void* This, bool *value);
get_IsActive_t pOriginal_get_IsActive = nullptr;
HRESULT STDMETHODCALLTYPE Detour_get_IsActive(void* This, bool *value) {
    HRESULT hr = pOriginal_get_IsActive(This, value);
    UpdateConfig();
    if (g_ForcePremium) { *value = true; Logger::Log("[OMNI] StoreContext::get_IsActive -> TRUE"); }
    return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *GetResults_t)(void* This, void** ppLicense);
GetResults_t pOriginal_GetResults = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetResults(void* This, void** ppLicense) {
    HRESULT hr = pOriginal_GetResults(This, ppLicense);
    if (SUCCEEDED(hr) && ppLicense && *ppLicense) {
        void** vtable = *(void***)*ppLicense;
        MH_CreateHook(vtable[7], &Detour_get_IsActive, reinterpret_cast<LPVOID*>(&pOriginal_get_IsActive));
        MH_EnableHook(vtable[7]);
    }
    return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *GetAppLicenseAsync_t)(void* This, void** ppAsyncOp);
GetAppLicenseAsync_t pOriginal_GetAppLicenseAsync = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetAppLicenseAsync(void* This, void** ppAsyncOp) {
    HRESULT hr = pOriginal_GetAppLicenseAsync(This, ppAsyncOp);
    if (SUCCEEDED(hr) && ppAsyncOp && *ppAsyncOp) {
        void** vtable = *(void***)*ppAsyncOp;
        MH_CreateHook(vtable[8], &Detour_GetResults, reinterpret_cast<LPVOID*>(&pOriginal_GetResults));
        MH_EnableHook(vtable[8]);
    }
    return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *GetDefault_t)(void* This, void** ppContext);
GetDefault_t pOriginal_GetDefault = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetDefault(void* This, void** ppContext) {
    HRESULT hr = pOriginal_GetDefault(This, ppContext);
    if (SUCCEEDED(hr) && ppContext && *ppContext) {
        void** vtable = *(void***)*ppContext;
        MH_CreateHook(vtable[6], &Detour_GetAppLicenseAsync, reinterpret_cast<LPVOID*>(&pOriginal_GetAppLicenseAsync));
        MH_EnableHook(vtable[6]);
    }
    return hr;
}

// --- JSON HOOKS (OMNI EDITION) ---

// GetNamedString (Index 10)
typedef HRESULT (STDMETHODCALLTYPE *GetNamedString_t)(void* This, HSTRING name, HSTRING *value);
GetNamedString_t pOriginal_GetNamedString = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetNamedString(void* This, HSTRING name, HSTRING *value) {
    HRESULT hr = pOriginal_GetNamedString(This, name, value);
    if (SUCCEEDED(hr) && name) {
        PCWSTR nameStr = WindowsGetStringRawBuffer(name, nullptr);
        UpdateConfig();
        if (g_ForcePremium && nameStr && (std::wstring(nameStr).find(L"Premium") != std::wstring::npos)) {
            WindowsDeleteString(*value);
            WindowsCreateString(L"true", 4, value); // Oder "premium"
            Logger::Log("[OMNI] JSON String '" + std::string(nameStr, nameStr + wcslen(nameStr)) + "' manipuliert.");
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
        UpdateConfig();
        if (g_ForcePremium && nStr) {
            std::wstring ws(nStr);
            if (ws.find(L"Premium") != std::wstring::npos || ws.find(L"active") != std::wstring::npos || ws.find(L"pro") != std::wstring::npos) {
                *value = true;
                Logger::Log("[OMNI] JSON Bool '" + std::string(ws.begin(), ws.end()) + "' -> TRUE");
            }
        }
    }
    return hr;
}

// --- ACTIVATION HOOK ---
typedef HRESULT (WINAPI *RoActivateInstance_t)(HSTRING activatableClassId, IInspectable** instance);
RoActivateInstance_t pOriginal_RoActivateInstance = nullptr;

HRESULT WINAPI Detour_RoActivateInstance(HSTRING activatableClassId, IInspectable** instance) {
    HRESULT hr = pOriginal_RoActivateInstance(activatableClassId, instance);
    if (SUCCEEDED(hr) && instance && *instance && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            void** vtable = *(void***)*instance;
            if (ws == L"Windows.Data.Json.JsonObject") {
                MH_CreateHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
                MH_EnableHook(vtable[12]);
                MH_CreateHook(vtable[10], &Detour_GetNamedString, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedString));
                MH_EnableHook(vtable[10]);
            }
        }
    }
    return hr;
}

typedef HRESULT (WINAPI *RoGetActivationFactory_t)(HSTRING activatableClassId, REFIID iid, void** factory);
RoGetActivationFactory_t pOriginal_RoGetActivationFactory = nullptr;

HRESULT WINAPI Detour_RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void** factory) {
    HRESULT hr = pOriginal_RoGetActivationFactory(activatableClassId, iid, factory);
    if (SUCCEEDED(hr) && factory && *factory) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            void** vtable = *(void***)*factory;
            if (ws == L"Windows.Services.Store.StoreContext") {
                MH_CreateHook(vtable[6], &Detour_GetDefault, reinterpret_cast<LPVOID*>(&pOriginal_GetDefault));
                MH_EnableHook(vtable[6]);
            }
        }
    }
    return hr;
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v4.0 'OMNI-HOOK' GESTARTET ===");
    MH_Initialize();
    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoAct = GetProcAddress(hCombase, "RoActivateInstance");
        MH_CreateHook(pRoAct, &Detour_RoActivateInstance, reinterpret_cast<LPVOID*>(&pOriginal_RoActivateInstance));
        MH_EnableHook(pRoAct);
        void* pRoGet = GetProcAddress(hCombase, "RoGetActivationFactory");
        MH_CreateHook(pRoGet, &Detour_RoGetActivationFactory, reinterpret_cast<LPVOID*>(&pOriginal_RoGetActivationFactory));
        MH_EnableHook(pRoGet);
    }
    Logger::Log("[ULTIMATE] Alle Überwachungssysteme scharfgeschaltet.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
