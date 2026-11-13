#pragma once
#include <windows.h>
namespace kb {
inline constexpr UINT kRemotePageAppearance = WM_APP + 71;
HWND CreateRemoteSettingsPage(HWND parent);
}
