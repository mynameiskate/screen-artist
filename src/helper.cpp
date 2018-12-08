#include "helper.h"
#include <gdiplus.h>

/*
    Format strings, dates, numbers
*/
rstring _r_fmt (LPCWSTR format, ...)
{
    rstring result;

    va_list args;
    va_start (args, format);

    result.FormatV (format, args);

    va_end (args);

    return result;
}

rstring _r_fmt_date(const LPFILETIME ft, const DWORD flags)
{
    DWORD pflags = flags;

    rstring result;

    SHFormatDateTime(ft, &pflags, result.GetBuffer(256), 256);
    result.ReleaseBuffer();

    return result;
}

rstring _r_fmt_date(const time_t ut, const DWORD flags)
{
    FILETIME ft = { 0 };
    _r_unixtime_to_filetime(ut, &ft);

    return _r_fmt_date(&ft, flags);
}

/*
    Filesystem
*/
bool _r_fs_exists (LPCWSTR path)
{
    return (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES);
}

bool _r_fs_mkdir (LPCWSTR path)
{
    bool result = false;

    const HMODULE hlib = GetModuleHandle (L"shell32.dll");

    if (hlib)
    {
        typedef int (WINAPI *SHCDEX) (HWND, LPCTSTR, const SECURITY_ATTRIBUTES*); // SHCreateDirectoryEx
        const SHCDEX _SHCreateDirectoryEx = (SHCDEX)GetProcAddress (hlib, "SHCreateDirectoryExW");

        if (_SHCreateDirectoryEx)
            result = _SHCreateDirectoryEx (nullptr, path, nullptr) == ERROR_SUCCESS;
    }

    if (!result)
    {
        if (CreateDirectory (path, nullptr))
            result = true;
    }

    return result;
}

/*
    Paths
*/
rstring _r_path_expand (const rstring &path)
{
    if (path.IsEmpty ())
        return path;

    if (path.At (0) == OBJ_NAME_PATH_SEPARATOR)
        return path;

    if (path.Find (OBJ_NAME_PATH_SEPARATOR) == rstring::npos)
        return path;

    rstring result;

    if (path.Find (L'%') != rstring::npos)
    {
        if (!ExpandEnvironmentStrings (path, result.GetBuffer (4096), 4096))
            result = path;
    }
    else
    {
        if (!PathSearchAndQualify (path, result.GetBuffer (4096), 4096))
            result = path;
    }

    result.ReleaseBuffer ();

    return result;
}

rstring _r_path_unexpand (const rstring &path)
{
    if (path.IsEmpty ())
        return path;

    if (path.Find (OBJ_NAME_PATH_SEPARATOR) == rstring::npos)
        return path;

    if (path.At (0) == OBJ_NAME_PATH_SEPARATOR)
        return path;

    rstring result;

    if (!PathUnExpandEnvStrings (path, result.GetBuffer (4096), 4096))
        result = path;

    result.ReleaseBuffer ();

    return result;
}

rstring _r_path_extractdir (LPCWSTR path)
{
    rstring result = path;
    const size_t pos = result.ReverseFind (L"\\/");

    result.Mid (0, pos);
    result.Trim (L"\\/");

    return result;
}

rstring _r_path_extractfile (LPCWSTR path)
{
    return PathFindFileName (path);
}

/*
    Strings
*/

WCHAR _r_str_lower (WCHAR chr)
{
    WCHAR buf[] = {chr, 0};
    CharLowerBuff (buf, _countof (buf));

    return buf[0];
}

WCHAR _r_str_upper (WCHAR chr)
{
    WCHAR buf[] = {chr, 0};
    CharUpperBuff (buf, _countof (buf));

    return buf[0];
}

size_t _r_str_hash (LPCWSTR text)
{
    if (!text)
        return 0;

#define InitialFNV 2166136261ULL
#define FNVMultiple 16777619

    size_t hash = InitialFNV;
    const size_t length = wcslen (text);

    for (size_t i = 0; i < length; i++)
    {
        hash = hash ^ (_r_str_lower (text[i])); /* xor the low 8 bits */
        hash = hash * FNVMultiple; /* multiply by the magic number */
    }

    return hash;
}

