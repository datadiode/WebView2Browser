// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <atlstr.h>
#include <strsafe.h>
#include <wil/result.h>
#include <wil/com.h>
#include <windows.h>
#include <wrl.h>
// C RunTime Header Files
#include <nlohmann/json.hpp>
#include <malloc.h>
#include <memory.h>
#include <memory>
#include <stdlib.h>
#include <tchar.h>
#include <map>
#include <codecvt> // codecvt_utf8
#include <locale>  // wstring_convert

// App specific includes
#include "resource.h"
#include "webview2.h"

#define DEFAULT_DPI 96
#define MIN_WINDOW_WIDTH 510
#define MIN_WINDOW_HEIGHT 75
#define MAX_LOADSTRING 256

#define INVALID_TAB_ID 0
#define MG_NAVIGATE 1
#define MG_UPDATE_URI 2
#define MG_GO_FORWARD 3
#define MG_GO_BACK 4
#define MG_NAV_STARTING 5
#define MG_NAV_COMPLETED 6
#define MG_RELOAD 7
#define MG_CANCEL 8
#define MG_CREATE_TAB 10
#define MG_UPDATE_TAB 11
#define MG_SWITCH_TAB 12
#define MG_CLOSE_TAB 13
#define MG_CLOSE_WINDOW 14
#define MG_SHOW_OPTIONS 15
#define MG_HIDE_OPTIONS 16
#define MG_OPTIONS_LOST_FOCUS 17
#define MG_OPTION_SELECTED 18
#define MG_SECURITY_UPDATE 19
#define MG_UPDATE_FAVICON 20
#define MG_GET_SETTINGS 21
#define MG_GET_FAVORITES 22
#define MG_REMOVE_FAVORITE 23
#define MG_CLEAR_CACHE 24
#define MG_CLEAR_COOKIES 25
#define MG_GET_HISTORY 26
#define MG_REMOVE_HISTORY_ITEM 27
#define MG_CLEAR_HISTORY 28
