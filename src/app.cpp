#include "app.h"

app::app (LPCWSTR name, LPCWSTR short_name)
{
	// get hinstance
	app_hinstance = GetModuleHandle (nullptr);

	// general information
	StringCchCopy (app_name, _countof (app_name), name);
	StringCchCopy (app_name_short, _countof (app_name_short), short_name);

	// get dpi scale
	{
		const HDC hdc = GetDC (nullptr);
		dpi_percent = DOUBLE (GetDeviceCaps (hdc, LOGPIXELSX)) / 96.0f;
		ReleaseDC (nullptr, hdc);
	}

	// get paths
	GetModuleFileName (GetHINSTANCE (), app_binary, _countof (app_binary));
	StringCchCopy (app_directory, _countof (app_directory), app_binary);
	PathRemoveFileSpec (app_directory);

	// parse command line
	INT numargs = 0;
	LPWSTR* arga = CommandLineToArgvW (GetCommandLine (), &numargs);

	if (arga && numargs > 1)
	{
		for (INT i = 1; i < numargs; i++)
		{
			if (_wcsnicmp (arga[i], L"/ini", 3) == 0 && (i + 1) < numargs)
			{
				LPWSTR ptr = arga[i + 1];

				PathUnquoteSpaces (ptr);

				StringCchCopy (app_config_path, _countof (app_config_path), _r_path_expand (ptr));

				if (PathGetDriveNumber (app_config_path) == -1)
					StringCchPrintf (app_config_path, _countof (app_config_path), L"%s\\%s", GetDirectory (), _r_path_expand (ptr).GetString ());

				if (!_r_fs_exists (app_config_path))
				{
					const HANDLE hfile = CreateFile (app_config_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

					if (hfile != INVALID_HANDLE_VALUE)
						CloseHandle (hfile);
				}
			}
		}
	}

	// get configuration path
	if (!app_config_path[0] || !_r_fs_exists (app_config_path))
		StringCchPrintf (app_config_path, _countof (app_config_path), L"%s\\%s.ini", GetDirectory (), app_name_short);

	if (!_r_fs_exists (app_config_path) && !_r_fs_exists (_r_fmt (L"%s\\portable.dat", GetDirectory ())))
	{
		StringCchCat (app_profile_directory, _countof (app_profile_directory), app_name);
		StringCchPrintf (app_config_path, _countof (app_config_path), L"%s\\%s.ini", app_profile_directory, app_name_short);
	}
	else
	{
		StringCchCopy (app_profile_directory, _countof (app_profile_directory), _r_path_extractdir (app_config_path));
	}

	// read config
	ConfigInit ();
}

#ifdef _APP_HAVE_AUTORUN
bool app::AutorunIsEnabled ()
{
	return ConfigGet (L"AutorunIsEnabled", false).AsBool ();
}

bool app::AutorunEnable (bool is_enable)
{
	bool result = false;

	HKEY hroot = HKEY_CURRENT_USER;
	rstring reg_path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

	// create autorun entry for current user only
	{
		rstring reg_domain;
		rstring reg_username;
		rstring reg_sid;

		_r_sys_getusername (&reg_domain, &reg_username);
		reg_sid = _r_sys_getusernamesid (reg_domain, reg_username);

		if (!reg_sid.IsEmpty ())
		{
			hroot = HKEY_USERS;
			reg_path.Format (L"%s\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", reg_sid.GetString ());
		}
	}

	HKEY hkey = nullptr;

	if (RegOpenKeyEx (hroot, reg_path, 0, KEY_ALL_ACCESS, &hkey) == ERROR_SUCCESS)
	{
		if (is_enable)
		{
			WCHAR buffer[MAX_PATH] = {0};
			StringCchPrintf (buffer, _countof (buffer), L"\"%s\" /minimized", GetBinaryPath ());

			result = (RegSetValueEx (hkey, app_name, 0, REG_SZ, (LPBYTE)buffer, DWORD ((wcslen (buffer) + 1) * sizeof (WCHAR))) == ERROR_SUCCESS);

			if (result)
				ConfigSet (L"AutorunIsEnabled", true);
		}
		else
		{
			RegDeleteValue (hkey, app_name);
			ConfigSet (L"AutorunIsEnabled", false);

			result = true;
		}

		RegCloseKey (hkey);
	}

	return result;
}
#endif // _APP_HAVE_AUTORUN

void app::ConfigInit ()
{
	app_config_array.clear (); // reset
	ParseINI (GetConfigPath (), &app_config_array, nullptr);
}

rstring app::ConfigGet (LPCWSTR key, INT def, LPCWSTR name)
{
	return ConfigGet (key, _r_fmt (L"%" PRId32, def), name);
}

rstring app::ConfigGet (LPCWSTR key, UINT def, LPCWSTR name)
{
	return ConfigGet (key, _r_fmt (L"%" PRIu32, def), name);
}

rstring app::ConfigGet (LPCWSTR key, DWORD def, LPCWSTR name)
{
	return ConfigGet (key, _r_fmt (L"%lu", def), name);
}

rstring app::ConfigGet (LPCWSTR key, LONGLONG def, LPCWSTR name)
{
	return ConfigGet (key, _r_fmt (L"%" PRId64, def), name);
}

rstring app::ConfigGet (LPCWSTR key, LPCWSTR def, LPCWSTR name)
{
	rstring result;

	if (!name)
		name = app_name_short;

	// check key is exists
	if (app_config_array.find (name) != app_config_array.end () && app_config_array.at (name).find (key) != app_config_array.at (name).end ())
		result = app_config_array.at (name).at (key);

	if (result.IsEmpty ())
		result = def;

	return result;
}

bool app::ConfigSet (LPCWSTR key, DWORD val, LPCWSTR name)
{
	return ConfigSet (key, _r_fmt (L"%lu", val), name);
}

bool app::ConfigSet (LPCWSTR key, LONG val, LPCWSTR name)
{
	return ConfigSet (key, _r_fmt (L"%" PRId32, val), name);
}

bool app::ConfigSet (LPCWSTR key, LONGLONG val, LPCWSTR name)
{
	return ConfigSet (key, _r_fmt (L"%" PRId64, val), name);
}

bool app::ConfigSet (LPCWSTR key, bool val, LPCWSTR name)
{
	return ConfigSet (key, val ? L"true" : L"false", name);
}

bool app::ConfigSet (LPCWSTR key, LPCWSTR val, LPCWSTR name)
{
	if (!_r_fs_exists (app_profile_directory))
		_r_fs_mkdir (app_profile_directory);

	if (!name)
		name = app_name_short;

	// update hash value
	app_config_array[name][key] = val;

	if (WritePrivateProfileString (name, key, val, GetConfigPath ()))
		return true;

	return false;
}

LRESULT CALLBACK app::MainWindowProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	static app* this_ptr = (app*)GetProp (hwnd, L"this_ptr");

	switch (msg)
	{
		case WM_SHOWWINDOW:
		{
			if (this_ptr->is_needmaximize)
			{
				ShowWindow (hwnd, SW_MAXIMIZE);
				this_ptr->is_needmaximize = false;
			}

			break;
		}

		case WM_DESTROY:
		{
			if ((GetWindowLongPtr (hwnd, GWL_STYLE) & WS_MAXIMIZEBOX) != 0)
				this_ptr->ConfigSet (L"IsWindowZoomed", IsZoomed (hwnd) ? true : false);

			SendMessage (hwnd, RM_UNINITIALIZE, 0, 0);

			break;
		}

		case WM_QUERYENDSESSION:
		{
			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);

            return TRUE;
		}

		case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO lpmmi = (LPMINMAXINFO)lparam;

			lpmmi->ptMinTrackSize.x = this_ptr->max_width;
			lpmmi->ptMinTrackSize.y = this_ptr->max_height;

			break;
		}

		case WM_EXITSIZEMOVE:
		{
			RECT rc = {0};
			GetWindowRect (hwnd, &rc);

			this_ptr->ConfigSet (L"WindowPosX", rc.left);
			this_ptr->ConfigSet (L"WindowPosY", rc.top);

			if ((GetWindowLongPtr (hwnd, GWL_STYLE) & WS_SIZEBOX) != 0)
			{
				this_ptr->ConfigSet (L"WindowPosWidth", _R_RECT_WIDTH (&rc));
				this_ptr->ConfigSet (L"WindowPosHeight", _R_RECT_HEIGHT (&rc));
			}

			break;
		}
	}

	if (!this_ptr->app_wndproc)
		return FALSE;

	return CallWindowProc (this_ptr->app_wndproc, hwnd, msg, wparam, lparam);
}

