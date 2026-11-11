#include "app/resource_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace endfield::app;
void check(bool v,const char* what){if(!v)throw std::runtime_error(what);}
int main(){try{
    auto settings=ehud::data::Settings::defaults();auto p=ResourcePolicy::from(settings);
    check(p.releaseSeconds==1&&p.pauseTelemetry&&p.frameRate==60,"Resource-saving defaults");
    DesktopPresence state;using A=DesktopPresence::Action;state.opened(100);
    check(state.sample(p,101,500,false,false,true,true)==A::none,"A manual opening has a fresh grace period");
    check(state.sample(p,160,59,false,false,true,true)==A::none,"Keyboard input prevents idle hiding");
    check(state.sample(p,161,60,false,false,true,false)==A::none,"A modal or unfinished edit blocks automatic hiding");
    check(state.sample(p,161,60,false,false,true,true)==A::hide,"Idle HUD hides");
    check(state.sample(p,162,0,true,false,false,true)==A::none,"Moving over another app never steals focus");
    check(state.sample(p,163,0,false,true,false,true)==A::none,"A stationary desktop pointer never reveals");
    check(state.sample(p,164,0,true,true,false,true)==A::reveal,"Desktop movement reveals an automatically hidden HUD");
    state.opened(164);state.dismissed();
    check(state.sample(p,165,0,true,true,false,true)==A::none,"A manual close stays closed");
    state.hiddenAutomatically();p.desktopResume=false;
    check(state.sample(p,166,0,true,true,false,true)==A::none,"Resume can be disabled");
    p.idleSeconds=0;state.sample(p,167,900,false,false,true,true);
    check(!state.automatic(),"Disabling automatic hiding cancels pending resume");
    settings.set("windowsGraphicsReleaseSeconds",-4);settings.set("windowsFrameRateLimit",9999);
    p=ResourcePolicy::from(settings);check(p.releaseSeconds==0&&p.frameRate==240,"Stored numeric preferences are bounded");
    check(settings.restoring(ehud::data::SettingsScope::display).number("windowsFrameRateLimit")==60,"Display restore includes fork performance options");
    std::cout<<"Resource policy contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
