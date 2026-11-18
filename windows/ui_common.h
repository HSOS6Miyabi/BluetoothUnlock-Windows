#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>

namespace kb::ui {
struct Palette {
    COLORREF background, panel, field, text, muted, accent, accentText, border, subtle;
    bool dark;
};

inline bool SystemDark() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) return false;
    DWORD light = 1;
    DWORD size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) != ERROR_SUCCESS)
        return false;
    return light == 0;
}

inline Palette Colors() {
    if (SystemDark()) return {
        RGB(15, 22, 34), RGB(25, 36, 52), RGB(37, 51, 70),
        RGB(240, 245, 251), RGB(174, 189, 207), RGB(108, 169, 255),
        RGB(9, 26, 47), RGB(57, 74, 98), RGB(32, 45, 64), true};
    return {
        RGB(247, 249, 252), RGB(255, 255, 255), RGB(243, 247, 252),
        RGB(30, 42, 60), RGB(92, 108, 130), RGB(39, 105, 213),
        RGB(255, 255, 255), RGB(215, 224, 237), RGB(233, 241, 253), false};
}

inline UINT Dpi(HWND window) {
    const UINT dpi = window ? GetDpiForWindow(window) : GetDpiForSystem();
    return dpi ? dpi : 96;
}
inline int Scale(UINT dpi, int logicalPixels) {
    return MulDiv(logicalPixels, static_cast<int>(dpi ? dpi : 96), 96);
}
inline HFONT MakeFont(UINT dpi, int logicalPixels, int weight = FW_NORMAL) {
    return CreateFontW(-Scale(dpi, logicalPixels), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}
inline void Titlebar(HWND window, bool dark) {
    const BOOL enabled = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &enabled, sizeof(enabled));
}

inline HRESULT CALLBACK TaskDialogTheme(HWND dialog, UINT notification, WPARAM, LPARAM, LONG_PTR) {
    if (notification == TDN_CREATED) Titlebar(dialog, SystemDark());
    return S_OK;
}
inline int ShowTaskDialog(HWND owner, const wchar_t* title, const wchar_t* heading,
                          const wchar_t* detail, PCWSTR icon,
                          unsigned int buttons = TDCBF_OK_BUTTON,
                          int defaultButton = IDOK) {
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    UINT flags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    if (owner) flags |= TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwFlags = static_cast<TASKDIALOG_FLAGS>(flags);
    config.dwCommonButtons = static_cast<TASKDIALOG_COMMON_BUTTON_FLAGS>(buttons);
    config.pszWindowTitle = title;
    config.pszMainInstruction = heading;
    config.pszContent = detail;
    config.pszMainIcon = icon;
    config.nDefaultButton = defaultButton;
    config.pfCallback = TaskDialogTheme;
    int selected = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&config, &selected, nullptr, nullptr))) return IDCANCEL;
    return selected;
}

inline void Rounded(HDC dc, RECT rect, COLORREF color, int radius) {
    const HBRUSH brush = CreateSolidBrush(color);
    const HPEN pen = CreatePen(PS_SOLID, 1, color);
    auto oldBrush = SelectObject(dc, brush);
    auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}
} // namespace kb::ui