bool app::CreateMainWindow (UINT dlg_id, UINT icon_id, DLGPROC proc)
{
	bool result = false;

	// initialize controls
	{
		INITCOMMONCONTROLSEX icex = {0};

		icex.dwSize = sizeof (icex);
		icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;

		InitCommonControlsEx (&icex);
	}

	// create window
	if (dlg_id && proc)
	{
		app_hwnd = CreateDialog (nullptr, MAKEINTRESOURCE (dlg_id), nullptr, proc);

		if (app_hwnd)
		{
			// uipi fix (vista+)
            _r_wnd_changemessagefilter(GetHWND(), WM_DROPFILES, MSGFLT_ALLOW);
            _r_wnd_changemessagefilter(GetHWND(), WM_COPYDATA, MSGFLT_ALLOW);
            _r_wnd_changemessagefilter(GetHWND(), WM_COPYGLOBALDATA, MSGFLT_ALLOW);

			// subclass window
			SetProp (GetHWND (), L"this_ptr", this);
			app_wndproc = (WNDPROC)SetWindowLongPtr (GetHWND (), DWLP_DLGPROC, (LONG_PTR)&MainWindowProc);

			// update autorun settings
#ifdef _APP_HAVE_AUTORUN
			if (AutorunIsEnabled ())
				AutorunEnable (true);
#endif // _APP_HAVE_AUTORUN

			// set window on top
			_r_wnd_top (GetHWND (), ConfigGet (L"AlwaysOnTop", _APP_ALWAYSONTOP).AsBool ());

			// restore window position
			{
				RECT rect_original = {0};

				// set minmax info
				if (GetWindowRect (GetHWND (), &rect_original))
				{
					max_width = _R_RECT_WIDTH (&rect_original);
					max_height = _R_RECT_HEIGHT (&rect_original);
				}

				// send resize message
				if ((GetWindowLongPtr (GetHWND (), GWL_STYLE) & WS_SIZEBOX) != 0)
				{
					RECT rc_client = {0};

					if (GetClientRect (GetHWND (), &rc_client))
						SendMessage (GetHWND (), WM_SIZE, 0, MAKELPARAM (_R_RECT_WIDTH (&rc_client), _R_RECT_HEIGHT (&rc_client)));
				}

				// restore window position
				RECT rect_new = {0};

				rect_new.left = ConfigGet (L"WindowPosX", rect_original.left).AsLong ();
				rect_new.top = ConfigGet (L"WindowPosY", rect_original.top).AsLong ();

				if ((GetWindowLongPtr (GetHWND (), GWL_STYLE) & WS_SIZEBOX) != 0)
				{
					rect_new.right = max (ConfigGet (L"WindowPosWidth", 0).AsLong (), max_width) + rect_new.left;
					rect_new.bottom = max (ConfigGet (L"WindowPosHeight", 0).AsLong (), max_height) + rect_new.top;
				}
				else
				{
					rect_new.right = _R_RECT_WIDTH (&rect_original) + rect_new.left;
					rect_new.bottom = _R_RECT_HEIGHT (&rect_original) + rect_new.top;
				}

				_r_wnd_adjustwindowrect (nullptr, &rect_new);

				SetWindowPos (GetHWND (), nullptr, rect_new.left, rect_new.top, _R_RECT_WIDTH (&rect_new), _R_RECT_HEIGHT (&rect_new), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_FRAMECHANGED);
			}

			// show window
			{
				INT show_code = SW_SHOW;

				if (ConfigGet (L"IsWindowZoomed", false).AsBool () && (GetWindowLongPtr (GetHWND (), GWL_STYLE) & WS_MAXIMIZEBOX) != 0)
				{
					if (show_code == SW_HIDE)
						is_needmaximize = true;

					else
						show_code = SW_SHOWMAXIMIZED;
				}

				ShowWindow (GetHWND (), show_code);
			}

			// set icons
			if (icon_id)
			{
				SendMessage (GetHWND (), WM_SETICON, ICON_SMALL, (LPARAM)GetSharedIcon (GetHINSTANCE (), icon_id, GetSystemMetrics (SM_CXSMICON)));
				SendMessage (GetHWND (), WM_SETICON, ICON_BIG, (LPARAM)GetSharedIcon (GetHINSTANCE (), icon_id, GetSystemMetrics (SM_CXICON)));
			}

			// common initialization
			SendMessage (GetHWND (), RM_INITIALIZE, 0, 0);
			SendMessage (GetHWND (), RM_LOCALIZE, 0, 0);

			DrawMenuBar (GetHWND ()); // redraw menu

			result = true;
	    }
	}

	return result;
}

