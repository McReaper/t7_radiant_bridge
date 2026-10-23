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
    constexpr uintptr_t QuestionBox = 0x140D337C0;

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
    constexpr uintptr_t g_entityList = 0x14522B440;       // global entity list sentinel
    constexpr uintptr_t g_selectedBrushes = 0x1453A2278;  // BrushCollection "... - Selected Brushes"
    constexpr size_t off_Collection_List = 56;            // -> list sentinel
    constexpr size_t off_Entity_BrushNext = 8;            // the entity's brush list next
    constexpr size_t off_Entity_ListNext = 448;           // global entity list next
    constexpr size_t off_Entity_Guid = 352;               // GUID (16 bytes)
    constexpr size_t off_Brush_Next = 8;                  // links in the owning entity's list
    constexpr size_t off_Brush_Owner = 16;                // entity_t*
    constexpr size_t off_Brush_Guid = 48;                 // GUID (16 bytes)
    constexpr size_t off_Brush_Mins = 64;                 // float[3]
    constexpr size_t off_Brush_Maxs = 76;                 // float[3]
    constexpr size_t off_BInst_Next = 8;                  // links in the collection list
    constexpr size_t off_BInst_Brush = 48;                // brush_t*
}
