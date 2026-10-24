// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hostapi.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// The host-API table, filled with the entries the bridge's reads (hello, state, entities, selection), edits (import,
// delete, edit, replace, setkey, select), reload and menus (command, menus, save, action) use. The capture entries
// answer "nothing there" until they are served; entries no bridge command calls stay null (host->size spans the whole
// struct).
#include "hostapi.h"
#include "addrs.h"
#include "editor.h"
#include "hooks.h"
#include "mainwindow.h"
#include "qt.h"
#include "log.h"
#include "modtools_plugin.h"
#include <windows.h>
#include <objbase.h>   // StringFromGUID2
#include <cstring>
#include <string>
#include <vector>

namespace host::hostapi
{
    namespace
    {
        DWORD g_mainThread = GetCurrentThreadId();   // set when the table is first built (on the init/main path)
        std::wstring g_dllDir;

        size_t CopyOutW(const std::wstring& s, wchar_t* out, size_t cap)
        {
            if (out && cap) { size_t n = s.size() < cap - 1 ? s.size() : cap - 1; memcpy(out, s.data(), n * sizeof(wchar_t)); out[n] = 0; }
            return s.size();
        }
        std::string GuidText(const BYTE* guid16)
        {
            wchar_t w[64] = {};
            if (StringFromGUID2(*reinterpret_cast<const GUID*>(guid16), w, 64) <= 0) return "";
            char a[64] = {};
            WideCharToMultiByte(CP_ACP, 0, w, -1, a, 64, nullptr, nullptr);
            return a;
        }

        // ---- where things are ----
        const wchar_t* H_PluginDir(ModToolsPlugin*) { return g_dllDir.c_str(); }
        const wchar_t* H_PluginsRoot(ModToolsPlugin*) { return L""; }   // MarkVersionSeen is compiled out of the standalone build
        const wchar_t* H_RadiantRoot(ModToolsPlugin*)
        {
            static wchar_t root[MAX_PATH * 2] = {};
            if (!root[0]) GetEnvironmentVariableW(L"TA_TOOLS_PATH", root, MAX_PATH * 2);
            return root;
        }

        // ---- log ----
        void H_Log(ModToolsPlugin*, const char* fmt, ...)
        {
            char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
            HLOG("%s", buf);
        }
        void H_LogW(ModToolsPlugin*, const wchar_t* fmt, ...)
        {
            wchar_t wbuf[2048]; va_list ap; va_start(ap, fmt); _vsnwprintf_s(wbuf, _TRUNCATE, fmt, ap); va_end(ap);
            char buf[2048]; WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof buf, nullptr, nullptr);
            HLOG("%s", buf);
        }

        // ---- config (ini beside the DLL) ----
        size_t H_ConfigGet(ModToolsPlugin*, const char* section, const char* key, const char* def, char* out, size_t cap)
        {
            size_t n = def ? strlen(def) : 0;
            if (out && cap) { size_t m = n < cap - 1 ? n : cap - 1; if (def) memcpy(out, def, m); out[m] = 0; }
            (void)section; (void)key;
            return n;
        }
        int H_ConfigGetInt(ModToolsPlugin*, const char*, const char*, int def) { return def; }
        void H_ConfigSet(ModToolsPlugin*, const char*, const char*, const char*) {}

        // ---- threads ----
        void H_PostToMainThread(ModToolsPlugin*, ModToolsCallback fn, void* user) { if (fn) fn(user); }   // the host's ticks already run here
        int H_IsMainThread(ModToolsPlugin*) { return GetCurrentThreadId() == g_mainThread ? 1 : 0; }
        void* H_MainWindow(ModToolsPlugin*) { return host::FindMainWindow(); }

        // ---- map ----
        size_t H_CurrentMapPath(ModToolsPlugin*, wchar_t* out, size_t cap) { return CopyOutW(host::editor::CurrentMapPath(), out, cap); }
        int H_PrefabDepth(ModToolsPlugin*) { return host::editor::PrefabDepth(); }
        int H_IsMapModified(ModToolsPlugin*) { return host::editor::Modified(); }

        // ---- camera ----
        int H_GetCamera(ModToolsPlugin*, float o[3], float a[3]) { return host::editor::GetCamera(o, a) ? 1 : 0; }
        int H_SetCamera(ModToolsPlugin*, const float o[3], const float a[3]) { return host::editor::SetCamera(o, a) ? 1 : 0; }

