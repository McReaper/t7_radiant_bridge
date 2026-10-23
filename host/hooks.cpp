// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hooks.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// Trimmed to what the standalone host needs: the idle tick, map load/save and prefab enter/leave. The bridge resolves
// its own further addresses (console print, undo, camera window) through host->RadiantAddress and host->Detour.
#include "hooks.h"
#include "addrs.h"
#include "plugin.h"
#include "log.h"
#include <windows.h>
#include <shlwapi.h>
#include "MinHook.h"

#pragma comment(lib, "shlwapi.lib")

namespace host::hooks
{
    namespace
    {
        using Map_LoadFile_t = __int64(__fastcall*)(char* filename);
        using Map_SaveFile_t = char(__fastcall*)(char* filename, char silentAutosave, char saveSelected);
        using Prefab_t = __int64(__fastcall*)(__int64, __int64, __int64, __int64);
        using Routine_t = __int64(__fastcall*)(void* mainWin);

        Map_LoadFile_t o_Map_LoadFile = nullptr;
        Map_SaveFile_t o_Map_SaveFile = nullptr;
        Prefab_t o_Map_EnterPrefab = nullptr;
        Prefab_t o_Map_LeavePrefab = nullptr;
        Routine_t o_RoutineProcessing = nullptr;

        bool g_pluginReady = false;   // main thread: EnsureLoaded ran and the map was replayed

        std::wstring Widen(const char* s)
        {
            if (!s) return L"";
            int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
            std::wstring w(n ? n - 1 : 0, L'\0');
            if (n) MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
            return w;
        }

        std::wstring ResolveFull(const std::wstring& name)
        {
            wchar_t b[MAX_PATH * 2];
            DWORD n = GetFullPathNameW(name.c_str(), MAX_PATH * 2, b, nullptr);
            std::wstring p = n ? std::wstring(b) : name;
            for (wchar_t& c : p) if (c == L'/') c = L'\\';
            return p;
        }

        int PrefabDepthNow() { return *rt::Global<int>(rt::g_prefabDepth); }

        void NotifyScope()
        {
            plugin::ScopeChanged(CurrentScopePath(), PrefabDepthNow());
        }

        __int64 __fastcall hk_Map_LoadFile(char* filename)
        {
            HLOG("hook: Map_LoadFile('%s')", filename ? filename : "(null)");
            __int64 r = o_Map_LoadFile(filename);
            plugin::MapLoaded(ResolveFull(Widen(filename)));
            return r;
        }

        __int64 __fastcall hk_Map_EnterPrefab(__int64 a1, __int64 a2, __int64 a3, __int64 a4)
        {
            int before = PrefabDepthNow();
            __int64 r = o_Map_EnterPrefab(a1, a2, a3, a4);
            if (PrefabDepthNow() != before) NotifyScope();
            return r;
        }

        __int64 __fastcall hk_Map_LeavePrefab(__int64 a1, __int64 a2, __int64 a3, __int64 a4)
        {
            int before = PrefabDepthNow();
            __int64 r = o_Map_LeavePrefab(a1, a2, a3, a4);
            if (PrefabDepthNow() != before) NotifyScope();
            return r;
        }

        char __fastcall hk_Map_SaveFile(char* filename, char silentAutosave, char saveSelected)
        {
            HLOG("hook: Map_SaveFile('%s', autosave=%d, selected=%d)", filename ? filename : "(null)", silentAutosave, saveSelected);
            char r = o_Map_SaveFile(filename, silentAutosave, saveSelected);
            plugin::MapSaved(CurrentScopePath(), silentAutosave != 0, saveSelected != 0);
            return r;
        }

        // The main window's idle processing: the host's tick. It loads the plugin
        // on its first run and then drives it every frame, on Radiant's main thread.
        __int64 __fastcall hk_RoutineProcessing(void* mainWin)
        {
            if (!g_pluginReady)
            {
                g_pluginReady = true;
                if (plugin::EnsureLoaded())
                {
                    const char* cur = rt::Global<char>(rt::g_currentMapName);
                    if (cur && *cur && strcmp(cur, "unnamed.map") != 0) plugin::MapLoaded(ResolveFull(Widen(cur)));
                }
            }
            plugin::Tick();
            return o_RoutineProcessing(mainWin);
        }

        bool Hook(const char* name, uintptr_t ida, void* detour, void** original)
        {
            void* target = reinterpret_cast<void*>(rt::Rva(ida));
            MH_STATUS st = MH_CreateHook(target, detour, original);
            if (st == MH_OK) st = MH_EnableHook(target);
            if (st != MH_OK) { HLOG("hook: %s failed: %s", name, MH_StatusToString(st)); return false; }
            HLOG("hook: %s installed at 0x%p", name, target);
            return true;
        }
    }

    std::wstring CurrentScopePath()
    {
        if (!rt::VerifyBinary()) return L"";
        std::wstring name = Widen(rt::Global<char>(rt::g_currentMapName));
        if (!PathIsRelativeW(name.c_str())) return ResolveFull(name);
        wchar_t root[MAX_PATH * 2] = {};
        if (GetEnvironmentVariableW(L"TA_TOOLS_PATH", root, MAX_PATH * 2))
        {
            std::wstring a = std::wstring(root) + L"\\" + name;             if (PathFileExistsW(a.c_str())) return ResolveFull(a);
            std::wstring b = std::wstring(root) + L"\\map_source\\" + name; if (PathFileExistsW(b.c_str())) return ResolveFull(b);
        }
        return ResolveFull(name);
    }

    bool Detour(void* target, void* detour, void** original)
    {
        if (!target || !detour) return false;
        if (MH_CreateHook(target, detour, original) != MH_OK) return false;
        return MH_EnableHook(target) == MH_OK;
    }

    bool Install()
    {
        MH_STATUS st = MH_Initialize();
        if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { HLOG("hook: MH_Initialize failed: %s", MH_StatusToString(st)); return false; }
        if (!rt::VerifyBinary()) { HLOG("hook: not the known Radiant build; hooks disabled"); return false; }

        Hook("Map_LoadFile", rt::Map_LoadFile, reinterpret_cast<void*>(&hk_Map_LoadFile), reinterpret_cast<void**>(&o_Map_LoadFile));
        Hook("Map_SaveFile", rt::Map_SaveFile, reinterpret_cast<void*>(&hk_Map_SaveFile), reinterpret_cast<void**>(&o_Map_SaveFile));
        Hook("Map_EnterPrefab", rt::Map_EnterPrefab, reinterpret_cast<void*>(&hk_Map_EnterPrefab), reinterpret_cast<void**>(&o_Map_EnterPrefab));
        Hook("Map_LeavePrefab", rt::Map_LeavePrefab, reinterpret_cast<void*>(&hk_Map_LeavePrefab), reinterpret_cast<void**>(&o_Map_LeavePrefab));
        return Hook("RoutineProcessing", rt::RoutineProcessing, reinterpret_cast<void*>(&hk_RoutineProcessing), reinterpret_cast<void**>(&o_RoutineProcessing));
    }
}
