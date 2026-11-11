#pragma once
#include "modules/app_shortcut_model.hpp"

namespace endfield::modules {
inline constexpr std::string_view desktopSourceField="windowsDesktopSource";
inline constexpr std::string_view desktopIgnoredField="windowsDesktopIgnored";
struct DesktopShortcutEntry {
    std::string path,key,name,fingerprint,fileID;
};
struct DesktopShortcutSnapshot {
    std::vector<DesktopShortcutEntry> entries;
    bool complete{true}; // An unavailable/redirected desktop never means "delete everything".
};
struct DesktopShortcutSyncResult {
    std::size_t added{},updated{},removed{},inspected{},failed{};
    bool changed{};
};
using DesktopShortcutScanner=std::function<DesktopShortcutSnapshot()>;
DesktopShortcutSyncResult synchronizeDesktopShortcuts(ShortcutFile&,const DesktopShortcutSnapshot&,
    const std::function<ShortcutCandidate(const std::string&)>& inspect,
    const std::function<std::string()>& newID,double now,const ShortcutTextRules&);
}
