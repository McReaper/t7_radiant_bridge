#include "plugin.h"
#include "seh.h"
#include "hostapi.h"
#include "log.h"
#include "modtools_plugin.h"

// The bridge's entry, compiled into this DLL (plugin/src/bridge.cpp).
extern "C" MODTOOLS_PLUGIN_EXPORT const ModToolsPluginApi* ModToolsPluginEntry(ModToolsPlugin* self, const ModToolsHostApi* host);

namespace host::plugin
{
    namespace
    {
        // The opaque handle the plugin gets; the host's entries ignore it (one plugin).
        int g_handle = 0;
        ModToolsPlugin* const kSelf = reinterpret_cast<ModToolsPlugin*>(&g_handle);

        const ModToolsPluginApi* g_api = nullptr;
        bool g_triedLoad = false;
        bool g_failed = false;

        struct EntryCall { const ModToolsPluginApi* result; };
        void CallEntry(void* a) { static_cast<EntryCall*>(a)->result = ModToolsPluginEntry(kSelf, host::hostapi::Table()); }

        void CallOnLoad(void*) { if (g_api && g_api->OnLoad) g_api->OnLoad(); }
        void CallOnTick(void*) { if (g_api && g_api->OnTick) g_api->OnTick(); }

        struct MapCall { const wchar_t* path; bool autosave; bool selected; };
        void CallMapLoaded(void* a) { auto* c = static_cast<MapCall*>(a); if (g_api && g_api->OnMapLoaded) g_api->OnMapLoaded(c->path); }
        void CallMapSaved(void* a) { auto* c = static_cast<MapCall*>(a); if (g_api && g_api->OnMapSaved) g_api->OnMapSaved(c->path, c->autosave ? 1 : 0, c->selected ? 1 : 0); }

        struct ScopeCall { const wchar_t* path; int depth; };
        void CallScope(void* a) { auto* c = static_cast<ScopeCall*>(a); if (g_api && g_api->OnScopeChanged) g_api->OnScopeChanged(c->path, c->depth); }

        void CallDraw(void* a) { if (g_api && g_api->OnDraw) g_api->OnDraw(static_cast<const ModToolsDrawContext*>(a)); }

        // Runs a callback under SEH; the first fault disables the plugin.
        void Guard(void (*fn)(void*), void* arg, const char* what)
        {
            if (g_failed) return;
            if (!host::seh::Run(fn, arg, what)) g_failed = true;
        }
    }

    bool Loaded() { return g_api != nullptr && !g_failed; }

    bool EnsureLoaded()
    {
        if (g_triedLoad) return Loaded();
        g_triedLoad = true;
        EntryCall ec{ nullptr };
        if (!host::seh::Run(CallEntry, &ec, "ModToolsPluginEntry") || !ec.result)
        {
            HLOG("host: the bridge's entry failed");
            g_failed = true;
            return false;
        }
        g_api = ec.result;
        HLOG("host: bridge loaded (%s), calling OnLoad", g_api->version ? g_api->version : "?");
        Guard(CallOnLoad, nullptr, "OnLoad");
        return Loaded();
    }

    void Tick() { if (Loaded()) Guard(CallOnTick, nullptr, "OnTick"); }

    void MapLoaded(const std::wstring& path)
    {
        if (!Loaded()) return;
        MapCall c{ path.c_str(), false, false };
        Guard(CallMapLoaded, &c, "OnMapLoaded");
    }

    void MapSaved(const std::wstring& path, bool autosave, bool selectedOnly)
    {
        if (!Loaded()) return;
        MapCall c{ path.c_str(), autosave, selectedOnly };
        Guard(CallMapSaved, &c, "OnMapSaved");
    }

    void ScopeChanged(const std::wstring& path, int depth)
    {
        if (!Loaded()) return;
        ScopeCall c{ path.c_str(), depth };
        Guard(CallScope, &c, "OnScopeChanged");
    }

    void Draw(const ModToolsDrawContext& ctx)
    {
        if (Loaded()) Guard(CallDraw, const_cast<ModToolsDrawContext*>(&ctx), "OnDraw");
    }
}