LPCWSTR app::GetBinaryPath () const
{
	return app_binary;
}

LPCWSTR app::GetDirectory () const
{
	return app_directory;
}

LPCWSTR app::GetConfigPath () const
{
	return app_config_path;
}

INT app::GetDPI (INT v) const
{
	return (INT)ceil (DOUBLE (v)* dpi_percent);
}

HICON app::GetSharedIcon (HINSTANCE hinst, UINT icon_id, INT cx_width)
{
	PAPP_SHARED_ICON presult = nullptr;

	for (size_t i = 0; i < app_shared_icons.size (); i++)
	{
		PAPP_SHARED_ICON const pshared = app_shared_icons.at (i);

		if (pshared && pshared->hinst == hinst && pshared->icon_id == icon_id && pshared->cx_width == cx_width && pshared->hicon)
			presult = pshared;
	}

	if (!presult)
	{
		presult = new APP_SHARED_ICON;

		if (presult)
		{
			const HICON hicon = _r_loadicon (hinst, MAKEINTRESOURCE (icon_id), cx_width);

			if (hicon)
			{
				presult->hinst = hinst;
				presult->icon_id = icon_id;
				presult->cx_width = cx_width;
				presult->hicon = hicon;

				app_shared_icons.push_back (presult);
			}
			else
			{
				delete presult;
				presult = nullptr;
			}
		}
	}

	if (!presult)
		return nullptr;

	return presult->hicon;
}

