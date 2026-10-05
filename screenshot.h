// screenshot.h
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <shlwapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

// GDI+ token global — se inicializa UNA vez
static ULONG_PTR g_gdiplusToken = 0;

static bool GdiPlusInit() {
    if (g_gdiplusToken != 0) return true;
    Gdiplus::GdiplusStartupInput gdiInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiInput, NULL) != Gdiplus::Ok)
        return false;
    return true;
}

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto* info = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    Gdiplus::GetImageEncoders(num, size, info);
    int found = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, format) == 0) {
            *pClsid = info[i].Clsid;
            found = (int)i; break;
        }
    }
    free(info);
    return found;
}

static bool CaptureScreenshotJpeg(std::vector<BYTE>& out, int quality = 60) {
    if (!GdiPlusInit()) return false;

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hMem    = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, w, h);
    HGDIOBJ old = SelectObject(hMem, hBmp);

    BitBlt(hMem, 0, 0, w, h, hScreen, x, y, SRCCOPY);
    SelectObject(hMem, old);
    ReleaseDC(NULL, hScreen);
    DeleteDC(hMem);

    Gdiplus::Bitmap bmp(hBmp, NULL);

    CLSID clsid;
    if (GetEncoderClsid(L"image/jpeg", &clsid) < 0) {
        DeleteObject(hBmp);
        return false;
    }

    IStream* stream = NULL;
    if (CreateStreamOnHGlobal(NULL, TRUE, &stream) != S_OK) {
        DeleteObject(hBmp);
        return false;
    }

    ULONG q = (ULONG)quality;
    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid           = Gdiplus::EncoderQuality;
    params.Parameter[0].Type           = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value          = &q;

    bmp.Save(stream, &clsid, &params);

    STATSTG stat;
    stream->Stat(&stat, STATFLAG_NONAME);
    ULARGE_INTEGER sz = stat.cbSize;
    out.resize((size_t)sz.QuadPart);
    LARGE_INTEGER zero = {0};
    stream->Seek(zero, STREAM_SEEK_SET, NULL);
    ULONG read = 0;
    stream->Read(out.data(), (ULONG)out.size(), &read);
    stream->Release();

    DeleteObject(hBmp);
    return true;
}