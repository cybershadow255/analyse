#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "logger.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Services.Store.h>

using namespace winrt;
using namespace Windows::Services::Store;
using namespace Windows::Foundation;

// --- MINHOOK ---
typedef enum MH_STATUS { MH_OK = 0 } MH_STATUS;
extern "C" {
    MH_STATUS WINAPI MH_Initialize();
    MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
    MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);
}

// --- GLOBALE ORIGINALS & DETOURS (KORRIGIERTE INDIZES) ---

// IStoreAppLicense::get_IsActive (Index 7)
typedef HRESULT (STDMETHODCALLTYPE *get_IsActive_t)(void* This, bool *value);
get_IsActive_t pOriginal_get_IsActive = nullptr;
HRESULT STDMETHODCALLTYPE Detour_get_IsActive(void* This, bool *value) {
    HRESULT hr = pOriginal_get_IsActive(This, value);
    Logger::Log("[ANALYSIS] IStoreAppLicense::get_IsActive = " + std::string(*value ? "TRUE" : "FALSE"));
    std::ifstream config("C:\\temp\\breezip_config.txt");
    std::string line;
    if (std::getline(config, line) && line == "true") {
        *value = true;
        Logger::Log("[MANIPULATION] get_IsActive erzwungen!");
    }
    return hr;
}

// IAsyncOperation<StoreAppLicense>::GetResults (Index 8)
typedef HRESULT (STDMETHODCALLTYPE *GetResults_t)(void* This, void** ppLicense);
GetResults_t pOriginal_GetResults = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetResults(void* This, void** ppLicense) {
    HRESULT hr = pOriginal_GetResults(This, ppLicense);
    if (SUCCEEDED(hr) && ppLicense && *ppLicense) {
        void** vtable = *(void***)*ppLicense;
        if (MH_CreateHook(vtable[7], &Detour_get_IsActive, reinterpret_cast<LPVOID*>(&pOriginal_get_IsActive)) == MH_OK) {
            MH_EnableHook(vtable[7]);
            Logger::Log("[HOOK] get_IsActive (Index 7) installiert.");
        }
    }
    return hr;
}

// IStoreContext::GetAppLicenseAsync (Index 6)
typedef HRESULT (STDMETHODCALLTYPE *GetAppLicenseAsync_t)(void* This, void** ppAsyncOp);
GetAppLicenseAsync_t pOriginal_GetAppLicenseAsync = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetAppLicenseAsync(void* This, void** ppAsyncOp) {
    HRESULT hr = pOriginal_GetAppLicenseAsync(This, ppAsyncOp);
    if (SUCCEEDED(hr) && ppAsyncOp && *ppAsyncOp) {
        void** vtable = *(void***)*ppAsyncOp;
        if (MH_CreateHook(vtable[8], &Detour_GetResults, reinterpret_cast<LPVOID*>(&pOriginal_GetResults)) == MH_OK) {
            MH_EnableHook(vtable[8]);
            Logger::Log("[HOOK] GetResults (Index 8) installiert.");
        }
    }
    return hr;
}

// IStoreContextStatics::GetDefault (Index 6)
typedef HRESULT (STDMETHODCALLTYPE *GetDefault_t)(void* This, void** ppContext);
GetDefault_t pOriginal_GetDefault = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetDefault(void* This, void** ppContext) {
    HRESULT hr = pOriginal_GetDefault(This, ppContext);
    if (SUCCEEDED(hr) && ppContext && *ppContext) {
        void** vtable = *(void***)*ppContext;
        if (MH_CreateHook(vtable[6], &Detour_GetAppLicenseAsync, reinterpret_cast<LPVOID*>(&pOriginal_GetAppLicenseAsync)) == MH_OK) {
            MH_EnableHook(vtable[6]);
            Logger::Log("[HOOK] GetAppLicenseAsync (Index 6) installiert.");
        }
    }
    return hr;
}

// --- ALTE API: CurrentApp ---

typedef HRESULT (STDMETHODCALLTYPE *get_IsActiveOld_t)(void* This, bool *value);
get_IsActiveOld_t pOriginal_get_IsActiveOld = nullptr;
HRESULT STDMETHODCALLTYPE Detour_get_IsActiveOld(void* This, bool *value) {
    HRESULT hr = pOriginal_get_IsActiveOld(This, value);
    Logger::Log("[ANALYSIS] OldAPI::get_IsActive = " + std::string(*value ? "TRUE" : "FALSE"));
    std::ifstream config("C:\\temp\\breezip_config.txt");
    std::string line;
    if (std::getline(config, line) && line == "true") {
        *value = true;
        Logger::Log("[MANIPULATION] OldAPI erzwungen!");
    }
    return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *get_LicenseInformation_t)(void* This, void** ppLicenseInfo);
get_LicenseInformation_t pOriginal_get_LicenseInformation = nullptr;
HRESULT STDMETHODCALLTYPE Detour_get_LicenseInformation(void* This, void** ppLicenseInfo) {
    HRESULT hr = pOriginal_get_LicenseInformation(This, ppLicenseInfo);
    if (SUCCEEDED(hr) && ppLicenseInfo && *ppLicenseInfo) {
        void** vtable = *(void***)*ppLicenseInfo;
        if (MH_CreateHook(vtable[7], &Detour_get_IsActiveOld, reinterpret_cast<LPVOID*>(&pOriginal_get_IsActiveOld)) == MH_OK) {
            MH_EnableHook(vtable[7]);
            Logger::Log("[HOOK] Old get_IsActive (Index 7) installiert.");
        }
    }
    return hr;
}

// --- BASIS ---
typedef HRESULT (WINAPI *RoGetActivationFactory_t)(HSTRING activatableClassId, REFIID iid, void** factory);
RoGetActivationFactory_t pOriginal_RoGetActivationFactory = nullptr;

HRESULT WINAPI Detour_RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void** factory) {
    HRESULT hr = pOriginal_RoGetActivationFactory(activatableClassId, iid, factory);
    if (SUCCEEDED(hr) && factory && *factory) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            Logger::Log("[SCANNER] App fordert an: " + std::string(ws.begin(), ws.end()));
            void** vtable = *(void***)*factory;
            if (ws == L"Windows.Services.Store.StoreContext") {
                MH_CreateHook(vtable[6], &Detour_GetDefault, reinterpret_cast<LPVOID*>(&pOriginal_GetDefault));
                MH_EnableHook(vtable[6]);
                Logger::Log("[HOOK] StoreContext Factory (Index 6) gehookt.");
            } else if (ws == L"Windows.ApplicationModel.Store.CurrentApp") {
                MH_CreateHook(vtable[6], &Detour_get_LicenseInformation, reinterpret_cast<LPVOID*>(&pOriginal_get_LicenseInformation));
                MH_EnableHook(vtable[6]);
                Logger::Log("[HOOK] CurrentApp Factory (Index 6) gehookt.");
            }
        }
    }
    return hr;
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v3.2 Final Release (Corrected Indices) ===");
    MH_Initialize();
    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoGet = GetProcAddress(hCombase, "RoGetActivationFactory");
        MH_CreateHook(pRoGet, &Detour_RoGetActivationFactory, reinterpret_cast<LPVOID*>(&pOriginal_RoGetActivationFactory));
        MH_EnableHook(pRoGet);
        Logger::Log("[HOOK] RoGetActivationFactory aktiv.");
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
