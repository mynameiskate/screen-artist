#pragma once

#include <windows.h>
#include <math.h>
#include <strsafe.h>

/*
    Configuration
*/
#define _R_BUFFER_LENGTH 8192

#ifndef _APP_ALWAYSONTOP
#define _APP_ALWAYSONTOP false
#endif // _APP_ALWAYSONTOP
#define PREVIEW_MAX_WIDTH 1090
#define PREVIEW_MAX_HEIGHT 600

/*
    Callback message codes
*/
#define RM_INITIALIZE (WM_USER + 1)
#define RM_UNINITIALIZE (WM_USER + 2)
#define RM_CLOSE (WM_USER + 3)
#define RM_MESSAGE (WM_USER + 4)
#define RM_LOCALIZE (WM_USER + 5)
#define RM_ARGUMENTS (WM_USER + 6)
