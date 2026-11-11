#include "app/resource_policy.hpp"
#include "app/desktop_window_visibility.hpp"
#include <iostream>
#include <stdexcept>
using namespace endfield::app;
void check(bool v,const char* what){if(!v)throw std::runtime_error(what);}
int main(){try{
    auto settings=ehud::data::Settings::defaults();auto p=ResourcePolicy::from(settings);
    check(p.releaseSeconds==1&&p.pauseTelemetry&&p.frameRate==60&&p.wakeIdleSeconds==30,"Resource-saving defaults and the desktop-away threshold");
    check(p.observesPresence(false),"A hidden startup observes the desktop before any automatic hide has happened");
    DesktopPresence state;using A=DesktopPresence::Action;state.opened(100);
    check(state.sample(p,100,500,false,true,true,true)==A::none,"A manual desktop opening has a fresh grace period");
    check(state.sample(p,160,59,false,true,true,true)==A::none,"Keyboard input prevents idle hiding");
    check(state.sample(p,161,60,false,true,true,false)==A::none,"A modal or unfinished edit blocks automatic hiding");
    check(state.sample(p,162,60,false,true,true,true)==A::none,"Resuming after a modal starts a fresh desktop interval");
    check(state.sample(p,221,600,false,true,true,true)==A::none,"Desktop interval must reach the selected delay");
    check(state.sample(p,222,600,false,true,true,true)==A::hide,"Only an idle desktop HUD hides");
    check(state.sample(p,223,0,true,false,false,true)==A::none,"Moving over another app never steals focus");
    check(state.sample(p,224,0,false,true,false,true)==A::none,"A stationary desktop pointer never reveals");
    check(state.sample(p,225,0,true,true,false,true)==A::none,"Ordinary motion after leaving another app never summons the HUD");
    check(state.sample(p,255,30,false,true,false,true)==A::none&&state.wakeArmed(),"Desktop inactivity arms the next mouse movement without showing the HUD");
    check(state.sample(p,256,0,true,true,false,true)==A::reveal,"The first desktop movement after the away interval reveals");
    state.opened(256);state.dismissed();
    check(state.sample(p,257,0,true,true,false,true)==A::none,"A manual close does not immediately reopen on mouse movement");
    DesktopPresence app;app.opened(0);
    check(app.sample(p,1000,1000,false,false,true,true)==A::none,"An idle HUD over another app never hides");
    check(app.sample(p,1001,1001,false,true,true,true)==A::none,"Returning to the desktop starts its own interval");
    check(app.sample(p,1050,1050,false,false,true,true)==A::none,"Leaving the desktop interrupts the interval");
    check(app.sample(p,1060,1060,false,true,true,true)==A::none,"Returning again does not reuse time spent in an app");
    check(app.sample(p,1119,1119,false,true,true,true)==A::none,"A full desktop interval is still required");
    check(app.sample(p,1120,1120,false,true,true,true)==A::hide,"A full desktop interval permits hiding");
    for(const auto seconds:{3.,5.,10.}){
        DesktopPresence quick;p.idleSeconds=seconds;quick.opened(0);
        check(quick.sample(p,0,0,false,true,true,true)==A::none,"Begin a short desktop timeout");
        check(quick.sample(p,seconds-.1,seconds,false,true,true,true)==A::none,"Short timeout does not fire early");
        check(quick.sample(p,seconds,seconds,false,true,true,true)==A::hide,"Each short timeout fires at its boundary");
    }
    p=ResourcePolicy::from(settings);
    DesktopPresence startup;
    check(startup.sample(p,0,0,true,true,false,true)==A::none,"Startup mouse movement alone never reveals");
    check(startup.sample(p,29,29,false,true,false,true)==A::none&&!startup.wakeArmed(),"A hidden startup waits the entire desktop-away interval");
    check(startup.sample(p,30,30,false,true,false,true)==A::none&&startup.wakeArmed(),"A hidden startup can arm without an earlier visible HUD");
    check(startup.sample(p,31,0,true,true,false,true)==A::reveal,"A hidden startup reveals on the first movement after inactivity");
    DesktopPresence moving;
    for(const auto time:{0.,10.,20.,40.})check(moving.sample(p,time,0,true,true,false,true)==A::none&&!moving.wakeArmed(),"Continuous desktop movement never arms wakeup");
    check(moving.sample(p,70,30,false,true,false,true)==A::none&&moving.wakeArmed(),"Stopping movement for the away interval arms wakeup");
    check(moving.sample(p,71,0,false,true,false,true)==A::none&&!moving.wakeArmed(),"Keyboard activity consumes the away state without revealing");
    check(moving.sample(p,72,0,true,true,false,true)==A::none,"Mouse movement immediately after keyboard activity does not reveal");
    DesktopPresence earlyHide;p.idleSeconds=5;earlyHide.opened(0);
    check(earlyHide.sample(p,0,0,false,true,true,true)==A::none,"Start a short hide interval");
    check(earlyHide.sample(p,5,5,false,true,true,true)==A::hide&&!earlyHide.wakeArmed(),"A five-second hide does not prematurely arm a thirty-second wakeup");
    check(earlyHide.sample(p,30,30,false,true,false,true)==A::none&&earlyHide.wakeArmed(),"Automatic hiding continues the same desktop-away interval");
    check(earlyHide.sample(p,31,0,true,true,false,true)==A::reveal,"The first movement after the full away interval reveals");
    p.desktopResume=false;
    check(!p.observesPresence(false)&&p.observesPresence(true),"Resume-off stops hidden polling while retaining shown idle hiding");
    check(startup.sample(p,100,0,true,true,false,true)==A::none,"Resume can be disabled");
    p.idleSeconds=0;startup.sample(p,101,900,false,false,true,true);
    check(!startup.wakeArmed()&&!p.observesPresence(true),"Disabling automatic hiding cancels pending wakeup and polling");
    settings.set("windowsGraphicsReleaseSeconds",-4);settings.set("windowsFrameRateLimit",9999);
    p=ResourcePolicy::from(settings);check(p.releaseSeconds==0&&p.frameRate==240,"Stored numeric preferences are bounded");
    check(settings.restoring(ehud::data::SettingsScope::display).number("windowsFrameRateLimit")==60,"Display restore includes fork performance options");
#ifdef _WIN32
    struct HiddenWindow { HWND value{};~HiddenWindow(){if(value)DestroyWindow(value);} } overlay;
    overlay.value=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"STATIC",L"Desktop visibility regression",WS_POPUP,-32000,-32000,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(overlay.value!=nullptr,"Create an offscreen helper; no window is shown");
    check(SetLayeredWindowAttributes(overlay.value,0,255,LWA_ALPHA)&&!desktopNonBlockingOverlay(overlay.value),"A normal layered app must still block desktop presence");
    check(SetLayeredWindowAttributes(overlay.value,0,1,LWA_ALPHA)&&!desktopNonBlockingOverlay(overlay.value),"A faint but visible app must still block desktop presence");
    check(SetLayeredWindowAttributes(overlay.value,0,0,LWA_ALPHA)&&desktopNonBlockingOverlay(overlay.value),"A zero-alpha driver overlay does not obscure the desktop");
    check(SetLayeredWindowAttributes(overlay.value,0,255,LWA_ALPHA)!=FALSE,"Restore the synthetic overlay's alpha");
    SetWindowLongPtrW(overlay.value,GWL_EXSTYLE,WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT);
    check(desktopNonBlockingOverlay(overlay.value)&&!IsWindowVisible(overlay.value),"A click-through helper is ignored without displaying a test window");
#endif
    std::cout<<"Resource policy contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
