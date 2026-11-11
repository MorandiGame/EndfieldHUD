#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::app {
// Capture/driver overlays can keep a visible, full-screen HWND while its
// global alpha is zero. It neither replaces nor obscures the desktop.
// Do not exempt a normal layered app just because it uses transparency.
inline bool desktopNonBlockingOverlay(HWND window) {
    const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if(!(style&WS_EX_LAYERED))return false;
    if(style&WS_EX_TRANSPARENT)return true;
    COLORREF key{};BYTE alpha{};DWORD flags{};
    return GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&
           (flags&LWA_ALPHA)&&alpha==0;
}
}
#endif
