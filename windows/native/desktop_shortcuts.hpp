#pragma once
#include "modules/desktop_shortcut_sync.hpp"
#include <span>

namespace endfield::native {
// Worker-only, one nonrecursive directory listing. No link resolution, icon
// extraction, application launch, directory watcher or recurring timer.
modules::DesktopShortcutSnapshot scanDesktopShortcuts(std::span<const std::filesystem::path> roots);
#ifdef _WIN32
modules::DesktopShortcutSnapshot windowsDesktopShortcuts();
#endif
}
