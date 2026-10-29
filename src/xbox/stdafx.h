// Project Kaos-Byte2.0v (The Static Rift) - common includes for the Xbox 360 plugin.
// Requires the Xbox 360 XDK (xtl.h) and xkelib (kernel/XAM export declarations).
#pragma once

#include <xtl.h>
#include <ppcintrinsics.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "xkelib.h"

#include "../core/portal_core.h"

// Plugin-wide constants.
#define SKYPORTAL_VERSION_STRING "2.0"
#define SKYPORTAL_PRODUCT "Project Kaos-Byte2.0v (build 9)"
#define SKYPORTAL_VERSION_WSTRING L"1.0.0"

// XAM ordinals hooked in the game (checked against xkelib's xamext .def files and Xenia Canary).
#define XAM_ORD_XamInputGetState 401                  // 0x191
#define XAM_ORD_XamInputNonControllerGetRaw 1185      // 0x4A1
#define XAM_ORD_XamInputNonControllerSetRaw 1186      // 0x4A2
#define XAM_ORD_XamInputNonControllerGetRawEx 1322    // 0x52A (newer dashboards only)
#define XAM_ORD_XamInputNonControllerSetRawEx 1323    // 0x52B (newer dashboards only)

// XEX optional header holding the import descriptor.
#define XEX_HEADER_IMPORT_LIBRARIES 0x000103FF
