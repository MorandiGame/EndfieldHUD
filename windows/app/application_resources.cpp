#include "app/application_impl.hpp"
#include <cwchar>

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
}
void Application::Impl::scheduleDesktopPresence(double time){
    const auto policy=ResourcePolicy::from(configuration);
    const bool shown=session->phase()!=core::VisibilityPhase::concealed;
    const bool wanted=production()&&!suspensions&&!projectionHandoff.active()&&!projectionShown()&&
        policy.idleSeconds>0&&(shown||(policy.desktopResume&&desktopPresence.automatic()));
    if(!wanted){presenceWake.reset();return;}
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
    const bool desktop=hidden&&moved&&desktopWindow(WindowFromPoint(mouse))&&desktopWindow(GetForegroundWindow());
    const auto action=desktopPresence.sample(ResourcePolicy::from(configuration),time,idle,moved,desktop,shown,canAct);
    if(action==DesktopPresence::Action::hide){
        perform(lifecycle.close(),time);
        // close() clears automatic intent for ordinary user closes. Re-arm only
        // after the close was accepted; a refusing editor remains visible.
        if(closing||session->phase()==core::VisibilityPhase::concealed){
            desktopPresence.hiddenAutomatically();
        }else desktopPresence.dismissed();
    }else if(action==DesktopPresence::Action::reveal){
        perform(lifecycle.openOverlay(),time);
    }
}
}