/*
    System information
*/
bool _r_sys_adminstate ()
{
    BOOL result = FALSE;
    DWORD status = 0, ps_size = sizeof (PRIVILEGE_SET);

    HANDLE token = nullptr, impersonation_token = nullptr;

    PRIVILEGE_SET ps = {0};
    GENERIC_MAPPING gm = {0};

    PACL acl = nullptr;
    PSID sid = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;

    SID_IDENTIFIER_AUTHORITY sia = SECURITY_NT_AUTHORITY;

    __try
    {
        if (!OpenThreadToken (GetCurrentThread (), TOKEN_DUPLICATE | TOKEN_QUERY, TRUE, &token))
        {
            if (GetLastError () != ERROR_NO_TOKEN || !OpenProcessToken (GetCurrentProcess (), TOKEN_DUPLICATE | TOKEN_QUERY, &token))
                __leave;
        }

        if (!DuplicateToken (token, SecurityImpersonation, &impersonation_token))
            __leave;

        if (!AllocateAndInitializeSid (&sia, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid))
            __leave;

        sd = LocalAlloc (LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);

        if (!sd || !InitializeSecurityDescriptor (sd, SECURITY_DESCRIPTOR_REVISION))
            __leave;

        DWORD acl_size = sizeof (ACL) + sizeof (ACCESS_ALLOWED_ACE) + (size_t)GetLengthSid (sid) - sizeof (DWORD);
        acl = (PACL)LocalAlloc (LPTR, (SIZE_T)acl_size);

        if (!acl || !InitializeAcl (acl, acl_size, ACL_REVISION2) || !AddAccessAllowedAce (acl, ACL_REVISION2, ACCESS_READ | ACCESS_WRITE, sid) || !SetSecurityDescriptorDacl (sd, TRUE, acl, FALSE))
            __leave;

        SetSecurityDescriptorGroup (sd, sid, FALSE);
        SetSecurityDescriptorOwner (sd, sid, FALSE);

        if (!IsValidSecurityDescriptor (sd))
            __leave;

        gm.GenericRead = ACCESS_READ;
        gm.GenericWrite = ACCESS_WRITE;
        gm.GenericExecute = 0;
        gm.GenericAll = ACCESS_READ | ACCESS_WRITE;

        if (!AccessCheck (sd, impersonation_token, ACCESS_READ, &gm, &ps, &ps_size, &status, &result))
        {
            result = FALSE;
            __leave;
        }
    }

    __finally
    {
        if (acl)
            LocalFree (acl);

        if (sd)
            LocalFree (sd);

        if (sid)
            FreeSid (sid);

        if (impersonation_token)
            CloseHandle (impersonation_token);

        if (token)
            CloseHandle (token);
    }

    return result ? true : false;
}

void _r_sys_getusername (rstring* pdomain, rstring* pusername)
{
    LPWSTR username = nullptr;
    LPWSTR domain = nullptr;

    DWORD username_bytes = 0;
    DWORD domain_bytes = 0;

    if (
        WTSQuerySessionInformation (WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSUserName, &username, &username_bytes) &&
        WTSQuerySessionInformation (WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSDomainName, &domain, &domain_bytes)
        )
    {
        if (pusername)
            *pusername = username;

        if (pdomain)
            *pdomain = domain;
    }

    if (username)
        WTSFreeMemory (username);

    if (domain)
        WTSFreeMemory (domain);
}

rstring _r_sys_getusernamesid (LPCWSTR domain, LPCWSTR username)
{
    rstring result;

    DWORD sid_length = SECURITY_MAX_SID_SIZE;
    PBYTE psid = new BYTE[sid_length];

    if (psid)
    {
        WCHAR ref_domain[MAX_PATH] = {0};
        DWORD ref_length = _countof (ref_domain);

        SID_NAME_USE name_use;

        if (LookupAccountName (domain, username, psid, &sid_length, ref_domain, &ref_length, &name_use))
        {
            LPWSTR sidstring = nullptr;

            if (ConvertSidToStringSid (psid, &sidstring))
            {
                result = sidstring;
                LocalFree (sidstring);
            }
        }

        delete[] psid;
    }

    return result;
}

