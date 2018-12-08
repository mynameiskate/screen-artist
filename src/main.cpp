#include <windows.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shlobj.h>

#include "main.h"
#include "app.h"
#include "helper.h"
#include "resource.h"

STATIC_DATA config;
ULONG_PTR gdiplusToken;

std::vector<IMAGE_FORMAT> formats;

app app(APP_NAME, APP_NAME_SHORT);
void ShowScreenshot(HBITMAP hBitmap, INT width, INT height);
void BlitToHdc(HDC hdcDst, HBITMAP hbmSrc, int x, int y, int wd, int hgt);
rstring CreateFileName(LPCWSTR directory, EnumImageName name_type);
HBITMAP hCurrentBmp = nullptr;
RECT rPreviewField = { 0, 0, 0, 0 };
BOOL isDrawing = false;
POINT ptStartPos = { 0, 0 };
POINT ptEndPos = { 0, 0 };
COLORREF clSelectedColor = 0x000000;
HWND hTrackBar = nullptr;
LPCWSTR bmpFilePath = nullptr;

size_t GetImageFormat ()
{
    if (formats.empty ())
        return 0;

    const INT size = (INT)formats.size ();
    const INT current = app.ConfigGet (L"ImageFormat", FormatPng).AsInt ();

    return size_t (max (min (current, size - 1), 0));
}



rstring CreateFileName(LPCWSTR directory, EnumImageName name_type)
{
    const rstring fext = formats.at(GetImageFormat()).ext;

    WCHAR result[MAX_PATH] = { 0 };
    const rstring name_prefix = app.ConfigGet(L"FilenamePrefix", FILE_FORMAT_NAME_PREFIX);

    if (name_type == NameDate)
    {
        WCHAR date_format[MAX_PATH] = { 0 };
        WCHAR time_format[MAX_PATH] = { 0 };

        SYSTEMTIME st = { 0 };
        GetLocalTime(&st);

        if (
            GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, &st, FILE_FORMAT_DATE_FORMAT_1, date_format, _countof(date_format)) &&
            GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, &st, FILE_FORMAT_DATE_FORMAT_2, time_format, _countof(time_format))
            )
        {
            StringCchPrintf(result, _countof(result), L"%s\\%s%s-%s.%s", directory, name_prefix.GetString(), date_format, time_format, fext.GetString());

            if (!_r_fs_exists(result))
                return result;
        }

        if (PathYetAnotherMakeUniqueName(result, _r_fmt(L"%s\\%s%s-%s.%s", directory, name_prefix.GetString(), date_format, time_format, fext.GetString()), nullptr, _r_fmt(L"%s%s %s.%s", name_prefix.GetString(), date_format, time_format, fext.GetString())))
            return result;
    }
    else
    {
        static const USHORT idx = START_IDX;

        for (USHORT i = idx; i < USHRT_MAX; i++)
        {
            StringCchPrintf(result, _countof(result), L"%s\\" FILE_FORMAT_NAME_FORMAT L".%s", directory, name_prefix.GetString(), i, fext.GetString());

            if (!_r_fs_exists(result))
                return result;
        }

        if (PathYetAnotherMakeUniqueName(result, _r_fmt(L"%s\\%s.%s", directory, name_prefix.GetString(), fext.GetString()), nullptr, _r_fmt(L"%s.%s", name_prefix.GetString(), fext.GetString())))
            return result;
    }

    return result;
}

rstring GetDirectory ()
{
    rstring result = _r_path_expand (app.ConfigGet (L"Folder", config.default_folder));

    if (!_r_fs_exists (result))
        result = _r_path_expand (config.default_folder);

    return result;
}

bool SaveBitmap (HBITMAP hbitmap, LPCWSTR filepath)
{
    bool result = false;
    Gdiplus::Bitmap *pScreenShot = new Gdiplus::Bitmap (hbitmap, nullptr);

    if (pScreenShot)
    {
        CLSID* imageCLSID = &formats.at (GetImageFormat ()).clsid;
        ULONG uQuality = app.ConfigGet (L"JPEGQuality", JPEG_QUALITY).AsUlong ();

        Gdiplus::EncoderParameters encoderParams = {0};

        encoderParams.Count = 1;
        encoderParams.Parameter[0].NumberOfValues = 1;
        encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
        encoderParams.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].Value = &uQuality;

        result = (pScreenShot->Save (filepath, imageCLSID, &encoderParams) == Gdiplus::Ok);

        delete pScreenShot;
    }

    return result;
}

HBITMAP CreateBitmap(HDC hdc, LONG width, LONG height)
{
    BITMAPINFO bmi = {0};

    bmi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biSizeImage = (width * height) * 4;

    return CreateDIBSection (hdc, &bmi, DIB_RGB_COLORS, nullptr, nullptr, 0);
}

void SaveBitmap(HBITMAP hbitmap, INT width, INT height)
{
    HBITMAP hbitmap_copy = (HBITMAP)CopyImage(hbitmap, IMAGE_BITMAP, width, height, 0);
    ShowScreenshot(hbitmap, width, height);

    if (app.ConfigGet (L"CopyToClipboard", false).AsBool ())
    {
        if (OpenClipboard (app.GetHWND ()))
        {
            if (EmptyClipboard ())
            {
                SetClipboardData (CF_BITMAP, hbitmap_copy);
            }

            CloseClipboard ();
        }
    }

    WCHAR full_path[MAX_PATH] = {0};
    StringCchCopy (full_path, _countof (full_path), CreateFileName (GetDirectory (), (EnumImageName)app.ConfigGet (L"FilenameType", NameIndex).AsUint ()));
    SaveBitmap(hbitmap, full_path);
}

void CreateScreenshot(INT x, INT y, INT width, INT height, bool is_cursor)
{
    const HDC hdc = GetDC(nullptr);
    const HDC hcapture = CreateCompatibleDC(hdc);
    const HBITMAP hbitmap = CreateBitmap(hdc, width, height);

    if (hbitmap)
    {
        SelectObject (hcapture, hbitmap);
        BitBlt (hcapture, 0, 0, width, height, hdc, x, y, SRCCOPY);

        if (is_cursor)
        {
            CURSORINFO cursorinfo = { 0 };
            cursorinfo.cbSize = sizeof(cursorinfo);

            if (GetCursorInfo(&cursorinfo))
            {
                const HICON hicon = CopyIcon(cursorinfo.hCursor);

                if (hicon)
                {
                    ICONINFO iconinfo = { 0 };
                    GetIconInfo(hicon, &iconinfo);

                    DrawIcon(hcapture, cursorinfo.ptScreenPos.x - iconinfo.xHotspot - x, cursorinfo.ptScreenPos.y - iconinfo.yHotspot - y, hicon);

                    DestroyIcon(hicon);
                }
            }
        }

        SaveBitmap (hbitmap, width, height);

        DeleteObject (hbitmap);
    }

    DeleteDC (hcapture);
    ReleaseDC (nullptr, hdc);
}

