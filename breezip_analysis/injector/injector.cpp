#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

void LogError(const std::wstring& msg) {
    DWORD err = GetLastError();
    std::wcerr << L"[!] " << msg << L" (Error Code: " << err << L")" << std::endl;
}

// Stellt sicher, dass C:\temp\ existiert
bool EnsureTempDir() {
    if (!CreateDirectoryW(L"C:\\temp\\", NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            LogError(L"Konnte C:\\temp\\ nicht erstellen.");
            return false;
        }
    }
    return true;
}

bool SetAppContainerPermissions(std::wstring dllPath) {
    PSECURITY_DESCRIPTOR pSD = NULL;
    PACL pOldDACL = NULL, pNewDACL = NULL;
    EXPLICIT_ACCESSW ea;
    PSID pSid = NULL;

    if (GetNamedSecurityInfoW(dllPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD) != ERROR_SUCCESS) {
        LogError(L"GetNamedSecurityInfo fehlgeschlagen");
        return false;
    }

    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &pSid)) {
        LogError(L"ConvertStringSidToSid fehlgeschlagen");
        LocalFree(pSD);
        return false;
    }

    ZeroMemory(&ea, sizeof(EXPLICIT_ACCESSW));
    ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)pSid;

    if (SetEntriesInAclW(1, &ea, pOldDACL, &pNewDACL) != ERROR_SUCCESS) {
        LogError(L"SetEntriesInAcl fehlgeschlagen");
        FreeSid(pSid);
        LocalFree(pSD);
        return false;
    }

    if (SetNamedSecurityInfoW((LPWSTR)dllPath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL) != ERROR_SUCCESS) {
        LogError(L"SetNamedSecurityInfo fehlgeschlagen");
    }

    if (pSid) FreeSid(pSid);
    if (pNewDACL) LocalFree(pNewDACL);
    if (pSD) LocalFree(pSD);
    return true;
}

DWORD GetProcessIdByName(std::wstring processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (processName == entry.szExeFile) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

int main() {
    std::wstring processName = L"BreeZip.exe";
    std::wstring dllPath = L"C:\\temp\\breezip_hook.dll";

    std::wcout << L"--- UWP Injector v2.0 für BreeZip ---" << std::endl;

    if (!EnsureTempDir()) return 1;

    DWORD pid = GetProcessIdByName(processName);
    if (pid == 0) {
        std::wcerr << L"[!] BreeZip.exe wurde nicht gefunden. Starte die App zuerst!" << std::endl;
        return 1;
    }
    std::wcout << L"[*] Zielprozess gefunden: PID " << pid << std::endl;

    if (!SetAppContainerPermissions(dllPath)) {
        std::wcerr << L"[!] Fehler beim Setzen der DLL-Berechtigungen." << std::endl;
        return 1;
    }

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        LogError(L"OpenProcess fehlgeschlagen. Bitte als Administrator ausführen!");
        return 1;
    }

    void* pLibPath = VirtualAllocEx(hProcess, NULL, (dllPath.length() + 1) * sizeof(wchar_t), MEM_COMMIT, PAGE_READWRITE);
    if (!pLibPath) {
        LogError(L"VirtualAllocEx fehlgeschlagen");
        return 1;
    }

    if (!WriteProcessMemory(hProcess, pLibPath, dllPath.c_str(), (dllPath.length() + 1) * sizeof(wchar_t), NULL)) {
        LogError(L"WriteProcessMemory fehlgeschlagen");
        return 1;
    }

    void* pLoadLibraryW = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryW, pLibPath, 0, NULL);

    if (!hThread) {
        LogError(L"CreateRemoteThread fehlgeschlagen");
        return 1;
    }

    std::wcout << L"[*] DLL erfolgreich injiziert!" << std::endl;
    std::wcout << L"[*] Prüfe C:\\temp\\breezip_analysis.log für Ergebnisse." << std::endl;

    CloseHandle(hThread);
    CloseHandle(hProcess);
    return 0;
}
