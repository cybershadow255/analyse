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

using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

// --- KONFIGURATION CACHE ---
bool g_ForcePremium = false;
std::mutex g_ConfigMutex;
DWORD g_LastConfigCheck = 0;

void UpdateConfig() {
    std::lock_guard<std::mutex> lock(g_ConfigMutex);
    DWORD now = GetTickCount();
    if (now - g_LastConfigCheck > 5000) { // Alle 5 Sek prüfen
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

// --- JSON HOOKS ---
typedef HRESULT (STDMETHODCALLTYPE *GetNamedBoolean_t)(void* This, HSTRING name, bool *value);
GetNamedBoolean_t pOriginal_GetNamedBoolean = nullptr;

HRESULT STDMETHODCALLTYPE Detour_GetNamedBoolean(void* This, HSTRING name, bool *value) {
    HRESULT hr = pOriginal_GetNamedBoolean(This, name, value);
    if (SUCCEEDED(hr) && name) {
        PCWSTR nameStr = WindowsGetStringRawBuffer(name, nullptr);
        if (nameStr) {
            std::wstring ws(nameStr);
            UpdateConfig(); // Nutzt Cache

            if (g_ForcePremium) {
                if (ws.find(L"Premium") != std::wstring::npos ||
                    ws.find(L"License") != std::wstring::npos ||
                    ws.find(L"active") != std::wstring::npos ||
                    ws.find(L"pro") != std::wstring::npos) {
                    *value = true;
                    Logger::Log("[MANIPULATION] JSON Bool '" + std::string(ws.begin(), ws.end()) + "' -> TRUE");
                }
            }
        }
    }
    return hr;
}

// --- ACTIVATION HOOKS ---
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
                // Index 12 für IJsonObject::GetNamedBoolean
                if (MH_CreateHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean)) == MH_OK) {
                    MH_EnableHook(vtable[12]);
                    Logger::Log("[HOOK] IJsonObject::GetNamedBoolean (Index 12) gehookt.");
                }
            }
        }
    }
    return hr;
}

void HookThread() {
    Logger::Init("C:\\temp\\breezip_analysis.log");
    Logger::Log("=== BreeZip v3.3.1 'Optimized Observer' gestartet ===");
    MH_Initialize();

    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoAct = GetProcAddress(hCombase, "RoActivateInstance");
        if (pRoAct) {
            MH_CreateHook(pRoAct, &Detour_RoActivateInstance, reinterpret_cast<LPVOID*>(&pOriginal_RoActivateInstance));
            MH_EnableHook(pRoAct);
            Logger::Log("[HOOK] RoActivateInstance aktiv.");
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
