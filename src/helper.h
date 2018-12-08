#pragma once

#include <windows.h>
#include <wtsapi32.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <psapi.h>
#include <uxtheme.h>
#include <time.h>
#include <lm.h>
#include <process.h>
#include <winhttp.h>
#include <subauth.h>
#include <sddl.h>
#include <inttypes.h>

#include "rconfig.h"
#include "rstring.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wtsapi32.lib")

#ifndef LVM_RESETEMPTYTEXT
#define LVM_RESETEMPTYTEXT (LVM_FIRST + 84)
#endif // LVM_RESETEMPTYTEXT

#ifndef WM_COPYGLOBALDATA
#define WM_COPYGLOBALDATA 0x0049
#endif // WM_COPYGLOBALDATA

#ifndef OBJ_NAME_PATH_SEPARATOR
#define OBJ_NAME_PATH_SEPARATOR ((WCHAR)L'\\')
#endif // OBJ_NAME_PATH_SEPARATOR

/*
    Rectangle
*/
#define _R_RECT_WIDTH(lprect)((lprect)->right - (lprect)->left)
#define _R_RECT_HEIGHT(lprect)((lprect)->bottom - (lprect)->top)

/*
    Format strings, dates, numbers
*/
rstring _r_fmt (LPCWSTR format, ...);
rstring _r_fmt_date(const LPFILETIME ft, const DWORD flags = FDTF_DEFAULT);
rstring _r_fmt_date (const time_t ut, const DWORD flags = FDTF_DEFAULT);

/*
    Filesystem
*/
bool _r_fs_exists (LPCWSTR path);
bool _r_fs_mkdir (LPCWSTR path);

/*
    Paths
*/
rstring _r_path_expand (const rstring &path);
rstring _r_path_unexpand (const rstring &path);
rstring _r_path_extractdir (LPCWSTR path);
rstring _r_path_extractfile (LPCWSTR path);

/*
    Strings
*/
WCHAR _r_str_lower (WCHAR chr);
WCHAR _r_str_upper (WCHAR chr);
size_t _r_str_hash (LPCWSTR text);

/*
    System information
*/
void _r_sys_getusername (rstring* pdomain, rstring* pusername);
rstring _r_sys_getusernamesid (LPCWSTR domain, LPCWSTR username);
void _r_sleep(DWORD milliseconds);

/*
    Unixtime
*/
void _r_unixtime_to_filetime (time_t ut, const LPFILETIME pft);

/*
    Window management
*/
void _r_wnd_adjustwindowrect(HWND hwnd, LPRECT lprect);
void _r_wnd_toggle(HWND hwnd, bool show);
void _r_wnd_addstyle(HWND hwnd, UINT ctrl_id, LONG_PTR mask, LONG_PTR stateMask, INT index);
void _r_wnd_top(HWND hwnd, bool is_enable);
void _r_wnd_changemessagefilter(HWND hwnd, UINT msg, DWORD action);

/*
    Other
*/
HICON _r_loadicon (HINSTANCE hinst, LPCWSTR name, INT cx_width);
bool r_get_encoder_clsid(LPCWSTR exif, CLSID *pClsid);

/*
    Controls
*/
rstring _r_ctrl_gettext(HWND hwnd, UINT ctrl_id);