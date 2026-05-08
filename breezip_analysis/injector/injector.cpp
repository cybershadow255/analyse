#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>

void Log(const std::wstring& msg) { std::wcout << L"[*] " << msg << std::endl; }
void LogError(const std::wstring& msg) { std::wcerr << L"[!] " << msg << L" (Error: " << GetLastError() << L")" << std::endl; }

bool SetAppContainerPermissions(std::wstring dllPath) {
    PSECURITY_DESCRIPTOR pSD = NULL; PACL pOldDACL = NULL, pNewDACL = NULL; EXPLICIT_ACCESSW ea; PSID pSid = NULL;
    if (GetNamedSecurityInfoW(dllPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD) != ERROR_SUCCESS) return false;
    ConvertStringSidToSidW(L"S-1-15-2-1", &pSid);
    ZeroMemory(&ea, sizeof(EXPLICIT_ACCESSW));
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE; ea.grfAccessMode = SET_ACCESS; ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID; ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea.Trustee.ptstrName = (LPWSTR)pSid;
    SetEntriesInAclW(1, &ea, pOldDACL, &pNewDACL);
    SetNamedSecurityInfoW((LPWSTR)dllPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);
    if (pSid) FreeSid(pSid); if (pNewDACL) LocalFree(pNewDACL); if (pSD) LocalFree(pSD);
    return true;
}

DWORD GetProcessIdByName(std::wstring processName) {
    DWORD pid = 0; HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry; entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do { if (processName == entry.szExeFile) { pid = entry.th32ProcessID; break; } } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

void KillProcess(std::wstring processName) {
    DWORD pid = GetProcessIdByName(processName);
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); Log(L"Prozess beendet."); }
    }
}

bool Inject(DWORD pid, std::wstring dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;
    void* pLibPath = VirtualAllocEx(hProcess, NULL, (dllPath.length() + 1) * sizeof(wchar_t), MEM_COMMIT, PAGE_READWRITE);
    WriteProcessMemory(hProcess, pLibPath, dllPath.c_str(), (dllPath.length() + 1) * sizeof(wchar_t), NULL);
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW"), pLibPath, 0, NULL);
    if (hThread) { WaitForSingleObject(hThread, INFINITE); CloseHandle(hThread); }
    CloseHandle(hProcess);
    return hThread != NULL;
}

int main() {
    std::wstring processName = L"BreeZip.exe";
    std::wstring dllPath = L"C:\\temp\\breezip_hook.dll"; // Standardpfad, kann angepasst werden
    std::vector<DWORD> injectedPids;

    std::wcout << L"=== UWP Research Injector v7.1 ===" << std::endl;
    Log(L"Druecke 'K' zum Beenden von BreeZip.");

    SetAppContainerPermissions(dllPath);

    while (true) {
        if (GetAsyncKeyState('K') & 0x8000) KillProcess(processName);
        DWORD pid = GetProcessIdByName(processName);
        if (pid != 0) {
            bool already = false;
            for (DWORD p : injectedPids) if (p == pid) already = true;
            if (!already) {
                Log(L"Zielprozess erkannt. Injiziere...");
                if (Inject(pid, dllPath)) { Log(L"Injection erfolgreich!"); injectedPids.push_back(pid); }
            }
        } else { injectedPids.clear(); }
        Sleep(500);
    }
    return 0;
}