        // ---- entities / selection ----
        void H_ForEachEntity(ModToolsPlugin*, ModToolsEntityFn fn, void* user)
        {
            if (fn) host::editor::ForEachEntity([&](void* e) { fn(e, user); });
        }
        int H_EntityGuid(ModToolsPlugin*, ModToolsEntity e, char out[40])
        {
            std::string g = e ? GuidText(reinterpret_cast<BYTE*>(e) + rt::off_Entity_Guid) : "";
            if (g.empty() || g.size() >= 40) return 0;
            strcpy_s(out, 40, g.c_str());
            return 1;
        }
        size_t H_GetSelection(ModToolsPlugin*, ModToolsSelectedBrush* out, size_t cap)
        {
            if (!rt::VerifyBinary()) return 0;
            BYTE* collection = rt::Global<BYTE>(rt::g_selectedBrushes);
            BYTE* sentinel = *reinterpret_cast<BYTE**>(collection + rt::off_Collection_List);
            if (!sentinel) return 0;
            size_t n = 0, guard = 0;
            for (BYTE* node = *reinterpret_cast<BYTE**>(sentinel + rt::off_BInst_Next);
                 node && node != sentinel && guard++ < 100000;
                 node = *reinterpret_cast<BYTE**>(node + rt::off_BInst_Next))
            {
                BYTE* brush = *reinterpret_cast<BYTE**>(node + rt::off_BInst_Brush);
                if (!brush) continue;
                if (out && n < cap)
                {
                    ModToolsSelectedBrush& sb = out[n];
                    strcpy_s(sb.brushGuid, sizeof sb.brushGuid, GuidText(brush + rt::off_Brush_Guid).c_str());
                    BYTE* owner = *reinterpret_cast<BYTE**>(brush + rt::off_Brush_Owner);
                    strcpy_s(sb.entityGuid, sizeof sb.entityGuid, owner ? GuidText(owner + rt::off_Entity_Guid).c_str() : "");
                    memcpy(sb.mins, brush + rt::off_Brush_Mins, sizeof sb.mins);
                    memcpy(sb.maxs, brush + rt::off_Brush_Maxs, sizeof sb.maxs);
                }
                ++n;
            }
            return n;
        }

        void H_ForEachEntityBrush(ModToolsPlugin*, ModToolsEntity e, ModToolsBrushFn fn, void* user)
        {
            if (fn) host::editor::ForEachEntityBrush(e, [&](void* b) { fn(b, user); });
        }
        int H_BrushGuid(ModToolsPlugin*, ModToolsBrush b, char out[40])
        {
            std::string g = host::editor::BrushGuid(b);
            if (g.empty() || g.size() >= 40) return 0;
            strcpy_s(out, 40, g.c_str());
            return 1;
        }
        ModToolsEntity H_BrushOwner(ModToolsPlugin*, ModToolsBrush b) { return host::editor::BrushOwner(b); }
        ModToolsEntity H_WorldEntity(ModToolsPlugin*) { return host::editor::WorldEntity(); }
        ModToolsBrush H_BrushOfInst(ModToolsPlugin*, ModToolsBrushInst inst) { return host::editor::BrushOfInst(inst); }
        size_t H_SelectedBrushInsts(ModToolsPlugin*, ModToolsBrushInst* out, size_t cap)
        {
            std::vector<void*> v = host::editor::SelectedBrushInsts();
            for (size_t i = 0; i < v.size() && i < cap && out; ++i) out[i] = v[i];
            return v.size();
        }

        char* DupString(const std::string& s)
        {
            char* p = static_cast<char*>(malloc(s.size() + 1));
            if (p) { memcpy(p, s.data(), s.size()); p[s.size()] = 0; }
            return p;
        }
        char* H_WriteEntityText(ModToolsPlugin*, ModToolsEntity e)
        {
            std::string t;
            return host::editor::WriteEntityText(e, t) ? DupString(t) : nullptr;
        }
        char* H_WriteEntityTextWith(ModToolsPlugin*, ModToolsEntity e, const ModToolsBrush* brushes, size_t n)
        {
            std::vector<void*> extra(brushes, brushes + n);
            std::string t;
            return host::editor::WriteEntityTextWith(e, extra, t) ? DupString(t) : nullptr;
        }
        void H_FreeString(ModToolsPlugin*, char* s) { free(s); }
        size_t H_FindEntitiesByGuid(ModToolsPlugin*, const char* const* guids, size_t n, ModToolsEntity* out)
        {
            std::vector<std::string> keys(n);
            for (size_t i = 0; i < n; ++i) keys[i] = guids[i] ? guids[i] : "";
            std::vector<void*> found = host::editor::FindEntitiesByGuid(keys);
            size_t got = 0;
            for (size_t i = 0; i < n; ++i) { out[i] = found[i]; if (found[i]) ++got; }
            return got;
        }

