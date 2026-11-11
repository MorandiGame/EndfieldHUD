#include "app/application_impl.hpp"
#include "app/desktop_window_visibility.hpp"
#include <cwchar>
#include <dwmapi.h>

namespace endfield::app {
namespace {
bool desktopWindow(HWND window) {
    if(!window)return false;
    const auto root=GetAncestor(window,GA_ROOT);
    if(root==GetShellWindow())return true;
    wchar_t name[64]{};
    if(!GetClassNameW(root,name,64))return false;
    // Wallpaper Engine attaches its wallpaper beneath Explorer's WorkerW.
    return std::wcscmp(name,L"Progman")==0||std::wcscmp(name,L"WorkerW")==0;
}
bool ownWindow(HWND window) {
    DWORD process{};
    return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
}
bool taskbarWindow(HWND window) {
    wchar_t name[64]{};
    if(!window||!GetClassNameW(GetAncestor(window,GA_ROOT),name,64))return false;
    return std::wcscmp(name,L"Shell_TrayWnd")==0||std::wcscmp(name,L"Shell_SecondaryTrayWnd")==0;
}
bool desktopContext(HWND hud, POINT mouse, bool shown) {
    const auto foreground=GetForegroundWindow(), pointed=WindowFromPoint(mouse);
    if(!desktopWindow(foreground)&&!taskbarWindow(foreground)&&!(shown&&ownWindow(foreground)))return false;
    // Taskbar use must never summon the HUD. Hiding only cares about the
    // backdrop, so leaving a stationary pointer on the bar is still idle.
    // Look through our own overlay. An app underneath any part of the HUD
    // means its background is not the desktop; other monitors do not count.
    RECT area{};MONITORINFO monitor{sizeof(monitor)};
    if(!GetWindowRect(hud,&area)||!GetMonitorInfoW(MonitorFromWindow(hud,MONITOR_DEFAULTTONEAREST),&monitor))return false;
    if(!IntersectRect(&area,&area,&monitor.rcWork))return false;
    if(!shown&&!desktopWindow(pointed)&&!(PtInRect(&area,mouse)&&desktopNonBlockingOverlay(pointed)))return false;
    struct Probe {RECT area;unsigned visited{};bool blocked{};} probe{area};
    const auto visit=[](HWND window,LPARAM value)->BOOL {
        auto& p=*reinterpret_cast<Probe*>(value);
        if(++p.visited>4096){p.blocked=true;return FALSE;}
        if(ownWindow(window)||!IsWindowVisible(window)||IsIconic(window)||desktopWindow(window)||desktopNonBlockingOverlay(window))return TRUE;
        DWORD cloaked{};
        if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return TRUE;
        if(taskbarWindow(window))return TRUE;
        RECT bounds{},overlap{};
        if(GetWindowRect(window,&bounds)&&IntersectRect(&overlap,&p.area,&bounds)){p.blocked=true;return FALSE;}
        return TRUE;
    };
    return EnumWindows(visit,reinterpret_cast<LPARAM>(&probe))&&!probe.blocked;
}
}
void Application::Impl::scheduleDesktopPresence(double time){
    const auto policy=ResourcePolicy::from(configuration);
    const bool shown=session->phase()!=core::VisibilityPhase::concealed;
    const bool wanted=production()&&!suspensions&&!projectionHandoff.active()&&!projectionShown()&&
        policy.observesPresence(shown);
    if(!wanted){presenceWake.reset();desktopPresence.pause();return;}
    if(!presenceWake)presenceWake=time+1;
}
void Application::Impl::sampleDesktopPresence(double time){
    if(!production()||suspensions||projectionHandoff.active()||projectionShown())return;
    LASTINPUTINFO input{sizeof(input)};POINT mouse{};
    if(!GetLastInputInfo(&input)||!GetCursorPos(&mouse))return;
    const bool moved=presenceMouseKnown&&(mouse.x!=presenceMouseX||mouse.y!=presenceMouseY);
    presenceMouseX=mouse.x;presenceMouseY=mouse.y;presenceMouseKnown=true;
    const double idle=static_cast<DWORD>(GetTickCount()-input.dwTime)/1000.;
    const bool shown=lifecycle.phase()==core::SystemOverlayPhase::open;
    const bool hidden=lifecycle.phase()==core::SystemOverlayPhase::closed;
    const auto picker=mediaPicker?mediaPicker->stats():gpu::ShelfPickerStats{};
    const bool canAct=(shown||hidden)&&!closing&&!pendingClose&&!quitRequested&&!quitPromptOpen&&
        !picker.queued&&!picker.presenting&&pickerOwner==PickerOwner::none&&!lifecycle.shelfDragActive()&&!pointerLocked();
    const bool desktop=canAct&&desktopContext(static_cast<HWND>(host.hwnd()),mouse,shown);
    const auto action=desktopPresence.sample(ResourcePolicy::from(configuration),time,idle,moved,desktop,shown,canAct);
    if(action==DesktopPresence::Action::hide){
        const auto desktopInterval=desktopPresence;
        perform(lifecycle.close(),time);
        // close() resets presence for a manual close. An accepted idle close
        // continues the same uninterrupted desktop-away interval.
        if(closing||session->phase()==core::VisibilityPhase::concealed){
            desktopPresence=desktopInterval;
        }else desktopPresence.dismissed();
    }else if(action==DesktopPresence::Action::reveal){
        perform(lifecycle.openOverlay(),time);
    }
}
}
