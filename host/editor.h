// The Radiant reads the host answers from: the open map, the entity count, the
// selection, the camera. Each verifies the build first and is 0/false/empty on
// a Radiant the addresses don't fit. Main thread only.
#pragma once
#include <functional>
#include <string>

namespace host::editor
{
    // The Radiant name of the open map ("unnamed.map" when none), resolved to a
    // full path; empty off the known build. Read from the live global, so it is
    // current inside a prefab too.
    std::wstring CurrentMapPath();
    int Modified();        // Radiant's unsaved-changes flag
    int PrefabDepth();     // 0 = the top-level map

    // The open map's entities, worldspawn first (opaque entity_t*).
    void ForEachEntity(const std::function<void(void*)>& fn);
    size_t SelectionCount();

    bool GetCamera(float origin[3], float angles[3]);
    bool SetCamera(const float origin[3], const float angles[3]);
}
