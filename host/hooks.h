// The detours into Radiant the host installs: the idle tick (drives the plugin),
// map load/save and prefab enter/leave (the map and scope events), and the
// question a load asks (answered during our own reloads). Main thread.
#pragma once
#include <string>

namespace host::hooks
{
    // Initialises MinHook and installs the detours. False when the build doesn't
    // match or MinHook fails; the host then stays passive.
    bool Install();

    // The open map/scope as a full path with backslashes (reads the live global).
    std::wstring CurrentScopePath();

    // MinHook create+enable on a live address, for the host's Detour entry.
    bool Detour(void* target, void* detour, void** original);

    // Loads a map like File > Open without any prompt (unsaved changes are lost), the camera kept when asked.
    // Refused inside a prefab, while a mouse button is held, or during another reload.
    bool ReloadMap(const std::wstring& fullPath, bool keepCamera);
    bool IsReloading();
}
