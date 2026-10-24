// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/radiant_addrs.h / radiant_addrs.cpp at 89423aa)
// on 2026-10-23. Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// The subset of Radiant_modtools.exe addresses the standalone host reads directly. The exe has no ASLR (base
// 0x140000000); Rva rebases. The bridge resolves its own addresses through host->RadiantAddress (= Rva), so only
// what the host's own reads and hooks need is here.
#pragma once
#include <cstdint>

namespace host::rt
{
    constexpr uintptr_t kImageBase = 0x140000000ULL;

    // Live address of an IDA address, or nullptr-equivalent arithmetic when the base is 0.
    uintptr_t Rva(uintptr_t idaAddress);
    // The loaded exe is the Radiant build these addresses describe. Cached, logs once.
    bool VerifyBinary();

    template <typename T> T* Global(uintptr_t ida) { return reinterpret_cast<T*>(Rva(ida)); }
    template <typename Fn> Fn Func(uintptr_t ida) { return reinterpret_cast<Fn>(Rva(ida)); }

    // --- map load / save, the idle tick, prefab enter/leave, the reload prompt ---
    constexpr uintptr_t Map_LoadFile = 0x140CD1760;
    constexpr uintptr_t Map_SaveFile = 0x140CD2890;
    constexpr uintptr_t Map_EnterPrefab = 0x140CDF5A0;
    constexpr uintptr_t Map_LeavePrefab = 0x140CD5E30;
    constexpr uintptr_t RoutineProcessing = 0x14064BFB0;   // RadiantMainWin::RoutineProcessing(this)
    constexpr uintptr_t QuestionBox = 0x140D337C0;         // (parent, text, title, buttons, default): 0x4000 Yes, 0x10000 No
    constexpr uintptr_t str_LightingChanged = 0x14264DA20; // .rdata: the "Lighting changed" question every load asks
    constexpr uintptr_t Bookmarks_Save = 0x140D46D40;      // char (bookmarks, const char* mapName), what Radiant does before a load
    constexpr uintptr_t Bookmarks_Count = 0x140D47010;     // int (bookmarks)
    constexpr uintptr_t g_bookmarks = 0x14521F200;         // camera bookmark table

    // --- map state globals ---
    constexpr uintptr_t g_currentMapName = 0x14521ED20;   // char[] path, "unnamed.map" when none
    constexpr uintptr_t g_modified = 0x14521EB94;         // int: 0 clean, 1 dirty, 2 after save-selected
    constexpr uintptr_t g_prefabDepth = 0x14521EB9C;      // int: 0 = the top-level map

    // --- camera (CCamWnd reached from the main window) ---
    constexpr uintptr_t g_pMainWin = 0x1444C6560;         // RadiantMainWin*
    constexpr size_t off_MainWin_QEChildren = 64;
    constexpr size_t off_QEChildren_CamWnd = 240;
    constexpr size_t off_CamWnd_Origin = 208;             // float[3]
    constexpr size_t off_CamWnd_Angles = 220;             // float[3]

    // --- entities / brushes / selection ---
    constexpr uintptr_t Entity_Write = 0x140C8FCF0;       // (entity_t*, FILE*, char selectedOnly): "{ guid ... }" + brushes
    constexpr uintptr_t g_entityList = 0x14522B440;       // global entity list sentinel
    constexpr uintptr_t g_worldEntity = 0x14522B438;      // EntityInst* of worldspawn (entity_t at +0), set by Map_LoadFile
    constexpr uintptr_t g_selectedBrushes = 0x1453A2278;  // BrushCollection "... - Selected Brushes"
    constexpr size_t off_Collection_List = 56;            // -> list sentinel
    constexpr size_t off_EInst_Entity = 0;                // entity_t* inside an EntityInst
    constexpr size_t off_Entity_BrushPrev = 0;            // the entity's brush list sentinel (prev/next of its brushes)
    constexpr size_t off_Entity_BrushNext = 8;
    constexpr size_t off_Entity_ListNext = 448;           // global entity list next
    constexpr size_t off_Entity_Guid = 352;               // GUID (16 bytes)
    constexpr size_t off_Brush_Prev = 0;                  // links in the owning entity's list
    constexpr size_t off_Brush_Next = 8;
    constexpr size_t off_Brush_Owner = 16;                // entity_t*
    constexpr size_t off_Brush_Guid = 48;                 // GUID (16 bytes)
    constexpr size_t off_Brush_Mins = 64;                 // float[3]
    constexpr size_t off_Brush_Maxs = 76;                 // float[3]
    constexpr size_t off_BInst_Next = 8;                  // links in the collection list
    constexpr size_t off_BInst_Brush = 48;                // brush_t*