bool _r_sys_setprivilege (LPCWSTR privileges[], size_t count, bool is_enable)
{
    HANDLE token = nullptr;

    LUID luid = {0};
    TOKEN_PRIVILEGES tp = {0};

    bool result = false;

    if (OpenProcessToken (GetCurrentProcess (), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        for (size_t i = 0; i < count; i++)
        {
            if (LookupPrivilegeValue (nullptr, privileges[i], &luid))
            {
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Luid = luid;
                tp.Privileges[0].Attributes = is_enable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_REMOVED;

                if (AdjustTokenPrivileges (token, FALSE, &tp, sizeof (tp), nullptr, nullptr))
                    result = true;
            }
        }

        CloseHandle (token);
    }

    return result;
}

void _r_sleep (DWORD milliseconds)
{
    if (!milliseconds || milliseconds == INFINITE)
        return;

    WaitForSingleObjectEx (GetCurrentThread(), milliseconds, FALSE);
}

/*
    Unixtime
*/
void _r_unixtime_to_filetime (time_t ut, const LPFILETIME pft)
{
    if (pft)
    {
        const LONGLONG ll = Int32x32To64 (ut, 10000000) + 116444736000000000; // 64-bit value

        pft->dwLowDateTime = DWORD (ll);
        pft->dwHighDateTime = ll >> 32;
    }
}

void _r_unixtime_to_systemtime (time_t ut, const LPSYSTEMTIME pst)
{
    FILETIME ft = {0};

    _r_unixtime_to_filetime (ut, &ft);

    FileTimeToSystemTime (&ft, pst);
}

/*
    Window management
*/
void _r_wnd_addstyle (HWND hwnd, UINT ctrl_id, LONG_PTR mask, LONG_PTR stateMask, INT index)
{
    if (ctrl_id)
        hwnd = GetDlgItem (hwnd, ctrl_id);

    LONG_PTR style = (GetWindowLongPtr (hwnd, index) & ~stateMask) | mask;

    SetWindowLongPtr (hwnd, index, style);

    SetWindowPos (hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void _r_wnd_adjustwindowrect (HWND hwnd, LPRECT lprect)
{
    MONITORINFO monitorInfo = {0};
    monitorInfo.cbSize = sizeof (monitorInfo);

    const HMONITOR hmonitor = hwnd ? MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST) : MonitorFromRect (lprect, MONITOR_DEFAULTTONEAREST);

    if (GetMonitorInfo (hmonitor, &monitorInfo))
    {
        LPRECT lpbounds = &monitorInfo.rcWork;

        const int original_width = _R_RECT_WIDTH (lprect);
        const int original_height = _R_RECT_HEIGHT (lprect);

        if (lprect->left + original_width > lpbounds->left + _R_RECT_WIDTH (lpbounds))
            lprect->left = lpbounds->left + _R_RECT_WIDTH (lpbounds) - original_width;

        if (lprect->top + original_height > lpbounds->top + _R_RECT_HEIGHT (lpbounds))
            lprect->top = lpbounds->top + _R_RECT_HEIGHT (lpbounds) - original_height;

        if (lprect->left < lpbounds->left)
            lprect->left = lpbounds->left;

        if (lprect->top < lpbounds->top)
            lprect->top = lpbounds->top;

        lprect->right = lprect->left + original_width;
        lprect->bottom = lprect->top + original_height;
    }
}

void _r_wnd_centerwindowrect (LPRECT lprect, LPRECT lpparent)
{
    lprect->left = lpparent->left + (_R_RECT_WIDTH (lpparent) - _R_RECT_WIDTH (lprect)) / 2;
    lprect->top = lpparent->top + (_R_RECT_HEIGHT (lpparent) - _R_RECT_HEIGHT (lprect)) / 2;
}

void _r_wnd_changemessagefilter (HWND hwnd, UINT msg, DWORD action)
{
    const HMODULE hlib = GetModuleHandle(L"user32.dll");

    if (hlib)
    {
        typedef BOOL(WINAPI *CWMFEX) (HWND, UINT, DWORD, PVOID); // ChangeWindowMessageFilterEx
        const CWMFEX _ChangeWindowMessageFilterEx = (CWMFEX)GetProcAddress(hlib, "ChangeWindowMessageFilterEx");
        _ChangeWindowMessageFilterEx(hwnd, msg, action, nullptr);
    }
}

void _r_wnd_toggle (HWND hwnd, bool show)
{
    if (show || !IsWindowVisible (hwnd))
    {
        ShowWindow (hwnd, SW_SHOW);
        SetForegroundWindow (hwnd);
        SwitchToThisWindow (hwnd, TRUE);
    }
    else
    {
        ShowWindow (hwnd, SW_HIDE);
    }
}

void _r_wnd_top (HWND hwnd, bool is_enable)
{
    SetWindowPos (hwnd, (is_enable ? HWND_TOPMOST : HWND_NOTOPMOST), 0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/*
    Other
*/

HICON _r_loadicon (HINSTANCE hinst, LPCWSTR name, INT cx_width)
{
    HICON result = nullptr;

    const HMODULE hlib = GetModuleHandle (L"comctl32.dll");

    if (hlib)
    {
        typedef HRESULT (WINAPI *LIWSD) (HINSTANCE, PCWSTR, INT, INT, HICON*); // LoadIconWithScaleDown
        const LIWSD _LoadIconWithScaleDown = (LIWSD)GetProcAddress (hlib, "LoadIconWithScaleDown");

        if (_LoadIconWithScaleDown)
            _LoadIconWithScaleDown (hinst, name, cx_width, cx_width, &result);
    }

    if (!result)
        result = (HICON)LoadImage(hinst, name, IMAGE_ICON, cx_width, cx_width, 0);

    return result;
}

/*
    Control: common
*/
rstring _r_ctrl_gettext (HWND hwnd, UINT ctrl_id)
{
    rstring result = L"";

    size_t length = (size_t)SendDlgItemMessage (hwnd, ctrl_id, WM_GETTEXTLENGTH, 0, 0);

    if (length)
    {
        length += 1;

        GetDlgItemText (hwnd, ctrl_id, result.GetBuffer (length), (INT)length);
        result.ReleaseBuffer ();
    }

    return result;
}


bool r_get_encoder_clsid(LPCWSTR exif, CLSID *pClsid)
{
    UINT num = 0, size = 0;
    const size_t len = wcslen(exif);
    Gdiplus::GetImageEncodersSize(&num, &size);

    if (size)
    {
        Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)malloc(size);

        if (pImageCodecInfo)
        {
            Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

            for (UINT i = 0; i < num; ++i)
            {
                if (_wcsnicmp(pImageCodecInfo[i].MimeType, exif, len) == 0)
                {
                    *pClsid = pImageCodecInfo[i].Clsid;
                    free(pImageCodecInfo);

                    return true;
                }
            }

            free(pImageCodecInfo);
        }
    }

    return false;
}