        // ---- raw ----
        void* H_RadiantAddress(ModToolsPlugin*, uintptr_t ida) { return rt::VerifyBinary() ? reinterpret_cast<void*>(rt::Rva(ida)) : nullptr; }
        int H_Detour(ModToolsPlugin*, void* target, void* detour, void** original) { return host::hooks::Detour(target, detour, original) ? 1 : 0; }
        int H_App(ModToolsPlugin*) { return MODTOOLS_APP_RADIANT; }

        // ---- edits ----
        int H_ImportMapText(ModToolsPlugin*, const char* text, const char* label)
        {
            return text && host::editor::ImportMapText(text, label ? label : "plugin") ? 1 : 0;
        }
        int H_DeleteBrushes(ModToolsPlugin*, const ModToolsBrush* brushes, size_t n, const char* label)
        {
            return host::editor::DeleteBrushes(std::vector<void*>(brushes, brushes + n), label ? label : "plugin") ? 1 : 0;
        }
        void H_SelectBrushInsts(ModToolsPlugin*, const ModToolsBrushInst* insts, size_t n)
        {
            host::editor::SelectBrushInsts(std::vector<void*>(insts, insts + n));
        }
        void H_DeselectAll(ModToolsPlugin*) { host::editor::DeselectAll(); }
        void H_BeginUndo(ModToolsPlugin*, const char* label) { host::editor::BeginUndo(label); }
        void H_EndUndo(ModToolsPlugin*) { host::editor::EndUndo(); }
        ModToolsBrushInst H_BrushInstOf(ModToolsPlugin*, ModToolsBrush b) { return host::editor::FindBrushInst(b); }
        int H_EntityFixedSize(ModToolsPlugin*, ModToolsEntity e) { return host::editor::EntityFixedSize(e) ? 1 : 0; }
        void H_RequestRedraw(ModToolsPlugin*, unsigned views)
        {
            unsigned bits = 0;
            if (views & MODTOOLS_VIEW_3D) bits |= rt::kRedraw3D;
            if (views & MODTOOLS_VIEW_2D) bits |= rt::kRedraw2D;
            host::editor::RequestRedraw(bits);
        }
        size_t H_FindBrushesByGuid(ModToolsPlugin*, const char* const* guids, size_t n, ModToolsBrush* out)
        {
            if (!guids || !out) return 0;
            std::vector<std::string> keys(n);
            for (size_t i = 0; i < n; ++i) keys[i] = guids[i] ? guids[i] : "";
            std::vector<void*> found;
            size_t count = host::editor::FindBrushesByGuid(keys, found);
            for (size_t i = 0; i < n; ++i) out[i] = i < found.size() ? found[i] : nullptr;
            return count;
        }
        int H_RestampImportedGuids(ModToolsPlugin*, const char* entityGuid, const char* const* brushGuids, size_t n)
        {
            std::vector<std::string> v(n);
            for (size_t i = 0; i < n; ++i) v[i] = brushGuids[i] ? brushGuids[i] : "";
            return host::editor::RestampImportedGuids(entityGuid ? entityGuid : "", v) ? 1 : 0;
        }
        size_t H_RestampImportedEntities(ModToolsPlugin*, const char* const* entityGuids, size_t n, const char* const* brushGuids, const size_t* brushCounts)
        {
            if (!entityGuids || !brushCounts) return 0;
            std::vector<std::string> ents(n);
            std::vector<std::vector<std::string>> brushes(n);
            size_t k = 0;
            for (size_t i = 0; i < n; ++i)
            {
                ents[i] = entityGuids[i] ? entityGuids[i] : "";
                if (brushCounts[i] && !brushGuids) return 0;
                for (size_t j = 0; j < brushCounts[i]; ++j, ++k) brushes[i].push_back(brushGuids[k] ? brushGuids[k] : "");
            }
            return host::editor::RestampImportedEntities(ents, brushes);
        }
        // ---- reload ----
        int H_ReloadMap(ModToolsPlugin*, const wchar_t* path, int keepCamera) { return path && host::hooks::ReloadMap(path, keepCamera != 0) ? 1 : 0; }
        int H_IsReloading(ModToolsPlugin*) { return host::hooks::IsReloading() ? 1 : 0; }

        // ---- the editor's menus and named widgets ----
        // 1 triggered, 0 no such entry, -1 disabled
        int H_TriggerMenuAction(ModToolsPlugin*, const char* path)
        {
            void* a = path ? host::qt::FindMenuAction(path) : nullptr;
            if (!a) return 0;
            if (!host::qt::ActionEnabled(a)) return -1;
            HLOG("host: menu action '%s'", path);
            host::qt::TriggerAction(a);
            return 1;
        }
        size_t H_MenuEntries(ModToolsPlugin*, const char* path, char* out, size_t cap)
        {
            std::string s = host::qt::DescribeMenu(path ? path : "");
            if (out && cap) { size_t n = s.size() < cap - 1 ? s.size() : cap - 1; memcpy(out, s.data(), n); out[n] = 0; }
            return s.size();
        }
        ModToolsWidget W_FindEditorWidget(const char* name) { return host::qt::FindEditorWidget(name); }

