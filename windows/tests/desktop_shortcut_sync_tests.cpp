#include "modules/desktop_shortcut_sync.hpp"
#include "modules/app_shortcut_tasks.hpp"
#include "native/desktop_shortcuts.hpp"
#include "native/app_shortcut_icons.hpp"
#include "native/app_shortcut_text.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#endif

namespace {
namespace m=endfield::modules;namespace n=endfield::native;namespace a=endfield::app;namespace fs=std::filesystem;
using J=m::ShortcutJson;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Temp {
    fs::path root=fs::temp_directory_path()/fs::path("EndfieldHUD-desktop-sync-"+ehud::data::makeUUID());
    Temp(){fs::create_directories(root);}
    ~Temp(){std::error_code error;const auto target=fs::weakly_canonical(root,error);if(error)return;
        const auto parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("EndfieldHUD-desktop-sync-"))fs::remove_all(target,error);}
};
m::ShortcutTextRules rules(){return n::nativeShortcutTextRules();}
m::DesktopShortcutEntry entry(std::string name,std::string version="1",std::string identity={}){
    const auto path="C:\\Desktop\\"+name+".lnk";return {path,path,name,version,identity};
}
m::ShortcutCandidate candidate(const std::string& path){
    return {"Original",{},J::Object{{"referencePlatform","windows"},{"windowsTarget",J::Object{{"kind","shellLink"},{"path",path},{"applicationKey",path}}}}};
}
void incremental(){
    m::ShortcutFile file;unsigned inspections{};
    const auto inspect=[&](const std::string& path){++inspections;if(path.find("Broken")!=std::string::npos)throw std::runtime_error("Unavailable");return candidate(path);};
    const auto id=[]{return ehud::data::makeUUID();};
    m::DesktopShortcutSnapshot snapshot{{entry("Alpha","1","file-A"),entry("Beta")},true};
    auto result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,1,rules());
    check(result.added==2&&result.inspected==2&&file.items.size()==2,"First startup imports the current desktop once");
    const auto original=file;
    result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,2,rules());
    check(!result.changed&&result.inspected==0&&file==original&&inspections==2,"Unchanged startup neither resolves old links nor rewrites records");
    file.items[0].name="My custom name";file.items[0].iconPreset="terminal";
    const auto alphaID=file.items[0].id;
    snapshot.entries[0].fingerprint="2";
    result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,3,rules());
    check(result.updated==1&&result.inspected==1&&file.items[0].name=="My custom name"&&file.items[0].iconPreset=="terminal"&&file.items[0].id==alphaID,"A changed desktop link keeps the user's name, preset, identity and position");
    auto renamed=entry("Renamed","3","file-A");snapshot.entries[0]=renamed;
    result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,4,rules());
    check(result.updated==1&&result.removed==0&&file.items[0].id==alphaID&&file.items[0].iconPreset=="terminal","A rename follows the file identity without recreating the shortcut");
    snapshot.entries[0]=entry("Broken","4","file-A");
    result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,5,rules());
    check(result.failed==1&&result.removed==0&&file.items[0].id==alphaID,"Failed reinspection never deletes the prior record");
    snapshot.entries[0]=renamed;
    auto manual=m::ShortcutRecord{};manual.id=id();manual.name=manual.originalName="Manual";manual.iconPreset="star";manual.locator=candidate("C:\\Manual.lnk").locator;
    file.items.push_back(manual);snapshot.entries.erase(snapshot.entries.begin()+1);
    result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,6,rules());
    check(result.removed==1&&file.items.size()==2&&file.items.back()==manual,"Removed desktop links are pruned while manual shortcuts stay");
    const auto kept=file;auto unavailable=snapshot;unavailable.complete=false;unavailable.entries.clear();
    result=m::synchronizeDesktopShortcuts(file,unavailable,inspect,id,7,rules());
    check(!result.changed&&file==kept,"An unreadable desktop never wipes the saved list");
    check(m::removeShortcut(file,alphaID),"A synchronized item can be manually removed");
    file=m::decodeShortcuts(m::encodeShortcuts(file,rules()),rules());
    result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,8,rules());
    check(result.added==0&&result.inspected==0&&file.items.size()==1,"The saved removal preference prevents reimport on every startup");
    snapshot.entries.clear();m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,9,rules());
    snapshot.entries.push_back(renamed);result=m::synchronizeDesktopShortcuts(file,snapshot,inspect,id,10,rules());
    check(result.added==1,"A shortcut removed from the desktop and later recreated can be imported again");
}
void cacheAndScan(){
    Temp temp;const auto desktop=temp.root/"Desktop";fs::create_directories(desktop/"nested");
    const auto link=desktop/"Alpha.lnk";std::ofstream(link)<<"one";
    std::ofstream(desktop/"Game.url")<<"[InternetShortcut]\nURL=steam://rungameid/123\n";
    std::ofstream(desktop/"notes.txt")<<"not an application";std::ofstream(desktop/"nested"/"Nested.lnk")<<"nested";
    std::vector<fs::path> roots{desktop};const auto snapshot=n::scanDesktopShortcuts(roots);
    check(snapshot.complete&&snapshot.entries.size()==2,"A bounded nonrecursive scan includes .lnk and .url, excluding ordinary files");
    roots.push_back(temp.root/"OfflineDesktop");check(!n::scanDesktopShortcuts(roots).complete,"A missing desktop directory is reported as incomplete");
    unsigned extractions{};
    const n::AppShortcutIconExtractor extractor=[&](const auto&,unsigned side)->std::optional<n::AppShortcutIconPixels>{
        ++extractions;n::AppShortcutIconPixels pixels{side,side,std::vector<std::uint8_t>(side*side*4)};
        for(std::size_t i=0;i<pixels.straightRGBA.size();i+=4){pixels.straightRGBA[i]=220;pixels.straightRGBA[i+1]=30;pixels.straightRGBA[i+2]=90;pixels.straightRGBA[i+3]=127;}return pixels;
    };
    const auto pathBytes=link.u8string();n::AppShortcutSelection selected{n::AppShortcutSelectionKind::file,{reinterpret_cast<const char*>(pathBytes.data()),pathBytes.size()}};
    const auto cached=n::cachedAppShortcutIconExtractor(extractor,temp.root/"IconCache");
    auto pixels=cached(selected,96);check(pixels&&extractions==1,"The first extraction creates the external binary icon cache");
    const auto restarted=n::cachedAppShortcutIconExtractor(extractor,temp.root/"IconCache");
    auto reused=restarted(selected,96);check(reused&&extractions==1&&reused->straightRGBA==pixels->straightRGBA,"The next startup reuses cached pixels without extracting the icon again");
    n::monochromeAppShortcutIcon(*reused);
    for(std::size_t i=0;i<reused->straightRGBA.size();i+=4)check(reused->straightRGBA[i]==reused->straightRGBA[i+1]&&reused->straightRGBA[i+1]==reused->straightRGBA[i+2]&&reused->straightRGBA[i+3]==127,"Monochrome conversion preserves transparency");
    std::ofstream(link,std::ios::app)<<" changed";check(restarted(selected,96).has_value()&&extractions==2,"A changed source invalidates only its cached icon");
#ifdef _WIN32
    const auto& internet=*std::find_if(snapshot.entries.begin(),snapshot.entries.end(),[](const auto& e){return e.path.ends_with(".url");});
    const auto inspected=n::NativeAppShortcutService(n::windowsAppShortcutOS()).inspect({n::AppShortcutSelectionKind::file,internet.path});
    check(inspected.locator["windowsTarget"]["kind"]==J("shellLink"),"Internet/game shortcuts use the normal explicit shell launch path");
#endif
}
void startupTask(){
    Temp temp;unsigned scans{},inspections{},launches{};
    n::AppShortcutOS os;
    os.inspect=[&](const n::AppShortcutSelection& s){++inspections;n::AppShortcutResolved r;r.selection=s;r.name="App";r.applicationKey=s.value;r.selectedFileKey="identity";r.launchKind=n::AppShortcutLaunchKind::shellLink;return r;};
    os.launch=[&](const auto&,auto){++launches;return n::AppShortcutLaunchReceipt{};};
    a::UtilityExecutor queue([]{});std::vector<m::ShortcutTaskResult> results;
    const auto scan=[&]{++scans;return m::DesktopShortcutSnapshot{{entry("Alpha")},true};};
    const auto drain=[&]{for(unsigned i=0;i<4;++i){queue.waitIdle();queue.drain();}};
    {
        m::AppShortcutTasks tasks(queue,temp.root/"Shortcuts",rules(),os,[&](auto r){results.push_back(std::move(r));},{},scan);
        check(scans==0,"Constructing the owner performs no filesystem scan");
        m::ShortcutTask load;load.kind=m::ShortcutTaskKind::load;load.token=1;tasks.submit(load);drain();
        load.token=2;tasks.submit(load);drain();
        check(scans==1&&inspections==1&&results.back().file->items.size()==1,"Repeated owner loads during one process do not resynchronize the desktop");
    }
    {
        m::AppShortcutTasks tasks(queue,temp.root/"Shortcuts",rules(),os,[&](auto r){results.push_back(std::move(r));},{},scan);
        m::ShortcutTask load;load.kind=m::ShortcutTaskKind::load;load.token=1;tasks.submit(load);drain();
        check(scans==2&&inspections==1&&results.back().desktopSync&&results.back().desktopSync->inspected==0,"The next process compares metadata and skips unchanged existing shortcuts");
        check(launches==0,"Startup synchronization never launches applications");
    }
}
#ifdef _WIN32
void desktopSummary(){
    const auto snapshot=n::windowsDesktopShortcuts();check(snapshot.complete,"Read both current desktop directories");
    m::ShortcutFile file;n::NativeAppShortcutService service(n::windowsAppShortcutOS());
    const auto inspect=[&](const auto& path){return service.inspect({n::AppShortcutSelectionKind::file,path});};
    const auto uuid=[]{return ehud::data::makeUUID();};
    const auto first=m::synchronizeDesktopShortcuts(file,snapshot,inspect,uuid,1,rules());
    const auto second=m::synchronizeDesktopShortcuts(file,n::windowsDesktopShortcuts(),inspect,uuid,2,rules());
    check(second.inspected==0,"The real unchanged desktop requires no repeated target inspection");
    Temp temporaryCache;unsigned iconReads{},icons{};const auto native=n::windowsAppShortcutIconExtractor();
    const auto cached=n::cachedAppShortcutIconExtractor([&](const auto& s,unsigned size){++iconReads;return native(s,size);},temporaryCache.root/"Icons");
    for(const auto& item:file.items)if(const auto selected=n::appShortcutIconSelection(item.locator))if(auto icon=cached(*selected,96)){++icons;n::monochromeAppShortcutIcon(*icon);}
    const auto firstReads=iconReads;
    for(const auto& item:file.items)if(const auto selected=n::appShortcutIconSelection(item.locator))(void)cached(*selected,96);
    check(iconReads-firstReads==file.items.size()-icons,"Every successfully extracted desktop icon is reused on the next read");
    std::cout<<"Desktop links: "<<snapshot.entries.size()<<"; imported: "<<first.added<<"; unavailable: "<<first.failed<<"; unchanged inspections: "<<second.inspected
        <<"; original icons: "<<icons<<"; cached icon re-extractions: "<<(iconReads-firstReads)<<"\n";
}
#endif
}
int main(int argc,char** argv){try{
#ifdef _WIN32
    struct Apartment{HRESULT hr{CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)};~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment;
    check(SUCCEEDED(apartment.hr),"Initialize the CLI-only desktop test apartment");
    if(argc==2&&std::string_view(argv[1])=="--desktop-summary"){desktopSummary();return 0;}
#endif
    incremental();cacheAndScan();startupTask();std::cout<<checks<<" desktop synchronization checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Desktop synchronization: "<<e.what()<<'\n';return 1;}}
