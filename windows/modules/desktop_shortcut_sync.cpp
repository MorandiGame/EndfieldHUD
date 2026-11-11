#include "modules/desktop_shortcut_sync.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace endfield::modules {
namespace {
using J=ShortcutJson;
std::string string(const J& value,std::string_view key){return value[key].isString()?value[key].string():std::string{};}
J source(const DesktopShortcutEntry& e){return J::Object{{"key",e.key},{"path",e.path},{"fingerprint",e.fingerprint},{"fileID",e.fileID}};}
}
DesktopShortcutSyncResult synchronizeDesktopShortcuts(ShortcutFile& file,const DesktopShortcutSnapshot& snapshot,
    const std::function<ShortcutCandidate(const std::string&)>& inspect,const std::function<std::string()>& newID,
    double now,const ShortcutTextRules& rules){
    DesktopShortcutSyncResult result;
    if(!snapshot.complete||snapshot.entries.size()>512)return result;
    auto next=file;
    std::map<std::string,const DesktopShortcutEntry*> current;
    std::map<std::string,unsigned> identities;
    for(const auto& e:snapshot.entries){
        if(e.key.empty()||e.path.empty()||!current.emplace(e.key,&e).second)throw ShortcutError(ShortcutErrorCode::invalidRecord);
        if(!e.fileID.empty())++identities[e.fileID];
    }
    std::set<std::string> ignored;
    const auto& savedIgnored=std::as_const(file.originalFields)[desktopIgnoredField];
    if(savedIgnored.isArray())for(const auto& key:savedIgnored.array())if(key.isString()&&current.contains(key.string()))ignored.insert(key.string());
    std::set<std::string> handled;
    for(auto& item:next.items){
        const auto& old=std::as_const(item.originalFields)[desktopSourceField];
        const auto oldKey=string(old,"key");if(oldKey.empty())continue;
        const DesktopShortcutEntry* entry{};
        if(const auto it=current.find(oldKey);it!=current.end())entry=it->second;
        else if(const auto identity=string(old,"fileID");!identity.empty()&&identities[identity]==1){
            // A desktop rename keeps the same file identity and the user's icon/order.
            for(const auto& e:snapshot.entries)if(e.fileID==identity&&!handled.contains(e.key)){entry=&e;break;}
        }
        if(!entry)continue;
        handled.insert(oldKey);
        handled.insert(entry->key);
        if(oldKey==entry->key&&string(old,"fingerprint")==entry->fingerprint)continue;
        try{
            ++result.inspected;auto candidate=inspect(entry->path);
            auto updated=item;
            if(updated.name==updated.originalName&&rules.validName(entry->name))updated.name=entry->name;
            updated.originalName=entry->name;updated.locator=std::move(candidate.locator);
            updated.bundleIdentifier=std::move(candidate.bundleIdentifier);
            updated.originalFields[std::string(desktopSourceField)]=source(*entry);
            validateShortcut(updated,rules);item=std::move(updated);++result.updated;
        }catch(const std::exception&){++result.failed;}
    }
    result.removed=std::erase_if(next.items,[&](const auto& item){
        const auto key=string(item.originalFields[desktopSourceField],"key");
        return !key.empty()&&!current.contains(key)&&!handled.contains(key);
    });
    for(const auto& [key,entry]:current){
        if(handled.contains(key)||ignored.contains(key)||next.items.size()>=512)continue;
        // A manually added shortcut to this exact file stays manually managed.
        if(std::any_of(next.items.begin(),next.items.end(),[&](const auto& item){return item.locator["windowsTarget"]["path"]==J(entry->path);}))continue;
        try{
            ++result.inspected;auto candidate=inspect(entry->path);
            ShortcutRecord item;item.id=newID();item.name=item.originalName=entry->name;item.createdAt=now;
            item.locator=std::move(candidate.locator);item.bundleIdentifier=std::move(candidate.bundleIdentifier);
            item.originalFields[std::string(desktopSourceField)]=source(*entry);
            validateShortcut(item,rules);next.items.push_back(std::move(item));++result.added;
        }catch(const std::exception&){++result.failed;}
    }
    J::Array keys;for(const auto& key:ignored)keys.emplace_back(key);
    if(keys.empty())next.originalFields.erase(desktopIgnoredField);
    else next.originalFields[std::string(desktopIgnoredField)]=std::move(keys);
    result.changed=next!=file;if(result.changed)file=std::move(next);
    return result;
}
}
