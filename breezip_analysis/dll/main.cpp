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
        if (std::getline(config, line)) {
            g_ForcePremium = (line == "true");
        } else {
            // Default auf true setzen für den Benutzer
            std::ofstream out("C:\\temp\\breezip_config.txt");
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

// --- HELPER ---
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// --- JSON OVERKILL ---

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
                // Aggressives Spoofing
                if (ws.find(L"Premium") != std::wstring::npos ||
                    ws.find(L"active") != std::wstring::npos ||
                    ws.find(L"pro") != std::wstring::npos ||
                    ws.find(L"License") != std::wstring::npos ||
                    ws.find(L"IsProtected") != std::wstring::npos ||
                    ws.find(L"success") != std::wstring::npos) {

                    if (*value == false) {
                        *value = true;
                        Logger::Log("[MANIPULATION] '" + WStringToString(ws) + "' auf TRUE erzwungen.");
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
            Logger::Log("[JSON-STRING] " + WStringToString(wn) + " = \"" + WStringToString(wv) + "\"");

            UpdateConfig();
            if (g_ForcePremium) {
                if (wn == L"status" || wn == L"type" || wn == L"Value" || wn == L"licenseType") {
                    if (wv == L"none" || wv == L"expired" || wv == L"free" || wv == L"trial") {
                        WindowsDeleteString(*value);
                        WindowsCreateString(L"Premium", 7, value);
                        Logger::Log("[MANIPULATION] '" + WStringToString(wn) + "' von '" + WStringToString(wv) + "' auf 'Premium' geändert.");
                    }
                }
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
        if (classStr) {
            std::wstring ws(classStr);
            Logger::Log("[ACTIVATE] " + WStringToString(ws));

            if (ws == L"Windows.Data.Json.JsonObject") {
                void** vtable = *(void***)*instance;
                // Index 10: GetNamedString, Index 12: GetNamedBoolean
                MH_CreateHook(vtable[10], &Detour_GetNamedString, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedString));
                MH_EnableHook(vtable[10]);
                MH_CreateHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
                MH_EnableHook(vtable[12]);
            }
        }
    }
    return hr;
}

// --- COM ---
typedef HRESULT (WINAPI *CoCreateInstance_t)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
CoCreateInstance_t pOriginal_CoCreateInstance = nullptr;

HRESULT WINAPI Detour_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv) {
    HRESULT hr = pOriginal_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    LPOLESTR clsidStr = NULL;
    StringFromCLSID(rclsid, &clsidStr);
    if (clsidStr) {
        std::wstring ws(clsidStr);
        Logger::Log("[COM] CoCreateInstance CLSID: " + WStringToString(ws));
        CoTaskMemFree(clsidStr);
    }
    return hr;
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v4.5 'BRUTE FORCE' GESTARTET ===");
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

    Logger::Log("[ULTIMATE] Brute Force Hooks aktiv.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
