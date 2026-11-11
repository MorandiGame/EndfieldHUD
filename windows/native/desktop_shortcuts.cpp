#include "native/desktop_shortcuts.hpp"
#include <algorithm>
#include <set>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace endfield::native {
namespace {
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string key(const std::filesystem::path& path){
#ifdef _WIN32
    auto value=path.lexically_normal().native();std::replace(value.begin(),value.end(),L'/',L'\\');
    std::wstring folded(value.size(),0);
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),folded.data(),static_cast<int>(folded.size()),nullptr,nullptr,0))return {};
    return utf8(std::filesystem::path(folded));
#else
    return utf8(path.lexically_normal());
#endif
}
std::string identity(const std::filesystem::path& path){
#ifdef _WIN32
    const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return {};
    BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(handle,&info)!=FALSE;CloseHandle(handle);
    if(ok&&(info.nFileIndexHigh||info.nFileIndexLow))return std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string(info.nFileIndexHigh)+":"+std::to_string(info.nFileIndexLow);
#endif
    return {};
}
}
modules::DesktopShortcutSnapshot scanDesktopShortcuts(std::span<const std::filesystem::path> roots){
    modules::DesktopShortcutSnapshot out;std::set<std::string> seen;
    if(roots.empty()){out.complete=false;return out;}
    for(const auto& root:roots){
        std::error_code error;std::filesystem::directory_iterator it(root,error),end;
        if(error){out.complete=false;continue;}
        for(;it!=end;it.increment(error)){
            if(error){out.complete=false;break;}
            const auto path=it->path();auto extension=utf8(path.extension());
            for(auto& c:extension)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            if(extension!=".lnk"&&extension!=".url")continue;
            if(!it->is_regular_file(error)||error){out.complete=false;error.clear();continue;}
            const auto length=it->file_size(error);if(error){out.complete=false;error.clear();continue;}
            const auto stamp=it->last_write_time(error);if(error){out.complete=false;error.clear();continue;}
            const auto sourceKey=key(path);if(sourceKey.empty()||!seen.insert(sourceKey).second)continue;
            if(out.entries.size()>=512){out.complete=false;return out;}
            out.entries.push_back({utf8(path),sourceKey,utf8(path.stem()),std::to_string(length)+":"+std::to_string(stamp.time_since_epoch().count()),identity(path)});
        }
        if(error)out.complete=false;
    }
    std::sort(out.entries.begin(),out.entries.end(),[](const auto& a,const auto& b){return a.key<b.key;});
    return out;
}
#ifdef _WIN32
modules::DesktopShortcutSnapshot windowsDesktopShortcuts(){
    std::vector<std::filesystem::path> roots;
    for(const auto* id:{&FOLDERID_Desktop,&FOLDERID_PublicDesktop}){
        PWSTR path{};const auto hr=SHGetKnownFolderPath(*id,KF_FLAG_DONT_VERIFY,nullptr,&path);
        if(FAILED(hr)){CoTaskMemFree(path);return {{},false};}
        roots.emplace_back(path);CoTaskMemFree(path);
    }
    return scanDesktopShortcuts(roots);
}
#endif
}
