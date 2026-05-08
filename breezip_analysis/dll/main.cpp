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

std::wstring GetConfigPath() {
    wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"breezip_config.txt";
}

bool g_ForcePremium = false;
bool g_BlockInternet = false;
void UpdateConfig() {
    static DWORD lastCheck = 0;
    if (GetTickCount() - lastCheck < 2000) return;
    std::ifstream config(GetConfigPath());
    std::string line;
    while (std::getline(config, line)) {
        if (line == "true") g_ForcePremium = true;
        if (line == "block") g_BlockInternet = true;
    }
    lastCheck = GetTickCount();
}

// --- MINHOOK ---
typedef enum MH_STATUS { MH_OK = 0 } MH_STATUS;
extern "C" {
    MH_STATUS WINAPI MH_Initialize();
    MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
    MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget);
}

std::set<void*> g_Hooks;
void SafeHook(void* target, void* detour, void** original) {
    if (!target) return;
    if (MH_CreateHook(target, detour, original) == MH_OK) MH_EnableHook(target);
}

// --- JSON DETOURS ---
typedef HRESULT (STDMETHODCALLTYPE *GetNamedBoolean_t)(void* This, HSTRING name, bool *value);
GetNamedBoolean_t pOriginal_GetNamedBoolean = nullptr;
HRESULT STDMETHODCALLTYPE Detour_GetNamedBoolean(void* This, HSTRING name, bool *value) {
    HRESULT hr = pOriginal_GetNamedBoolean(This, name, value);
    if (SUCCEEDED(hr) && name) {
        PCWSTR nStr = WindowsGetStringRawBuffer(name, nullptr);
        if (nStr) {
            UpdateConfig();
            if (g_ForcePremium) {
                std::wstring ws(nStr);
                if (ws.find(L"Premium") != std::wstring::npos || ws.find(L"active") != std::wstring::npos || ws.find(L"IsProtected") != std::wstring::npos) {
                    *value = true;
                    Logger::Log("[SPOOF] JSON Bool '" + WStringToString(ws) + "' -> TRUE");
                }
            }
        }
    }
    return hr;
}

// --- HTTP BLOCKER (The Wall) ---
typedef HRESULT (STDMETHODCALLTYPE *SendRequestAsync_t)(void* This, void* request, void** operation);
SendRequestAsync_t pOriginal_SendRequestAsync = nullptr;
HRESULT STDMETHODCALLTYPE Detour_SendRequestAsync(void* This, void* request, void** operation) {
    UpdateConfig();
    if (g_BlockInternet) {
        Logger::Log("[WALL] Internet-Anfrage blockiert!");
        return HRESULT_FROM_WIN32(ERROR_INTERNET_CANNOT_CONNECT);
    }
    return pOriginal_SendRequestAsync(This, request, operation);
}

// --- ACTIVATION ---
void HookInstance(void* instance, const std::wstring& name) {
    void** vtable = *(void***)instance;
    if (name == L"Windows.Data.Json.JsonObject") {
        SafeHook(vtable[12], &Detour_GetNamedBoolean, reinterpret_cast<LPVOID*>(&pOriginal_GetNamedBoolean));
    } else if (name == L"Windows.Web.Http.Filters.HttpBaseProtocolFilter") {
        SafeHook(vtable[13], &Detour_SendRequestAsync, reinterpret_cast<LPVOID*>(&pOriginal_SendRequestAsync));
        Logger::Log("[WALL] HTTP-Filter gehookt (Index 13).");
    }
}

typedef HRESULT (WINAPI *RoActivateInstance_t)(HSTRING activatableClassId, IInspectable** instance);
RoActivateInstance_t pOriginal_RoActivateInstance = nullptr;
HRESULT WINAPI Detour_RoActivateInstance(HSTRING activatableClassId, IInspectable** instance) {
    HRESULT hr = pOriginal_RoActivateInstance(activatableClassId, instance);
    if (SUCCEEDED(hr) && instance && *instance && activatableClassId) {
        PCWSTR classStr = WindowsGetStringRawBuffer(activatableClassId, nullptr);
        if (classStr) {
            std::wstring ws(classStr);
            Logger::Log("[ACTIVATE] " + WStringToString(ws));
            HookInstance(*instance, ws);
        }
    }
    return hr;
}

void HookThread() {
    wchar_t logP[MAX_PATH]; GetTempPathW(MAX_PATH, logP);
    Logger::Init(WStringToString(logP) + "breezip_analysis.log");
    Logger::Log("=== BreeZip v8.1 'THE WALL' GESTARTET ===");
    Sleep(2000);
    MH_Initialize();
    HMODULE hCombase = GetModuleHandleA("combase.dll");
    if (hCombase) {
        void* pRoAct = GetProcAddress(hCombase, "RoActivateInstance");
        SafeHook(pRoAct, &Detour_RoActivateInstance, reinterpret_cast<LPVOID*>(&pOriginal_RoActivateInstance));
    }
    Logger::Log("[ULTIMATE] Nexus-Hooks aktiv.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)HookThread, NULL, 0, NULL);
    }
    return TRUE;
}
