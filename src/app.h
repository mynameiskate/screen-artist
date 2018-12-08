#ifndef __APP_H__
#define __APP_H__

#define APP_NAME L"Screen Artist"
#define APP_NAME_SHORT L"SA"

#include "helper.h"
#include "resource.h"
#include "rconfig.h"

/*
Structures
*/
typedef struct
{
    HINSTANCE hinst = nullptr;
    UINT icon_id = 0;
    INT cx_width = 0;
    HICON hicon = nullptr;
} *PAPP_SHARED_ICON, APP_SHARED_ICON;

/*
    Application class
*/
class app
{
public:

    app(LPCWSTR name, LPCWSTR short_name);

    bool AutorunIsEnabled();
    bool AutorunEnable(bool is_enable);

    rstring ConfigGet(LPCWSTR key, INT def, LPCWSTR name = nullptr);
    rstring ConfigGet(LPCWSTR key, UINT def, LPCWSTR name = nullptr);
    rstring ConfigGet(LPCWSTR key, DWORD def, LPCWSTR name = nullptr);
    rstring ConfigGet(LPCWSTR key, LONGLONG def, LPCWSTR name = nullptr);
    rstring ConfigGet(LPCWSTR key, LPCWSTR def, LPCWSTR name = nullptr);

    bool ConfigSet(LPCWSTR key, DWORD val, LPCWSTR name = nullptr);
    bool ConfigSet(LPCWSTR key, LONG val, LPCWSTR name = nullptr);
    bool ConfigSet(LPCWSTR key, LONGLONG val, LPCWSTR name = nullptr);
    bool ConfigSet(LPCWSTR key, bool val, LPCWSTR name = nullptr);
    bool ConfigSet(LPCWSTR key, LPCWSTR val, LPCWSTR name = nullptr);

    void ConfigInit();
    bool CreateMainWindow(UINT dlg_id, UINT icon_id, DLGPROC proc);

    LPCWSTR GetBinaryPath() const;
    LPCWSTR GetDirectory() const;
    LPCWSTR GetConfigPath() const;

    INT GetDPI(INT v) const;
    HINSTANCE GetHINSTANCE() const;
    HWND GetHWND() const;
    HICON GetSharedIcon(HINSTANCE hinst, UINT icon_id, INT cx_width);

    rstring LocaleString(UINT id, LPCWSTR append);
    void LocaleMenu(HMENU menu, UINT id, UINT item, bool by_position, LPCWSTR append);

private:
    static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    bool ParseINI(LPCWSTR path, rstring::map_two* pmap, std::vector<rstring>* psections);

    DOUBLE dpi_percent = 0.f;
    bool is_needmaximize = false;

    LONG max_width = 0;
    LONG max_height = 0;

    WNDPROC app_wndproc = nullptr;
    HWND app_hwnd = nullptr;
    HINSTANCE app_hinstance = nullptr;

    WCHAR app_binary[MAX_PATH];
    WCHAR app_directory[MAX_PATH];
    WCHAR app_profile_directory[MAX_PATH];
    WCHAR app_config_path[MAX_PATH];

    WCHAR app_name[MAX_PATH];
    WCHAR app_name_short[MAX_PATH];

    rstring::map_two app_config_array;
    std::vector<PAPP_SHARED_ICON> app_shared_icons;
};

#endif // __APP_H__
