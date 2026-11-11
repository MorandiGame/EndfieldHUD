#include "app/resource_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace endfield::app;
void check(bool v,const char* what){if(!v)throw std::runtime_error(what);}
int main(){try{
    auto settings=ehud::data::Settings::defaults();auto p=ResourcePolicy::from(settings);
    check(p.releaseSeconds==1&&p.pauseTelemetry&&p.frameRate==60,"Resource-saving defaults");
    DesktopPresence state;using A=DesktopPresence::Action;state.opened(100);
    check(state.sample(p,100,500,false,true,true,true)==A::none,"A manual desktop opening has a fresh grace period");
    check(state.sample(p,160,59,false,true,true,true)==A::none,"Keyboard input prevents idle hiding");
    check(state.sample(p,161,60,false,true,true,false)==A::none,"A modal or unfinished edit blocks automatic hiding");
    check(state.sample(p,162,60,false,true,true,true)==A::none,"Resuming after a modal starts a fresh desktop interval");
    check(state.sample(p,221,600,false,true,true,true)==A::none,"Desktop interval must reach the selected delay");
    check(state.sample(p,222,600,false,true,true,true)==A::hide,"Only an idle desktop HUD hides");
    check(state.sample(p,223,0,true,false,false,true)==A::none,"Moving over another app never steals focus");
    check(state.sample(p,224,0,false,true,false,true)==A::none,"A stationary desktop pointer never reveals");
    check(state.sample(p,225,0,true,true,false,true)==A::reveal,"Desktop movement reveals an automatically hidden HUD");
    state.opened(225);state.dismissed();
    check(state.sample(p,226,0,true,true,false,true)==A::none,"A manual close stays closed");
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
    state.hiddenAutomatically();p.desktopResume=false;
    check(state.sample(p,227,0,true,true,false,true)==A::none,"Resume can be disabled");
    p.idleSeconds=0;state.sample(p,228,900,false,false,true,true);
    check(!state.automatic(),"Disabling automatic hiding cancels pending resume");
    settings.set("windowsGraphicsReleaseSeconds",-4);settings.set("windowsFrameRateLimit",9999);
    p=ResourcePolicy::from(settings);check(p.releaseSeconds==0&&p.frameRate==240,"Stored numeric preferences are bounded");
    check(settings.restoring(ehud::data::SettingsScope::display).number("windowsFrameRateLimit")==60,"Display restore includes fork performance options");
    std::cout<<"Resource policy contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
