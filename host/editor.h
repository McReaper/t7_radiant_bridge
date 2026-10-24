// The Radiant reads the host answers from: the open map, the entity count, the
// selection, the camera. Each verifies the build first and is 0/false/empty on
// a Radiant the addresses don't fit. Main thread only.
#pragma once
#include <functional>
#include <string>
#include <vector>

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
    void ForEachEntityBrush(void* ent, const std::function<void(void*)>& fn);
    size_t SelectionCount();

    void* WorldEntity();
    void* BrushOwner(void* brush);
    // The selection as BrushInst pointers (the collection's own nodes), to keep and restore it.
    std::vector<void*> SelectedBrushInsts();
    void* BrushOfInst(void* inst);
    std::string EntityGuid(void* ent);
    std::string BrushGuid(void* brush);
    // The entities with these GUID texts, nullptr where absent, in order.
    std::vector<void*> FindEntitiesByGuid(const std::vector<std::string>& guids);

    // The entity's .map text as Radiant's writer gives it ("{ guid ... }" + its brushes).
    bool WriteEntityText(void* ent, std::string& out);
    // The same, with extraBrushes (selected ones, unlinked from the entity) linked back in for the write.
    bool WriteEntityTextWith(void* ent, const std::vector<void*>& extraBrushes, std::string& out);

    bool GetCamera(float origin[3], float angles[3]);
    bool SetCamera(const float origin[3], const float angles[3]);

    // ---- edits: the paste/delete/undo pipeline the bridge's edit commands drive. Main thread; each verifies the build. ----
    void DeselectAll();
    void SelectBrushInsts(const std::vector<void*>& insts);   // BrushInst pointers, as SelectedBrushInsts gives them
    void BeginUndo(const char* label);    // one undo operation across several Import/Delete calls; pair with EndUndo
    void EndUndo();
    bool ImportMapText(const std::string& text, const char* label);   // clipboard .map text, worldspawn block first; leaves it selected
    bool DeleteBrushes(const std::vector<void*>& brushes, const char* label);
    void* FindBrushInst(void* brush);     // brush_t* -> its BrushInst (what SelectBrushInsts wants), null if none
    size_t FindBrushesByGuid(const std::vector<std::string>& guids, std::vector<void*>& out);   // out[i] null where absent
    bool EntityFixedSize(void* ent);      // point entity, model or prefab (not a world-brush entity)
    void RequestRedraw(unsigned views);   // rt::kRedraw3D | rt::kRedraw2D, consumed on the next idle tick
    // After an import leaves the new objects selected: write these GUIDs over the fresh ones so identity is kept.
    bool RestampImportedGuids(const std::string& entityGuid, const std::vector<std::string>& brushGuids);
    size_t RestampImportedEntities(const std::vector<std::string>& entityGuids, const std::vector<std::vector<std::string>>& brushGuids);
}
