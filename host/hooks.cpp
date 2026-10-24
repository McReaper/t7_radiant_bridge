// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hooks.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// Trimmed to what the standalone host needs: the idle tick, map load/save, prefab enter/leave, and the reload with the
// question it answers. The bridge resolves its own further addresses (console print, undo, camera window) through
// host->RadiantAddress and host->Detour.
#include "hooks.h"
#include "addrs.h"
#include "editor.h"
#include "plugin.h"
#include "render.h"
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
        using QuestionBox_t = __int64(__fastcall*)(void* parent, const char* text, const char* title, int buttons, int defaultButton);
        using Bookmarks_Save_t = char(__fastcall*)(void* bookmarks, const char* mapName);
        using Bookmarks_Count_t = int(__fastcall*)(void* bookmarks);

        Map_LoadFile_t o_Map_LoadFile = nullptr;
        Map_SaveFile_t o_Map_SaveFile = nullptr;
        Prefab_t o_Map_EnterPrefab = nullptr;
        Prefab_t o_Map_LeavePrefab = nullptr;
        Routine_t o_RoutineProcessing = nullptr;
        QuestionBox_t o_QuestionBox = nullptr;

        bool g_pluginReady = false;   // main thread: EnsureLoaded ran and the map was replayed
        bool g_reloading = false;     // main thread: our own reload in flight

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

        // Every load asks "Lighting changed. Do you want to export?": answered No during our own reloads only.
        __int64 __fastcall hk_QuestionBox(void* parent, const char* text, const char* title, int buttons, int defaultButton)
        {
            if (g_reloading && text && (text == rt::Global<const char>(rt::str_LightingChanged) || strcmp(text, "Lighting changed. Do you want to export?") == 0))
            {
                HLOG("reload: Radiant asked '%s' during our reload; answered No", text);
                return 0x10000;
            }
            return o_QuestionBox(parent, text, title, buttons, defaultButton);
        }

        std::string Narrow(const std::wstring& w)
        {
            int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string s(n ? n - 1 : 0, '\0');
            if (n) WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
            return s;
        }

        bool MouseButtonHeld() { return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0; }

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

    bool ReloadMap(const std::wstring& fullPath, bool keepCamera)
    {
        if (g_reloading || !o_Map_LoadFile || !rt::VerifyBinary()) return false;
        if (PrefabDepthNow() > 0) { HLOG("reload: refused inside a prefab (depth %d)", PrefabDepthNow()); return false; }
        // a load under a held button swallows the release and the freelook cursor stays hidden
        if (MouseButtonHeld()) { HLOG("reload: refused, a mouse button is held"); return false; }
        g_reloading = true;
        std::string narrow = Narrow(fullPath);
        const char* radiantName = rt::Global<char>(rt::g_currentMapName);
        void* bookmarks = rt::Global<void>(rt::g_bookmarks);
        if (rt::Func<Bookmarks_Count_t>(rt::Bookmarks_Count)(bookmarks) && strcmp(radiantName, "unnamed.map") != 0)
            rt::Func<Bookmarks_Save_t>(rt::Bookmarks_Save)(bookmarks, radiantName);
        float origin[3] = {}, angles[3] = {};
        bool haveCamera = editor::GetCamera(origin, angles);
        HLOG("reload: Map_LoadFile('%s') keepCamera=%d", narrow.c_str(), keepCamera ? 1 : 0);
        o_Map_LoadFile(&narrow[0]);   // the original, so the map event goes out once, after the camera is back
        if (keepCamera && haveCamera) editor::SetCamera(origin, angles);
        g_reloading = false;
        CURSORINFO ci{ sizeof ci };
        if (GetCursorInfo(&ci) && !(ci.flags & CURSOR_SHOWING))
        {
            int n = 0;
            while (ShowCursor(TRUE) < 0 && ++n < 16) {}
            HLOG("reload: the cursor was hidden, shown again (%d step%s)", n, n == 1 ? "" : "s");
        }
        plugin::MapLoaded(ResolveFull(fullPath));
        return true;
    }

    bool IsReloading() { return g_reloading; }

    bool Install()
    {
        MH_STATUS st = MH_Initialize();
        if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { HLOG("hook: MH_Initialize failed: %s", MH_StatusToString(st)); return false; }
        if (!rt::VerifyBinary()) { HLOG("hook: not the known Radiant build; hooks disabled"); return false; }

        Hook("Map_LoadFile", rt::Map_LoadFile, reinterpret_cast<void*>(&hk_Map_LoadFile), reinterpret_cast<void**>(&o_Map_LoadFile));
        Hook("Map_SaveFile", rt::Map_SaveFile, reinterpret_cast<void*>(&hk_Map_SaveFile), reinterpret_cast<void**>(&o_Map_SaveFile));
        Hook("Map_EnterPrefab", rt::Map_EnterPrefab, reinterpret_cast<void*>(&hk_Map_EnterPrefab), reinterpret_cast<void**>(&o_Map_EnterPrefab));
        Hook("Map_LeavePrefab", rt::Map_LeavePrefab, reinterpret_cast<void*>(&hk_Map_LeavePrefab), reinterpret_cast<void**>(&o_Map_LeavePrefab));
        Hook("QuestionBox", rt::QuestionBox, reinterpret_cast<void*>(&hk_QuestionBox), reinterpret_cast<void**>(&o_QuestionBox));
        render::Install();
        return Hook("RoutineProcessing", rt::RoutineProcessing, reinterpret_cast<void*>(&hk_RoutineProcessing), reinterpret_cast<void**>(&o_RoutineProcessing));
    }
}