HINSTANCE app::GetHINSTANCE () const
{
	return app_hinstance;
}

HWND app::GetHWND () const
{
	return app_hwnd;
}

rstring app::LocaleString (UINT uid, LPCWSTR append)
{
	rstring result;

	if (uid)
	{
		rstring key;
		key.Format (L"%03d", uid);

        if (LoadString(GetHINSTANCE(), uid, result.GetBuffer(_R_BUFFER_LENGTH), _R_BUFFER_LENGTH))
        {
            result.ReleaseBuffer();
        }
        else
        {
            result.ReleaseBuffer();
            result = key;
        }
	}

	if (append)
		result.Append (append);

	return result;
}

void app::LocaleMenu (HMENU menu, UINT id, UINT item, bool by_position, LPCWSTR append)
{
	WCHAR buffer[128] = {0};
	StringCchCopy (buffer, _countof (buffer), LocaleString (id, append));

	MENUITEMINFO mi = {0};

	mi.cbSize = sizeof (mi);
	mi.fMask = MIIM_STRING;
	mi.dwTypeData = buffer;

	SetMenuItemInfo (menu, item, by_position, &mi);
}

bool app::ParseINI (LPCWSTR path, rstring::map_two* pmap, std::vector<rstring>* psections)
{
	bool result = false;

	if (pmap && _r_fs_exists (path))
	{
		rstring section_ptr;
		rstring value_ptr;

		size_t length = 0, out_length = 0;
		size_t delimeter = 0;

		// get sections
		do
		{
			length += _R_BUFFER_LENGTH;

			out_length = GetPrivateProfileSectionNames (section_ptr.GetBuffer (length), (DWORD)length, path);
		}
		while (out_length == (length - 1));

		section_ptr.SetLength (out_length);

		LPCWSTR section = section_ptr.GetString ();

		while (*section)
		{
			// get values
			length = 0;

			if (psections)
				psections->push_back (section);

			do
			{
				length += _R_BUFFER_LENGTH;

				out_length = GetPrivateProfileSection (section, value_ptr.GetBuffer (length), (DWORD)length, path);
			}
			while (out_length == (length - 1));

			value_ptr.SetLength (out_length);

			LPCWSTR value = value_ptr.GetString ();

			while (*value)
			{
				rstring parser = value;

				delimeter = parser.Find (L'=');

				if (delimeter != rstring::npos)
					(*pmap)[section][parser.Midded (0, delimeter)] = parser.Midded (delimeter + 1); // set

				value += wcslen (value) + 1; // go next item
			}

			section += wcslen (section) + 1; // go next section
		}

		result = true;
	}

	return result;
}