bool IsNormalWindow (HWND hwnd)
{
    return hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd) && !IsIconic(hwnd) && hwnd != GetShellWindow() && hwnd != GetDesktopWindow();
}

bool IsMenu(HWND hwnd)
{
    WCHAR class_name[MAX_PATH] = {0};

    if (GetClassName(hwnd, class_name, _countof (class_name)) && class_name[0])
        return _wcsnicmp (class_name, L"#32768", wcslen (class_name)) == 0;

    return false;
}

BOOL CALLBACK FindTopWindow (HWND hwnd, LPARAM lparam)
{
    HWND* lpresult = (HWND*)lparam;

    if (!IsNormalWindow (hwnd) || // exclude shell windows
        ((GetWindowLongPtr (hwnd, GWL_STYLE) & WS_SYSMENU) == 0) // exclude controls
        )
    {
        return TRUE;
    }

    *lpresult = hwnd;

    return FALSE;
}

INT GetShadowSize (HWND hwnd)
{
    if (IsZoomed (hwnd))
        return 0;
    RECT rc = {0};
    AdjustWindowRectEx(&rc, WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, FALSE, WS_EX_WINDOWEDGE);

    return _R_RECT_WIDTH(&rc);
}

void PlaySound ()
{
    if (app.ConfigGet (L"IsPlaySound", true).AsBool ())
        PlaySound (MAKEINTRESOURCE (IDW_MAIN), app.GetHINSTANCE (), SND_RESOURCE | SND_ASYNC);
}