        // ---- not yet served: the view capture (M2d) ----
        // The bridge calls these without a null check, and a call through a null entry would fault and disable it for
        // the session: each answers "nothing there" instead, which the bridge reports as an error.
        void* H_CamWindow(ModToolsPlugin*) { return nullptr; }
        void* H_XYWindow(ModToolsPlugin*, int) { return nullptr; }
        int H_Project(ModToolsPlugin*, const float*, float* sx, float* sy)
        {
            if (sx) *sx = 0;
            if (sy) *sy = 0;
            return 0;
        }

        ModToolsHostApi Build()
        {
            wchar_t p[MAX_PATH * 2] = {};
            HMODULE self = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&Build), &self);
            GetModuleFileNameW(self, p, MAX_PATH * 2);
            std::wstring s(p);
            size_t slash = s.find_last_of(L"\\/");
            g_dllDir = slash == std::wstring::npos ? L"." : s.substr(0, slash);
            g_mainThread = GetCurrentThreadId();

            ModToolsHostApi t{};
            t.size = sizeof t;
            t.apiVersion = MODTOOLS_PLUGIN_API_VERSION;
            t.hostVersion = 0x010200;   // 1.2.0; >= 0x10100 so the bridge keeps target/linkto on import
            t.PluginDir = H_PluginDir;
            t.PluginsRoot = H_PluginsRoot;
            t.RadiantRoot = H_RadiantRoot;
            t.Log = H_Log;
            t.LogW = H_LogW;
            t.ConfigGet = H_ConfigGet;
            t.ConfigGetInt = H_ConfigGetInt;
            t.ConfigSet = H_ConfigSet;
            t.PostToMainThread = H_PostToMainThread;
            t.IsMainThread = H_IsMainThread;
            t.MainWindow = H_MainWindow;
            t.CurrentMapPath = H_CurrentMapPath;
            t.PrefabDepth = H_PrefabDepth;
            t.IsMapModified = H_IsMapModified;
            t.GetCamera = H_GetCamera;
            t.SetCamera = H_SetCamera;
            t.ForEachEntity = H_ForEachEntity;
            t.ForEachEntityBrush = H_ForEachEntityBrush;
            t.EntityGuid = H_EntityGuid;
            t.BrushGuid = H_BrushGuid;
            t.BrushOwner = H_BrushOwner;
            t.WorldEntity = H_WorldEntity;
            t.GetSelection = H_GetSelection;
            t.SelectedBrushInsts = H_SelectedBrushInsts;
            t.BrushOfInst = H_BrushOfInst;
            t.WriteEntityText = H_WriteEntityText;
            t.WriteEntityTextWith = H_WriteEntityTextWith;
            t.FreeString = H_FreeString;
            t.FindEntitiesByGuid = H_FindEntitiesByGuid;
            t.RadiantAddress = H_RadiantAddress;
            t.Detour = H_Detour;
            t.App = H_App;
            // edits (the bridge's import/delete/edit/replace/setkey/select commands)
            t.ImportMapText = H_ImportMapText;
            t.DeleteBrushes = H_DeleteBrushes;
            t.SelectBrushInsts = H_SelectBrushInsts;
            t.DeselectAll = H_DeselectAll;
            t.RestampImportedGuids = H_RestampImportedGuids;
            t.BrushInstOf = H_BrushInstOf;
            t.TriggerMenuAction = H_TriggerMenuAction;
            t.BeginUndo = H_BeginUndo;
            t.EndUndo = H_EndUndo;
            t.RequestRedraw = H_RequestRedraw;
            t.FindBrushesByGuid = H_FindBrushesByGuid;
            t.EntityFixedSize = H_EntityFixedSize;
            t.RestampImportedEntities = H_RestampImportedEntities;
            t.ReloadMap = H_ReloadMap;
            t.IsReloading = H_IsReloading;
            // the editor's menus and named widgets (command, menus, save, action)
            t.MenuEntries = H_MenuEntries;
            if (host::qt::Bind())
            {
                // only the entry the bridge reads; host->ui->size spans the whole struct, the rest stay null
                static ModToolsUiApi ui{};
                ui.size = sizeof ui;
                ui.FindEditorWidget = W_FindEditorWidget;
                t.ui = &ui;
            }
            // M2d stubs
            t.CamWindow = H_CamWindow;
            t.XYWindow = H_XYWindow;
            t.Project = H_Project;
            return t;
        }
    }

    const ModToolsHostApi* Table()
    {
        static ModToolsHostApi t = Build();
        return &t;
    }
}
