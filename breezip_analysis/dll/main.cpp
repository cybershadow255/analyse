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

// --- DYNAMISCHE PFADE ---
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring GetLogPath() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"breezip_analysis.log";
}

std::wstring GetConfigPath() {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"breezip_config.txt";
}

// --- CONFIG ---
bool g_ForcePremium = false;
std::mutex g_ConfigMutex;
DWORD g_LastConfigCheck = 0;

void UpdateConfig() {
    std::lock_guard<std::mutex> lock(g_ConfigMutex);
    DWORD now = GetTickCount();
    if (now - g_LastConfigCheck > 2000) {
        std::ifstream config(GetConfigPath());
        std::string line;
        if (std::getline(config, line)) g_ForcePremium = (line == "true");
        else {
            std::ofstream out(GetConfigPath());
            out << "true";
            g_ForcePremium = true;
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

// --- HOOK TRACKER ---
std::set<void*> g_HookedAddresses;
std::mutex g_HookMutex;

bool InstallHookOnce(void* target, void* detour, void** original) {
    if (!target) return false;
    std::lock_guard<std::mutex> lock(g_HookMutex);
    if (g_HookedAddresses.find(target) != g_HookedAddresses.end()) return false;
    if (MH_CreateHook(target, detour, original) == MH_OK) {
        MH_EnableHook(target);
        g_HookedAddresses.insert(target);
        return true;
    }
    return false;
}

// --- CORE DETOURS ---

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
            if (g_ForcePremium) {
                if (ws.find(L"Premium") != std::wstring::npos || ws.find(L"active") != std::wstring::npos ||
                    ws.find(L"pro") != std::wstring::npos || ws.find(L"IsProtected") != std::wstring::npos ||
                    ws.find(L"success") != std::wstring::npos || ws.find(L"Licensed") != std::wstring::npos) {
                    if (*value == false) {
                        *value = true;
                        Logger::Log("[MANIPULATION] Forced TRUE for " + WStringToString(ws));
                    }
                }
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
            UpdateConfig();
            Logger::Log("[JSON-STRING] " + WStringToString(wn) + " = \"" + WStringToString(wv) + "\"");
            if (g_ForcePremium) {
                if (wv == L"none" || wv == L"expired" || wv == L"free" || wv == L"trial" || wv == L"expired_trial") {
                    WindowsDeleteString(*value);
                    WindowsCreateString(L"active", 6, value);
                    Logger::Log("[MANIPULATION] String '" + WStringToString(wn) + "' set to 'active'");
                } else if (wn == L"licenseType" || wn == L"status" || wn == L"edition") {
                    if (wv != L"Premium" && wv != L"active" && wv != L"pro") {
                        WindowsDeleteString(*value);
                        WindowsCreateString(L"Premium", 7, value);
                        Logger::Log("[MANIPULATION] '" + WStringToString(wn) + "' set to 'Premium'");
                    }
                }
            }
        }
    }
    return hr;
}

// Hook für Instanzen (RoActivateInstance)
void HookJsonObjectInstance(void* instance) {
    if (!instance) return;
    void** vtable = *(void***)instance;
    InstallHookOnce(vtable[10], &Detour_GetNamedString, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedString));
    InstallHookOnce(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
}

// Hook für Parse()
typedef HRESULT (STDMETHODCALLTYPE *Parse_t)(void* This, HSTRING json, void** ppJsonObject);
Parse_t pOriginal_Parse = nullptr;

HRESULT STDMETHODCALLTYPE Detour_Parse(void* This, HSTRING json, void** ppJsonObject) {
    HRESULT hr = pOriginal_Parse(This, json, ppJsonObject);
    if (SUCCEEDED(hr) && ppJsonObject && *ppJsonObject) {
        HookJsonObjectInstance(*ppJsonObject);
    }
    return hr;
}

// ACTIVATION & FACTORY HOOKS
typedef HRESULT (WINAPI *RoActivateInstance_t)(HSTRING activatableClassId, IInspectable** instance);
RoActivateInstance_t pOriginal_RoActivateInstance = nullptr;

HRESULT WINAPI Detour_RoActivateInstance(HSTRING activatableClassId, IInspectable** instance) {
    HRESULT hr = pOriginal_RoActivateInstance(activatableClassId, instance);
    if (SUCCEEDED(hr) && instance && *instance && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            Logger::Log("[ACTIVATE] " + WStringToString(ws));
            if (ws == L"Windows.Data.Json.JsonObject") {
                HookJsonObjectInstance(*instance);
            }
        }
    }
    return hr;
}

typedef HRESULT (WINAPI *RoGetActivationFactory_t)(HSTRING activatableClassId, REFIID iid, void** factory);
RoGetActivationFactory_t pOriginal_RoGetActivationFactory = nullptr;

HRESULT WINAPI Detour_RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void** factory) {
    HRESULT hr = pOriginal_RoGetActivationFactory(activatableClassId, iid, factory);
    if (SUCCEEDED(hr) && factory && *factory && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            if (ws == L"Windows.Data.Json.JsonObject") {
                void** vtable = *(void***)*factory;
                InstallHookOnce(vtable[6], &Detour_Parse, reinterpret_cast<LPVOID*>(&pOriginal_Parse));
            }
        }
    }
    return hr;
}

typedef HRESULT (WINAPI *CoCreateInstance_t)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
CoCreateInstance_t pOriginal_CoCreateInstance = nullptr;

HRESULT WINAPI Detour_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv) {
    HRESULT hr = pOriginal_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr)) {
        LPOLESTR clsidStr = NULL;
        StringFromCLSID(rclsid, &clsidStr);
        if (clsidStr) {
            Logger::Log("[COM] CLSID: " + WStringToString(clsidStr));
            CoTaskMemFree(clsidStr);
        }
    }
    return hr;
}

void HookThread() {
    Logger::Init(WStringToString(GetLogPath()));
    Logger::Log("=== BreeZip v7.2 'CORE' GESTARTET ===");
    Sleep(2000);
    MH_Initialize();
    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoAct = GetProcAddress(hCombase, "RoActivateInstance");
        InstallHookOnce(pRoAct, &Detour_RoActivateInstance, reinterpret_cast<LPVOID*>(&pOriginal_RoActivateInstance));
        void* pRoGet = GetProcAddress(hCombase, "RoGetActivationFactory");
        InstallHookOnce(pRoGet, &Detour_RoGetActivationFactory, reinterpret_cast<LPVOID*>(&pOriginal_RoGetActivationFactory));
    }
    HMODULE hOle32 = GetModuleHandleA("ole32.dll");
    if (hOle32) {
        void* pCoCreate = GetProcAddress(hOle32, "CoCreateInstance");
        InstallHookOnce(pCoCreate, &Detour_CoCreateInstance, reinterpret_cast<LPVOID*>(&pOriginal_CoCreateInstance));
    }
    Logger::Log("[ULTIMATE] Core-Hooks aktiv.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
