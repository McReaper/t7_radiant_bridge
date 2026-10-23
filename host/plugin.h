// The one plugin this host carries: the bridge, compiled into the same DLL.
// The host calls its entry and callbacks under SEH; a fault disables it. Main
// thread only (the host's hooks run here).
#pragma once
#include <string>

namespace host::plugin
{
    // Calls ModToolsPluginEntry then OnLoad, once. Safe to call every tick: it
    // loads on the first and does nothing after. Returns true once loaded.
    bool EnsureLoaded();
    bool Loaded();

    void Tick();
    void MapLoaded(const std::wstring& path);
    void MapSaved(const std::wstring& path, bool autosave, bool selectedOnly);
    void ScopeChanged(const std::wstring& path, int depth);
}