void TakeShot(HWND hwnd, EnumScreenshot mode)
{
    const HWND myWindow = app.GetHWND();

    bool result = false;
    const bool is_includecursor = app.ConfigGet(L"IsIncludeMouseCursor", false).AsBool();
    const bool is_windowdisplayed = IsWindowVisible(myWindow) && !IsIconic(myWindow);

    RECT prev_rect = {0};

    if (is_windowdisplayed)
    {
        GetWindowRect(myWindow, &prev_rect);
        SetWindowPos(myWindow, nullptr, -GetSystemMetrics(SM_CXVIRTUALSCREEN), -GetSystemMetrics(SM_CYVIRTUALSCREEN), 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOCOPYBITS | SWP_DEFERERASE | SWP_NOSENDCHANGING);
    }

    if (mode == ScreenshotFullscreen)
    {
        POINT pt = {0};
        GetCursorPos(&pt);

        const HMONITOR hmonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

        MONITORINFO monitorInfo = {0};
        monitorInfo.cbSize = sizeof (monitorInfo);

        if (GetMonitorInfo (hmonitor, &monitorInfo))
        {
            const LPRECT lprc = &monitorInfo.rcMonitor;

            PlaySound();
            CreateScreenshot(lprc->left, lprc->top, _R_RECT_WIDTH(lprc), _R_RECT_HEIGHT(lprc), is_includecursor);
        }
    }
    else if (mode == ScreenshotWindow)
    {
        if (!hwnd || !IsNormalWindow (hwnd))
        {
            POINT pt = {0};
            GetCursorPos (&pt);

            hwnd = GetAncestor(WindowFromPoint(pt), GA_ROOT);
        }

        if (IsNormalWindow (hwnd))
        {
            HWND hdummy = nullptr;

            const bool is_menu = IsMenu(hwnd);
            const bool is_includewindowshadow = app.ConfigGet (L"IsIncludeWindowShadow", true).AsBool ();
            const bool is_clearbackground = app.ConfigGet (L"IsClearBackground", true).AsBool ();

            RECT window_rect = {0};
            GetWindowRect(hwnd, &window_rect);

            // calculate shadow padding
            if (is_includewindowshadow)
            {
                const INT shadow_size = GetShadowSize(hwnd);

                window_rect.left -= shadow_size;
                window_rect.right += shadow_size;

                window_rect.top -= shadow_size;
                window_rect.bottom += shadow_size;
            }

            if (is_clearbackground)
            {
                hdummy = CreateWindowEx(0, DUMMY_CLASS_DLG, APP_NAME, WS_POPUP | WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, app.GetHINSTANCE (), nullptr);

                if (hdummy)
                {
                    SetWindowPos(hdummy, hwnd, window_rect.left, window_rect.top, _R_RECT_WIDTH(&window_rect), _R_RECT_HEIGHT(&window_rect), SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
                }
            }

            PlaySound();
            CreateScreenshot(window_rect.left, window_rect.top, _R_RECT_WIDTH (&window_rect), _R_RECT_HEIGHT (&window_rect), is_includecursor);

            if (is_clearbackground && hdummy)
            {
                DestroyWindow(hdummy);
            }
        }
    }
    else if (mode == ScreenshotRegion)
    {
        if (WaitForSingleObjectEx(config.hregion_mutex, 0, FALSE) == WAIT_OBJECT_0)
        {
            config.hregion = CreateWindowEx(WS_EX_TOPMOST, REGION_CLASS_DLG, APP_NAME, WS_POPUP | WS_OVERLAPPED, 0, 0, 0, 0, myWindow, nullptr, app.GetHINSTANCE(), nullptr);

            if (config.hregion)
            {
                SetWindowPos(config.hregion, HWND_TOPMOST, 0, 0, GetSystemMetrics (SM_CXVIRTUALSCREEN), GetSystemMetrics (SM_CYVIRTUALSCREEN), SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSENDCHANGING);
                WaitForSingleObjectEx(config.hregion, INFINITE, FALSE);
            }
            else
            {
                SetEvent(config.hregion_mutex);
            }
        }
    }

    if (is_windowdisplayed)
    {
        SetWindowPos(
            myWindow,
            nullptr,
            prev_rect.left,
            prev_rect.top,
            _R_RECT_WIDTH(&prev_rect),
            _R_RECT_HEIGHT(&prev_rect),
            SWP_SHOWWINDOW
            | SWP_NOACTIVATE
            | SWP_FRAMECHANGED
            | SWP_NOZORDER
            | SWP_NOCOPYBITS
            | SWP_NOOWNERZORDER
            | SWP_NOSENDCHANGING);
    }
}

rstring KeyToString (UINT key)
{
    rstring result;

    const UINT vk_code = LOBYTE (key);
    const UINT modifiers = HIBYTE (key);

    if ((modifiers & HOTKEYF_CONTROL) != 0)
        result.Append (L"Ctrl+");

    if ((modifiers & HOTKEYF_ALT) != 0)
        result.Append (L"Alt+");

    if ((modifiers & HOTKEYF_SHIFT) != 0)
        result.Append (L"Shift+");

    WCHAR name[64] = {0};

    if (vk_code == VK_SNAPSHOT)
    {
        StringCchCopy (name, _countof (name), L"Prnt scrn");
    }
    else
    {
        const UINT scan_code = MapVirtualKey (vk_code, MAPVK_VK_TO_VSC);
        GetKeyNameText ((scan_code << 16), name, _countof (name));
    }

    return result.Append (name);
}

HPAINTBUFFER BeginBufferedPaint(HDC hdc, LPRECT lprect, HDC* lphdc)
{
    *lphdc = nullptr;

    BP_PAINTPARAMS bpp = {0};

    bpp.cbSize = sizeof (bpp);
    bpp.dwFlags = BPPF_NOCLIP;

    const HINSTANCE hlib = GetModuleHandle (L"uxtheme.dll");

    if (hlib)
    {
        typedef HPAINTBUFFER (WINAPI *BBP) (HDC, const LPRECT, BP_BUFFERFORMAT, PBP_PAINTPARAMS, HDC *); // BeginBufferedPaint
        const BBP _BeginBufferedPaint = (BBP)GetProcAddress (hlib, "BeginBufferedPaint");

        if (_BeginBufferedPaint)
        {
            HPAINTBUFFER hdpaint = _BeginBufferedPaint (hdc, lprect, BPBF_TOPDOWNDIB, &bpp, lphdc);

            if (hdpaint)
                return hdpaint;
        }
    }

    *lphdc = hdc;

    return nullptr;
}

VOID EndBufferedPaint(HPAINTBUFFER hpbuff)
{
    const HINSTANCE hlib = GetModuleHandle (L"uxtheme.dll");

    if (hlib)
    {
        typedef HRESULT (WINAPI *EBP) (HPAINTBUFFER, BOOL); // EndBufferedPaint
        const EBP _EndBufferedPaint = (EBP)GetProcAddress (hlib, "EndBufferedPaint");

        if (_EndBufferedPaint)
            _EndBufferedPaint (hpbuff, TRUE);
    }
}

LRESULT CALLBACK RegionProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static bool fDraw = false;

    static HDC hcapture = nullptr, hcapture_mask = nullptr;
    static HBITMAP hbitmap = nullptr, hbitmap_mask = nullptr;
    static HPEN hpen = nullptr, hpen_draw = nullptr;
    static RECT wndRect = {0};
    static POINT ptStart = {0}, ptEnd = {0};

    switch (msg)
    {
        case WM_CREATE:
        {
            ResetEvent (config.hregion_mutex);

            wndRect.left = wndRect.top = 0;
            wndRect.right = GetSystemMetrics (SM_CXVIRTUALSCREEN);
            wndRect.bottom = GetSystemMetrics (SM_CYVIRTUALSCREEN);

            const HDC hdc = GetDC (nullptr);

            if (hdc)
            {
                hpen = CreatePen (PS_SOLID, app.GetDPI (REGION_PEN_SIZE), REGION_PEN_COLOR);
                hpen_draw = CreatePen (PS_SOLID, app.GetDPI (REGION_PEN_SIZE), REGION_PEN_DRAW_COLOR);

                hcapture = CreateCompatibleDC (hdc);
                hcapture_mask = CreateCompatibleDC (hdc);

                hbitmap = CreateBitmap (hdc, _R_RECT_WIDTH (&wndRect), _R_RECT_HEIGHT (&wndRect));
                hbitmap_mask = CreateBitmap (hdc, _R_RECT_WIDTH (&wndRect), _R_RECT_HEIGHT (&wndRect));

                if (hbitmap && hcapture)
                {
                    SelectObject (hcapture, hbitmap);
                    BitBlt (hcapture, wndRect.left, wndRect.top, _R_RECT_WIDTH (&wndRect), _R_RECT_HEIGHT (&wndRect), hdc, wndRect.left, wndRect.top, SRCCOPY);
                }

                if (hbitmap_mask && hcapture_mask)
                {
                    SelectObject (hcapture_mask, hbitmap_mask);
                    BitBlt (hcapture_mask, wndRect.left, wndRect.top, _R_RECT_WIDTH (&wndRect), _R_RECT_HEIGHT (&wndRect), hcapture, wndRect.left, wndRect.top, SRCCOPY);

                    const LONG imageBytes = _R_RECT_WIDTH (&wndRect) * _R_RECT_HEIGHT (&wndRect) * 4;
                    static const double blend = 1.5;

                    UINT* bmpBuffer = (UINT*) new BYTE[imageBytes];
                    GetBitmapBits (hbitmap_mask, imageBytes, bmpBuffer);

                    int R, G, B;
                    for (int i = 0; i < (_R_RECT_WIDTH (&wndRect) * _R_RECT_HEIGHT (&wndRect)); i++)
                    {
                        R = (int) (GetRValue(bmpBuffer[i]) / blend);
                        G = (int) (GetGValue(bmpBuffer[i]) / blend);
                        B = (int) (GetBValue(bmpBuffer[i]) / blend);

                        bmpBuffer[i] = RGB (R, G, B);
                    }

                    SetBitmapBits (hbitmap_mask, imageBytes, bmpBuffer);

                    delete[] bmpBuffer;
                }

                ReleaseDC (nullptr, hdc);
            }

            ShowWindow (hwnd, SW_SHOWNA);

            break;
        }

        case WM_DESTROY:
        {
            SetEvent (config.hregion_mutex);

            fDraw = false;

            if (hpen)
            {
                DeleteObject (hpen);
                hpen = nullptr;
            }

            if (hpen_draw)
            {
                DeleteObject (hpen_draw);
                hpen_draw = nullptr;
            }

            if (hbitmap)
            {
                DeleteObject (hbitmap);
                hbitmap = nullptr;
            }

            if (hbitmap_mask)
            {
                DeleteObject (hbitmap_mask);
                hbitmap_mask = nullptr;
            }

            if (hcapture)
            {
                DeleteDC (hcapture);
                hcapture = nullptr;
            }

            if (hcapture_mask)
            {
                DeleteDC (hcapture_mask);
                hcapture_mask = nullptr;
            }

            return TRUE;
        }

        case WM_LBUTTONDOWN:
        {
            if (!fDraw)
            {
                fDraw = true;

                ptStart.x = LOWORD (lparam);
                ptStart.y = HIWORD (lparam);

                InvalidateRect (hwnd, nullptr, TRUE);
            }
            else
            {
                fDraw = false;

                ptEnd.x = LOWORD (lparam);
                ptEnd.y = HIWORD (lparam);

                const INT x = min (ptStart.x, ptEnd.x);
                const INT y = min (ptStart.y, ptEnd.y);
                const INT width = max (ptStart.x, ptEnd.x) - x;
                const INT height = max (ptStart.y, ptEnd.y) - y;

                if (width && height)
                {
                    // save region to a file
                    const HDC hdc = GetDC (nullptr);

                    if (hdc)
                    {
                        const HDC hcapture_finish = CreateCompatibleDC (hdc);

                        if (hcapture_finish)
                        {
                            const HBITMAP hbitmap_finish = CreateBitmap (hdc, width, height);

                            if (hbitmap_finish)
                            {
                                SelectObject (hcapture_finish, hbitmap_finish);
                                BitBlt (hcapture_finish, 0, 0, width, height, hcapture, x, y, SRCCOPY);

                                PlaySound ();
                                SaveBitmap (hbitmap_finish, width, height);

                                DeleteObject (hbitmap_finish);
                            }

                            DeleteDC (hcapture_finish);
                        }

                        ReleaseDC (nullptr, hdc);
                    }

                    DestroyWindow (hwnd);
                }
            }

            return TRUE;
        }

        case WM_MBUTTONDOWN:
        {
            if (fDraw)
            {
                fDraw = false;
                ptStart.x = ptStart.y = ptEnd.x = ptEnd.y = 0;

                InvalidateRect (hwnd, nullptr, TRUE);
            }
            else
            {
                DestroyWindow (hwnd);
            }

            return TRUE;
        }

        case WM_MOUSEMOVE:
        {
            InvalidateRect (hwnd, nullptr, TRUE);
            return TRUE;
        }

        case WM_PAINT:
        {
            return TRUE;
        }

        case WM_ERASEBKGND:
        {
            const HDC hdc = (HDC)wparam;
            HDC hdc_buffered = nullptr;

            POINT pt = {0};
            GetCursorPos (&pt);

            const HPAINTBUFFER hdpaint = BeginBufferedPaint (hdc, &wndRect, &hdc_buffered);

            BitBlt (hdc_buffered, wndRect.left, wndRect.top, _R_RECT_WIDTH (&wndRect), _R_RECT_HEIGHT (&wndRect), hcapture_mask, wndRect.left, wndRect.top, SRCCOPY);

            const HGDIOBJ old_pen = SelectObject (hdc_buffered, fDraw ? hpen_draw : hpen);
            const HGDIOBJ old_brush = SelectObject (hdc_buffered, GetStockObject (NULL_BRUSH));

            if (fDraw && (pt.x != ptStart.x) && (pt.y != ptStart.y))
            {
                // draw region rectangle
                RECT rectTarget = {0};

                rectTarget.left = min (ptStart.x, pt.x);
                rectTarget.top = min (ptStart.y, pt.y);
                rectTarget.right = max (ptStart.x, pt.x);
                rectTarget.bottom = max (ptStart.y, pt.y);

                BitBlt (hdc_buffered, rectTarget.left, rectTarget.top, _R_RECT_WIDTH (&rectTarget), _R_RECT_HEIGHT (&rectTarget), hcapture, rectTarget.left, rectTarget.top, SRCCOPY);
                Rectangle (hdc_buffered, ptStart.x, ptStart.y, pt.x, pt.y);
            }
            else
            {
                // draw cursor crosshair
                MoveToEx (hdc_buffered, pt.x, wndRect.top, nullptr);
                LineTo (hdc_buffered, pt.x, _R_RECT_HEIGHT (&wndRect));

                MoveToEx (hdc_buffered, wndRect.left, pt.y, nullptr);
                LineTo (hdc_buffered, _R_RECT_WIDTH (&wndRect), pt.y);
            }

            SelectObject (hdc_buffered, old_brush);
            SelectObject (hdc_buffered, old_pen);

            if (hdpaint)
                EndBufferedPaint (hdpaint);

            return TRUE;
        }

        case WM_KEYDOWN:
        {
            switch (wparam)
            {
                case VK_ESCAPE:
                {
                    if (fDraw)
                    {
                        fDraw = false;
                        ptStart.x = ptStart.y = ptEnd.x = ptEnd.y = 0;

                        InvalidateRect (hwnd, nullptr, true);
                    }
                    else
                    {
                        DestroyWindow (hwnd);
                    }

                    return TRUE;
                }
            }

            break;
        }
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void ChooseScreenshotPenColor()
{
    CHOOSECOLOR color;
    COLORREF ccref[16];

    memset(&color, 0, sizeof(color));
    color.lStructSize = sizeof(CHOOSECOLOR);
    color.hwndOwner = app.GetHWND();
    color.lpCustColors = ccref;
    color.rgbResult = clSelectedColor;
    color.Flags = CC_RGBINIT;

    if (ChooseColor(&color))
    {
        clSelectedColor = color.rgbResult;
    }
}

void ShowScreenshot(HBITMAP hBitmap, INT width, INT height)
{
    if (width > PREVIEW_MAX_WIDTH)
    {
        height = min(PREVIEW_MAX_HEIGHT, (INT)floor(height * width / PREVIEW_MAX_WIDTH));
        width = PREVIEW_MAX_WIDTH;
    }
    else if (height > PREVIEW_MAX_HEIGHT)
    {
        width = min(PREVIEW_MAX_WIDTH, (INT)floor(width * height / PREVIEW_MAX_HEIGHT));
        height = PREVIEW_MAX_HEIGHT;
    }

    hCurrentBmp = (HBITMAP)CopyImage(hBitmap, IMAGE_BITMAP, width, height, 0);
    HWND hPic = GetDlgItem(app.GetHWND(), IDC_PREVIEW_FIELD);

    rPreviewField.right = rPreviewField.left + width;
    rPreviewField.bottom = rPreviewField.top + height;
    
    SendMessage(hPic, STM_SETIMAGE, IMAGE_BITMAP, LPARAM(hCurrentBmp));
}

void BlitToHdc(HDC hdcDst, HBITMAP hbmSrc, int x, int y, int wd, int hgt) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcSrc = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcSrc, hbmSrc));
    BitBlt(hdcDst, x, y, wd, hgt, hdcSrc, 0, 0, SRCCOPY);

    SelectObject(hdcSrc, hbmOld);

    DeleteDC(hdcSrc);
    ReleaseDC(NULL, hdcScreen);
}