    // --- EntityInst world list (a brush_t -> its BrushInst, what Select_Brush wants) ---
    constexpr uintptr_t g_world = 0x14522B130;            // world object; the EntityInst list is reached through it
    constexpr uintptr_t EntityInst_Begin = 0x1400186F6;   // thunk: (world, EntityInst** out) writes the first inst
    constexpr uintptr_t EntityInst_End = 0x14004E1C0;     // thunk: (world, EntityInst** out) returns EntityInst** end
    constexpr size_t off_EInst_ListSentinel = 16;         // pseudo BrushInst; its instance-list prev/next at +24/+32
    constexpr size_t off_EInst_Next = 768;                // next EntityInst in the world list
    constexpr size_t off_BInst_ListPrev = 24;             // links in the owning EntityInst's instance list
    constexpr size_t off_BInst_ListNext = 32;

    // --- paste / delete / undo pipeline ---
    constexpr uintptr_t Map_ImportBuffer = 0x140CD3E60;   // (char** text, uint flags, uchar regenerateGuids)
    constexpr uintptr_t BeginParseSession = 0x1406F6A80;  // (const char* name)
    constexpr uintptr_t EndParseSession = 0x140D3C9F0;    // ()
    constexpr uintptr_t Undo_Begin = 0x140C164D0;         // (const char* label), the label kept by pointer
    constexpr uintptr_t Undo_End = 0x140C16520;           // ()
    constexpr uintptr_t Undo_IsOpen = 0x140C16ED0;        // () -> bool: the top undo operation is still open
    constexpr uintptr_t Undo_AddBrushList = 0x140B00FA0;  // (BrushCollection*) records the selection for undo
    constexpr uintptr_t Selection_BeginBatch = 0x140B01060; // (BrushCollection*) ++batch depth
    constexpr uintptr_t Selection_EndBatch = 0x140B01070; // (BrushCollection*, int 3, __int64 0, char 1) fires the change event
    constexpr uintptr_t Selection_CloseUndo = 0x140B00FF0; // (BrushCollection*) after Undo_AddBrushList
    constexpr uintptr_t Select_Brush = 0x140B01DB0;       // (BrushCollection*, BrushInst*) — NOT a brush_t* (reads the inst GUID at +56)
    constexpr uintptr_t Select_Deselect = 0x140D023E0;    // (char clearDrag)
    constexpr uintptr_t Select_Delete = 0x140D02320;      // () deletes everything in the selected collection
    constexpr uintptr_t g_importKeepLinks = 0x14521EB91;  // char: 1 keeps target/linkto through the paste remap

    // --- entity GUID registry (a GUID rewrite must move the instance through it) ---
    constexpr uintptr_t g_entityRegistry = 0x14522B6D0;   // std::multimap<GUID, EntityInst*> head; node +25 isnil, +48 EntityInst*
    constexpr uintptr_t EntityRegistry_Add = 0x140CD81B0; // (EntityInst*)
    constexpr uintptr_t EntityRegistry_Remove = 0x140CD82F0; // (EntityInst*)
    constexpr size_t off_Entity_EClass = 240;             // eclass*; eclass[72] != 0 = fixed-size (point / model / prefab)
    constexpr size_t off_Entity_ListPrev = 440;           // global entity list prev (an import's newest entity is list->prev)

    // --- view redraw flags (consumed on the idle tick) ---
    constexpr uintptr_t g_redrawFlags = 0x1444C656C;      // int bitmask
    constexpr unsigned kRedraw3D = 0x1;
    constexpr unsigned kRedraw2D = 0x2;
}
