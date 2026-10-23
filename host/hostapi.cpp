// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hostapi.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// The host-API table, filled with the entries hello and state use (M2a). Entries not yet implemented are null; the
// bridge null-checks them, and host->size spans the whole struct so rp::Has reports them present.
#include "hostapi.h"
#include "addrs.h"
#include "editor.h"
#include "hooks.h"
#include "mainwindow.h"
#include "log.h"
#include "modtools_plugin.h"
#include <windows.h>
#include <objbase.h>   // StringFromGUID2
#include <cstring>
#include <string>

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

        // ---- raw ----
        void* H_RadiantAddress(ModToolsPlugin*, uintptr_t ida) { return rt::VerifyBinary() ? reinterpret_cast<void*>(rt::Rva(ida)) : nullptr; }
        int H_Detour(ModToolsPlugin*, void* target, void* detour, void** original) { return host::hooks::Detour(target, detour, original) ? 1 : 0; }
        int H_App(ModToolsPlugin*) { return MODTOOLS_APP_RADIANT; }

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
            t.EntityGuid = H_EntityGuid;
            t.GetSelection = H_GetSelection;
            t.RadiantAddress = H_RadiantAddress;
            t.Detour = H_Detour;
            t.App = H_App;
            return t;
        }
    }

    const ModToolsHostApi* Table()
    {
        static ModToolsHostApi t = Build();
        return &t;
    }
}