void _app_initdropdownmenu (HMENU hmenu, bool is_button)
{
    app.LocaleMenu (hmenu, IDS_COPYTOCLIPBOARD_CHK, IDM_COPYTOCLIPBOARD_CHK, false, nullptr);
    app.LocaleMenu (hmenu, IDS_PLAYSOUNDS_CHK, IDM_PLAYSOUNDS_CHK, false, nullptr);
    app.LocaleMenu (hmenu, IDS_INCLUDEMOUSECURSOR_CHK, IDM_INCLUDEMOUSECURSOR_CHK, false, nullptr);
    app.LocaleMenu (hmenu, IDS_INCLUDEWINDOWSHADOW_CHK, IDM_INCLUDEWINDOWSHADOW_CHK, false, nullptr);
    app.LocaleMenu (hmenu, IDS_CLEARBACKGROUND_CHK, IDM_CLEARBACKGROUND_CHK, false, nullptr);
    app.LocaleMenu (hmenu, IDS_FILENAME, FILENAME_MENU, true, nullptr);
    app.LocaleMenu (hmenu, IDS_IMAGEFORMAT, FORMAT_MENU, true, nullptr);

    app.LocaleMenu (hmenu, 0, IDM_FILENAME_INDEX, false, _r_fmt (FILE_FORMAT_NAME_FORMAT L".%s", app.ConfigGet (L"FilenamePrefix", FILE_FORMAT_NAME_PREFIX).GetString (), START_IDX, formats.at (GetImageFormat ()).ext));
    app.LocaleMenu (hmenu, 0, IDM_FILENAME_DATE, false, _r_fmt (L"%s" FILE_FORMAT_DATE_FORMAT_1 L"-" FILE_FORMAT_DATE_FORMAT_2 L".%s", app.ConfigGet (L"FilenamePrefix", FILE_FORMAT_NAME_PREFIX).GetString (), formats.at (GetImageFormat ()).ext));

    // initialize formats
    {
        const HMENU submenu = GetSubMenu (hmenu, FORMAT_MENU);
        DeleteMenu (submenu, 0, MF_BYPOSITION);

        for (size_t i = 0; i < formats.size (); i++)
            AppendMenu (submenu, MF_BYPOSITION, IDX_FORMATS + i, formats.at (i).ext);
    }

    CheckMenuItem (hmenu, IDM_COPYTOCLIPBOARD_CHK, MF_BYCOMMAND | (app.ConfigGet (L"CopyToClipboard", false).AsBool () ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem (hmenu, IDM_INCLUDEMOUSECURSOR_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsIncludeMouseCursor", false).AsBool () ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem (hmenu, IDM_PLAYSOUNDS_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsPlaySound", true).AsBool () ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem (hmenu, IDM_INCLUDEWINDOWSHADOW_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsIncludeWindowShadow", true).AsBool () ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem (hmenu, IDM_CLEARBACKGROUND_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsClearBackground", true).AsBool () ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuRadioItem (hmenu, IDM_FILENAME_INDEX, IDM_FILENAME_DATE, IDM_FILENAME_INDEX + app.ConfigGet (L"FilenameType", NameIndex).AsUint (), MF_BYCOMMAND);
    CheckMenuRadioItem (hmenu, IDX_FORMATS, IDX_FORMATS + UINT (formats.size ()), IDX_FORMATS + UINT (GetImageFormat ()), MF_BYCOMMAND);
}

VOID WINAPI CreateTrackbar(HWND hwndDlg, UINT iMin, UINT iMax)
{
    InitCommonControls();

    hTrackBar = CreateWindowEx(
        0,
        TRACKBAR_CLASS,
        L"Set pen width",
        WS_CHILD |
        WS_VISIBLE |
        TBS_AUTOTICKS |
        TBS_ENABLESELRANGE,
        708, 30,
        280, 20,
        hwndDlg,
        (HMENU)IDC_TRACKBAR,
        app.GetHINSTANCE(),
        NULL
    );

    SendMessage(hTrackBar, TBM_SETRANGE,
        (WPARAM)TRUE,
        (LPARAM)MAKELONG(iMin, iMax));

    SendMessage(hTrackBar, TBM_SETPAGESIZE,
        0, (LPARAM)4);

    SetFocus(hTrackBar);
}

VOID InitPreviewField(HWND hwnd)
{
    HWND hPreviewField = GetDlgItem(hwnd, IDC_PREVIEW_FIELD);
    GetWindowRect(hPreviewField, &rPreviewField);
    MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&rPreviewField, 2);
}

void DrawLine(HDC hdc, POINT ptStart, POINT ptEnd, RECT rDrawRect)
{
    POINT pt;
    DWORD dwPos = (DWORD)SendMessage(hTrackBar, TBM_GETPOS, 0, 0);

    HPEN hPen = CreatePen(PS_DOT, dwPos, clSelectedColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    MoveToEx(hdc, ptStart.x - rDrawRect.left, ptStart.y - rDrawRect.top, &pt);
    LineTo(hdc, ptEnd.x - rDrawRect.left, ptEnd.y - rDrawRect.top);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

VOID DrawToBitmap(POINT ptPos)
{
    if (!hCurrentBmp)
    {
        return;
    }

    BITMAP bm;
    GetObject(hCurrentBmp, sizeof(bm), &bm);

    HDC hdc = GetDC(nullptr);
    HDC hdcCapture = CreateCompatibleDC(hdc);

    HWND hPic = GetDlgItem(app.GetHWND(), IDC_PREVIEW_FIELD);
    HDC hdcDrawCapture = GetDC(hPic);

    HBITMAP hDrawDib = CreateBitmap(hdcDrawCapture, bm.bmWidth, bm.bmHeight);
    HBITMAP hDrawBmp = (HBITMAP)SelectObject(hdcDrawCapture, hDrawDib);

    HBITMAP hCaptureBmp = (HBITMAP)SelectObject(hdcCapture, hCurrentBmp);

    BitBlt(hdcDrawCapture, 0, 0, bm.bmWidth, bm.bmHeight, hdcCapture, 0, 0, SRCCOPY);
    DrawLine(hdcDrawCapture, ptStartPos, ptPos, rPreviewField);
    BitBlt(hdcCapture, 0, 0, bm.bmWidth, bm.bmHeight, hdcDrawCapture, 0, 0, SRCCOPY);

    SelectObject(hdcCapture, hCaptureBmp);
    DeleteDC(hdcCapture);
    SelectObject(hdcDrawCapture, hDrawBmp);
    DeleteObject(hDrawDib);
}

BOOL IsMouseOverPreviewField(POINT ptMousePos)
{
    return ((ptMousePos.x < rPreviewField.right)
        && (ptMousePos.x > rPreviewField.left)
        && (ptMousePos.y < rPreviewField.bottom)
        && (ptMousePos.y > rPreviewField.top));
}


INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    POINT ptCurPos = { 0, 0 };
    static RECT wndRect = { 0 };

    switch (msg)
    {
        case WM_INITDIALOG:
        {
            // create dummy window
            {
                WNDCLASSEX wcex = {0};

                wcex.cbSize = sizeof (wcex);
                wcex.style = CS_VREDRAW | CS_HREDRAW;
                wcex.hInstance = app.GetHINSTANCE ();

                // dummy class
                wcex.lpszClassName = DUMMY_CLASS_DLG;
                wcex.lpfnWndProc = &DefWindowProc;
                wcex.hbrBackground = GetSysColorBrush (COLOR_WINDOW);
                wcex.hCursor = LoadCursor (nullptr, IDC_ARROW);

                RegisterClassEx (&wcex);

                // region class
                wcex.lpszClassName = REGION_CLASS_DLG;
                wcex.lpfnWndProc = &RegionProc;
                wcex.hCursor = LoadCursor (app.GetHINSTANCE (), MAKEINTRESOURCE (IDI_MAIN));
                CreateTrackbar(hwnd, 1, 50);

                RegisterClassEx (&wcex);
            }

            SendDlgItemMessage (hwnd, IDC_FOLDER, EM_SETLIMITTEXT, MAX_PATH, 0);

            HWND hPic = GetDlgItem(hwnd, IDC_PREVIEW_FIELD);

            // set default folder
            {
                if (SHGetFolderPath (hwnd, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT | SHGFP_TYPE_DEFAULT, config.default_folder) != S_OK &&
                    SHGetFolderPath (hwnd, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT | SHGFP_TYPE_DEFAULT, config.default_folder) != S_OK)
                {
                    StringCchCopy (config.default_folder, _countof (config.default_folder), DEFAULT_DIRECTORY);
                }

                app.ConfigSet (L"Folder", _r_path_unexpand (GetDirectory ()));
            }

            wndRect.left = wndRect.top = 0;
            wndRect.right = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            wndRect.bottom = GetSystemMetrics(SM_CYVIRTUALSCREEN);

            CoInitialize (nullptr);
            SHAutoComplete (GetDlgItem (hwnd, IDC_FOLDER), SHACF_FILESYS_ONLY | SHACF_FILESYS_DIRS | SHACF_AUTOSUGGEST_FORCE_ON | SHACF_USETAB);

            // add splitbutton style (vista+)
            _r_wnd_addstyle(hwnd, IDC_SETTINGS, BS_SPLITBUTTON, BS_SPLITBUTTON, GWL_STYLE);
                
            config.hregion_mutex = CreateEvent (nullptr, TRUE, TRUE, nullptr);

            // initialize gdi+
            Gdiplus::GdiplusStartupInput gdiplusStartupInput;
            Gdiplus::GdiplusStartup (&gdiplusToken, &gdiplusStartupInput, nullptr);

            // initialize formats
            {
                static LPCWSTR szexif[] = {
                    L"image/bmp",
                    L"image/jpeg",
                    L"image/png",
                    L"image/gif",
                    L"image/tiff"
                };

                static LPCWSTR szext[] = {
                    L"bmp",
                    L"jpg",
                    L"png",
                    L"gif",
                    L"tiff"
                };

                for (size_t i = 0; i < _countof (szexif); i++)
                {
                    IMAGE_FORMAT tagImage = {0};

                    if (r_get_encoder_clsid (szexif[i], &tagImage.clsid))
                    {
                        StringCchCopy (tagImage.ext, _countof (tagImage.ext), szext[i]);

                        formats.push_back (tagImage);
                    }
                }
            }

            InitPreviewField(hwnd);

            break;
        }

        case RM_INITIALIZE:
        {
            SetDlgItemText (hwnd, IDC_FOLDER, GetDirectory ());

            CheckDlgButton (hwnd, IDC_INCLUDEMOUSECURSOR_CHK, app.ConfigGet (L"IsIncludeMouseCursor", false).AsBool () ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton (hwnd, IDC_INCLUDEWINDOWSHADOW_CHK, app.ConfigGet (L"IsIncludeWindowShadow", true).AsBool () ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton (hwnd, IDC_CLEARBACKGROUND_CHK, app.ConfigGet (L"IsClearBackground", true).AsBool () ? BST_CHECKED : BST_UNCHECKED);

            CheckRadioButton (hwnd, IDC_MODE_FULLSCREEN, IDC_MODE_REGION, IDC_MODE_FULLSCREEN + app.ConfigGet (L"Mode", 0).AsUint ());

            // configure menu
            CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (app.ConfigGet (L"AlwaysOnTop", false).AsBool () ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem (GetMenu (hwnd), IDM_LOADONSTARTUP_CHK, MF_BYCOMMAND | (app.AutorunIsEnabled () ? MF_CHECKED : MF_UNCHECKED));

            break;
        }

        case RM_LOCALIZE:
        {
            SetDlgItemText (hwnd, IDC_INCLUDEMOUSECURSOR_CHK, app.LocaleString (IDS_INCLUDEMOUSECURSOR_CHK, nullptr));
            SetDlgItemText (hwnd, IDC_INCLUDEWINDOWSHADOW_CHK, app.LocaleString (IDS_INCLUDEWINDOWSHADOW_CHK, nullptr));
            SetDlgItemText (hwnd, IDC_CLEARBACKGROUND_CHK, app.LocaleString (IDS_CLEARBACKGROUND_CHK, nullptr));

            SetDlgItemText (hwnd, IDC_MODE_FULLSCREEN, app.LocaleString (IDS_MODE_FULLSCREEN, nullptr));
            SetDlgItemText (hwnd, IDC_MODE_WINDOW, app.LocaleString (IDS_MODE_WINDOW, nullptr));
            SetDlgItemText (hwnd, IDC_MODE_REGION, app.LocaleString (IDS_MODE_REGION, nullptr));

            SetDlgItemText (hwnd, IDC_SETTINGS, app.LocaleString (IDS_SETTINGS, nullptr));
            SetDlgItemText (hwnd, IDC_SCREENSHOT, app.LocaleString (IDS_SCREENSHOT, nullptr));
            SetDlgItemText (hwnd, IDC_EXIT, app.LocaleString (IDS_EXIT, nullptr));

            SetDlgItemText(hwnd, IDC_SCREENSHOT_SETTINGS, app.LocaleString(IDS_SCREENSHOT_SETTINGS, nullptr));
            SetDlgItemText(hwnd, IDC_COLOR, app.LocaleString(IDS_COLOR, nullptr));

            // configure button
            SetDlgItemText(hwnd, IDC_TITLE_FOLDER, app.LocaleString(IDS_FOLDER, L":"));
            SetDlgItemText(hwnd, IDC_TITLE_SETTINGS, app.LocaleString(IDS_QUICKSETTINGS, L":"));
            SetDlgItemText(hwnd, IDC_TITLE_MODE, app.LocaleString(IDS_MODE, L":"));
            SetDlgItemText(hwnd, IDC_PREVIEW_FIELD, app.LocaleString(IDS_PREVIEW, L":"));
            SetDlgItemText(hwnd, IDC_SAVE_BTN, app.LocaleString(IDS_SAVE_EDITS, L""));

            _r_wnd_addstyle (hwnd, IDC_SETTINGS, 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
            _r_wnd_addstyle (hwnd, IDC_SCREENSHOT, 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
            _r_wnd_addstyle(hwnd, IDC_BROWSE_BTN, 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
            _r_wnd_addstyle (hwnd, IDC_EXIT, 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
            _r_wnd_addstyle(hwnd, IDC_COLOR, 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

            // localize menu
            const HMENU menu = GetMenu (hwnd);
            rstring mode_fullscreen = app.LocaleString (IDS_MODE_FULLSCREEN, nullptr).GetString ();
            rstring mode_window = app.LocaleString (IDS_MODE_WINDOW, nullptr).GetString ();
            rstring mode_region = app.LocaleString (IDS_MODE_REGION, nullptr).GetString ();

            if (app.ConfigGet (L"HotkeyFullscreenEnabled", true).AsBool ())
                mode_fullscreen.ToLower ().AppendFormat (L"\t%s", KeyToString (app.ConfigGet (L"HotkeyFullscreen", HOTKEY_FULLSCREEN).AsUint ()).GetString ());

            if (app.ConfigGet (L"HotkeyWindowEnabled", true).AsBool ())
                mode_window.ToLower ().AppendFormat (L"\t%s", KeyToString (app.ConfigGet (L"HotkeyWindow", HOTKEY_WINDOW).AsUint ()).GetString ());

            if (app.ConfigGet (L"HotkeyRegionEnabled", true).AsBool ())
                mode_region.ToLower ().AppendFormat (L"\t%s", KeyToString (app.ConfigGet (L"HotkeyRegion", HOTKEY_REGION).AsUint ()).GetString ());

            app.LocaleMenu (menu, IDS_FILE, 0, true, nullptr);
            app.LocaleMenu (menu, IDS_EXPLORE, IDM_EXPLORE, false, L"...\tCtrl+E");
            app.LocaleMenu (menu, IDS_SCREENSHOT, IDM_TAKE_FULLSCREEN, false, _r_fmt (L": %s", mode_fullscreen.GetString ()));
            app.LocaleMenu (menu, IDS_SCREENSHOT, IDM_TAKE_WINDOW, false, _r_fmt (L": %s", mode_window.GetString ()));
            app.LocaleMenu (menu, IDS_SCREENSHOT, IDM_TAKE_REGION, false, _r_fmt (L": %s", mode_region.GetString ()));
            app.LocaleMenu (menu, IDS_EXIT, IDM_EXIT, false, L"\tAlt+F4");
            app.LocaleMenu (menu, IDS_VIEW, 1, true, nullptr);
            app.LocaleMenu (menu, IDS_ALWAYSONTOP_CHK, IDM_ALWAYSONTOP_CHK, false, nullptr);
            app.LocaleMenu (menu, IDS_LOADONSTARTUP_CHK, IDM_LOADONSTARTUP_CHK, false, nullptr);
            app.LocaleMenu (menu, IDS_HELP, 2, true, nullptr);
            app.LocaleMenu (menu, IDS_ABOUT, IDM_ABOUT, false, L"\tF1");

            break;
        }

        case WM_DESTROY:
        {
            CloseHandle (config.hregion_mutex);

            UnregisterClass (DUMMY_CLASS_DLG, app.GetHINSTANCE ());
            UnregisterClass (REGION_CLASS_DLG, app.GetHINSTANCE ());

            Gdiplus::GdiplusShutdown (gdiplusToken);
            CoUninitialize ();

            PostQuitMessage (0);

            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORBTN:
        {
            SetBkMode ((HDC)wparam, TRANSPARENT); // background-hack

            return (INT_PTR)GetSysColorBrush (COLOR_BTNFACE);
        }

        case WM_HOTKEY:
        {
            if (wparam == HOTKEY_ID_FULLSCREEN)
                TakeShot (nullptr, ScreenshotFullscreen);

            else if (wparam == HOTKEY_ID_WINDOW)
                TakeShot (nullptr, ScreenshotWindow);

            else if (wparam == HOTKEY_ID_REGION)
                TakeShot (nullptr, ScreenshotRegion);

            break;
        }

        case WM_COMMAND:
        {
            if ((LOWORD (wparam) >= IDX_FORMATS && LOWORD (wparam) <= IDX_FORMATS + formats.size ()))
            {
                const size_t idx = (LOWORD (wparam) - IDX_FORMATS);

                app.ConfigSet (L"ImageFormat", (LONGLONG)idx);

                return FALSE;
            }
            else if (LOWORD (wparam) == IDC_FOLDER && HIWORD (wparam) == EN_KILLFOCUS)
            {
                app.ConfigSet (L"Folder", _r_path_unexpand (_r_ctrl_gettext (hwnd, LOWORD (wparam))));

                return FALSE;
            }

            switch (LOWORD (wparam))
            {
                case IDM_EXPLORE:
                {
                    ShellExecute (hwnd, nullptr, GetDirectory (), nullptr, nullptr, SW_SHOWDEFAULT);
                    break;
                }

                case IDM_ALWAYSONTOP_CHK:
                {
                    const bool new_val = !app.ConfigGet (L"AlwaysOnTop", false).AsBool ();

                    CheckMenuItem (GetMenu (hwnd), LOWORD (wparam), MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));
                    app.ConfigSet (L"AlwaysOnTop", new_val);

                    _r_wnd_top (hwnd, new_val);

                    break;
                }

                case IDM_LOADONSTARTUP_CHK:
                {
                    const bool new_val = !app.AutorunIsEnabled ();
                    CheckMenuItem (GetMenu (hwnd), LOWORD (wparam), MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));
                    app.AutorunEnable (new_val);

                    break;
                }

                case IDM_COPYTOCLIPBOARD_CHK:
                {
                    const bool new_val = !app.ConfigGet (L"CopyToClipboard", false).AsBool ();
                    app.ConfigSet (L"CopyToClipboard", new_val);

                    break;
                }

                case IDC_BROWSE_BTN:
                {
                    BROWSEINFO browseInfo = {0};

                    browseInfo.hwndOwner = hwnd;
                    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_VALIDATE;

                    LPITEMIDLIST pidl = SHBrowseForFolder (&browseInfo);

                    if (pidl)
                    {
                        WCHAR buffer[MAX_PATH] = {0};

                        if (SHGetPathFromIDList (pidl, buffer))
                        {
                            app.ConfigSet (L"Folder", _r_path_unexpand (buffer));
                            SetDlgItemText (hwnd, IDC_FOLDER, buffer);
                        }

                        CoTaskMemFree (pidl);
                    }

                    break;
                }

                case IDM_PLAYSOUNDS_CHK:
                {
                    const bool new_val = !app.ConfigGet (L"IsPlaySound", true).AsBool ();
                    app.ConfigSet (L"IsPlaySound", new_val);

                    break;
                }

                case IDM_INCLUDEMOUSECURSOR_CHK:
                case IDC_INCLUDEMOUSECURSOR_CHK:
                {
                    const bool new_val = !app.ConfigGet (L"IsIncludeMouseCursor", false).AsBool ();

                    app.ConfigSet (L"IsIncludeMouseCursor", new_val);
                    CheckDlgButton (hwnd, IDC_INCLUDEMOUSECURSOR_CHK, new_val ? BST_CHECKED : BST_UNCHECKED);

                    break;
                }

                case IDM_CLEARBACKGROUND_CHK:
                case IDC_CLEARBACKGROUND_CHK:
                {
                    const bool new_val = !app.ConfigGet (L"IsClearBackground", true).AsBool ();

                    app.ConfigSet (L"IsClearBackground", new_val);
                    CheckDlgButton (hwnd, IDC_CLEARBACKGROUND_CHK, new_val ? BST_CHECKED : BST_UNCHECKED);

                    break;
                }

                case IDM_INCLUDEWINDOWSHADOW_CHK:
                case IDC_INCLUDEWINDOWSHADOW_CHK:
                {
                    const bool new_val = !app.ConfigGet (L"IsIncludeWindowShadow", true).AsBool ();

                    app.ConfigSet (L"IsIncludeWindowShadow", new_val);
                    CheckDlgButton (hwnd, IDC_INCLUDEWINDOWSHADOW_CHK, new_val ? BST_CHECKED : BST_UNCHECKED);

                    break;
                }

                case IDC_MODE_FULLSCREEN:
                case IDC_MODE_WINDOW:
                case IDC_MODE_REGION:
                {
                    EnumScreenshot mode;

                    if (LOWORD (wparam) == IDC_MODE_WINDOW)
                        mode = ScreenshotWindow;

                    else if (LOWORD (wparam) == IDC_MODE_REGION)
                        mode = ScreenshotRegion;

                    else
                        mode = ScreenshotFullscreen;

                    app.ConfigSet (L"Mode", (LONGLONG)mode);

                    break;
                }

                case IDM_FILENAME_INDEX:
                case IDM_FILENAME_DATE:
                {
                    EnumImageName val;

                    if (LOWORD (wparam) == IDM_FILENAME_DATE)
                        val = NameDate;

                    else
                        val = NameIndex;

                    app.ConfigSet (L"FilenameType", (LONGLONG)val);

                    break;
                }

                case IDM_TAKE_FULLSCREEN:
                case IDM_TAKE_WINDOW:
                case IDM_TAKE_REGION:
                case IDC_SCREENSHOT:
                {
                    const UINT ctrl_id = LOWORD (wparam);

                    EnumScreenshot mode;
                    HWND hwindow = nullptr;

                    if (ctrl_id == IDM_TAKE_WINDOW)
                    {
                        mode = ScreenshotWindow;
                    }
                    else if (ctrl_id == IDM_TAKE_REGION)
                    {
                        mode = ScreenshotRegion;
                    }
                    else if (ctrl_id == IDM_TAKE_FULLSCREEN)
                    {
                        mode = ScreenshotFullscreen;
                    }
                    else
                    {
                        mode = (EnumScreenshot)app.ConfigGet (L"Mode", 0).AsUint ();
                    }

                    if (mode == ScreenshotWindow)
                    {
                        EnumWindows (&FindTopWindow, (LPARAM)&hwindow);

                        if (hwindow)
                            _r_wnd_toggle (hwindow, true);
                    }

                    TakeShot (hwindow, mode);

                    break;
                }

                case IDC_SETTINGS:
                {
                    RECT rc = {0};
                    GetWindowRect (GetDlgItem (hwnd, IDC_SETTINGS), &rc);

                    const HMENU hmenu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_SETTINGS));
                    const HMENU submenu = GetSubMenu (hmenu, 0);

                    _app_initdropdownmenu (submenu, true);

                    TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, rc.left, rc.bottom, hwnd, nullptr);

                    DestroyMenu (hmenu);

                    break;
                }

                case IDC_EXIT:
                case IDM_EXIT:
                {
                    DestroyWindow (hwnd);
                    break;
                }

                case IDCANCEL: // process Esc key
                {
                    _r_wnd_toggle (hwnd, false);
                    break;
                }

                default:
                {
                    switch (wparam)
                    {
                        case IDC_COLOR:
                        {
                            ChooseScreenshotPenColor();
                            break;
                        }

                        case IDC_SAVE_BTN:
                        {
                            BITMAP bm;
                            GetObject(hCurrentBmp, sizeof(bm), &bm);
                            SaveBitmap(hCurrentBmp, bm.bmWidth, bm.bmHeight);
                        }

                        break;
                    }
                }
            }

            break;
        }

        case WM_LBUTTONDOWN:
        {
            ptCurPos.x = LOWORD(lparam);
            ptCurPos.y = HIWORD(lparam);

            if (IsMouseOverPreviewField(ptCurPos))
            {
                isDrawing = true;
                ptStartPos = { ptCurPos.x, ptCurPos.y };
            }

            break;
        }

        case WM_MOUSEMOVE:
        {
            if (isDrawing)
            {
                ptCurPos.x = LOWORD(lparam);
                ptCurPos.y = HIWORD(lparam);

                if (IsMouseOverPreviewField(ptCurPos))
                {
                    DrawToBitmap(ptCurPos);
                    ptStartPos = ptCurPos;
                }
            }

            break;
        }

        case WM_LBUTTONUP:
        {
            isDrawing = false;

            break;
        }
    }

    return FALSE;
}

INT APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, INT)
{
    MSG msg = {0};

    if (app.CreateMainWindow (IDD_MAIN, IDI_MAIN, &DlgProc))
    {
        const HACCEL haccel = LoadAccelerators (app.GetHINSTANCE (), MAKEINTRESOURCE (IDA_MAIN));

        if (haccel)
        {
            while (GetMessage (&msg, nullptr, 0, 0) > 0)
            {
                TranslateAccelerator (app.GetHWND (), haccel, &msg);

                if (!IsDialogMessage (app.GetHWND (), &msg))
                {
                    TranslateMessage (&msg);
                    DispatchMessage (&msg);
                }
            }

            DestroyAcceleratorTable (haccel);
        }
    }

    return (INT)msg.wParam;
}