#pragma once
#include <windows.h>
namespace kb {
inline constexpr UINT kServicePageAppearance = WM_APP + 72;
HWND CreateServiceSettingsPage(HWND parent);
}
