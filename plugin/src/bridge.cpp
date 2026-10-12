// T7RadiantBridge: serves PROTOCOL.md on \\.\pipe\t7_radiant_bridge_<pid>, inside Radiant, through MTPlugins.
//
// Requests reach Radiant's main thread two ways. The host's idle tick runs them, one connection's in order, at the
// point where Radiant expects edits. While a modal dialog runs its own loop the tick stops, so the pipe threads also
// post to a message-only window of ours, which that loop still serves: there, `hello`, `state` and `menus` answer and
// everything else is told `busy`.
#define MODTOOLS_PLUGIN_IMPL
#include "modtools_plugin.hpp"
#include "commands.h"
#include "pipe.h"
#include "radiant.h"
#include <windows.h>
#include <algorithm>
#include <vector>

namespace bridge
{
    namespace
    {
        constexpr UINT WM_BRIDGE = WM_APP + 0x7B;
        constexpr ULONGLONG kTickBudgetMs = 50;    // queued requests yield the UI past this, the next tick goes on
        const wchar_t* kWindowClass = L"T7RadiantBridge_Dispatch";

        struct Inbound
        {
            std::shared_ptr<Conn> conn;
            Pending p;
        };

        HWND g_window = nullptr;
        std::wstring g_pipeName;
        std::mutex g_inboxMutex;
        std::deque<Inbound> g_inbox;
        std::vector<std::shared_ptr<Conn>> g_live;   // connections with requests queued or running (main thread)
        int g_depth = 0;                             // > 0 while a request runs: a message then comes from a loop inside it

        // ---- pipe threads ---------------------------------------------------------------------------------
        void OnPayload(const std::shared_ptr<Conn>& c, std::string&& payload)
        {
            Inbound in{ c, {} };
            std::string why;
            if (!ParseRequest(payload, in.p.h, in.p.body, why))
            {
                Send(c, FormatReply(in.p.h.id, Err("bad-request", why)));
                return;
            }
            {
                std::lock_guard<std::mutex> lock(g_inboxMutex);
                g_inbox.push_back(std::move(in));
            }
            PostMessageW(g_window, WM_BRIDGE, 0, 0);
        }

        void OnClosed(const std::shared_ptr<Conn>&) { PostMessageW(g_window, WM_BRIDGE, 0, 0); }

        // ---- main thread ----------------------------------------------------------------------------------
        void Intake()
        {
            std::lock_guard<std::mutex> lock(g_inboxMutex);
            for (Inbound& in : g_inbox)
            {
                if (in.conn->closed) continue;
                if (std::find(g_live.begin(), g_live.end(), in.conn) == g_live.end()) g_live.push_back(in.conn);
                in.conn->pending.push_back(std::move(in.p));
            }
            g_inbox.clear();
        }

        void Forget()
        {
            for (auto& c : g_live)
                if (c->closed) c->pending.clear();    // a request is never run once its client left
            g_live.erase(std::remove_if(g_live.begin(), g_live.end(), [](const std::shared_ptr<Conn>& c) { return c->pending.empty() && !c->running; }), g_live.end());
        }

        // Under a dialog: the fast requests run, out of turn, the others are refused. A connection whose request is
        // still running (the one that opened the dialog) keeps its `running` mark.
        void AnswerBlocked(const std::string& dialog)
        {
            std::vector<std::shared_ptr<Conn>> live = g_live;
            for (auto& c : live)
                while (!c->pending.empty())
                {
                    Pending p = std::move(c->pending.front());
                    c->pending.pop_front();
                    Reply r = Err("busy", dialog);
                    if (IsFast(p.h.command))
                    {
                        ++g_depth;
                        Run(c, p, r, true, dialog);
                        --g_depth;
                    }
                    Send(c, FormatReply(p.h.id, r));
                }
        }

        void RunQueued()
        {
            ULONGLONG start = GetTickCount64();
            for (bool again = true; again && !CaptureActive() && GetTickCount64() - start < kTickBudgetMs;)
            {
                again = false;
                for (size_t i = 0; i < g_live.size() && !CaptureActive(); ++i)
                {
                    std::shared_ptr<Conn> c = g_live[i];
                    if (c->running || c->pending.empty() || c->closed) continue;
                    Pending p = std::move(c->pending.front());
                    c->pending.pop_front();
                    Reply r;
                    c->running = true;
                    ++g_depth;
                    bool now = Run(c, p, r, false, std::string());
                    --g_depth;
                    if (now)
                    {
                        c->running = false;
                        Send(c, FormatReply(p.h.id, r));
                    }
                    again = true;
                }
            }
        }

        // fromTick: called by the host's idle tick, where queued requests may run; otherwise by our window.
        void Pump(bool fromTick)
        {
            Intake();
            if (g_live.empty()) return;
            std::string dialog = radiant::ModalTitle();
            bool reloading = rp::Has(&rp::host->IsReloading) && rp::host->IsReloading && rp::host->IsReloading(rp::self);
            if (g_depth > 0 || !dialog.empty() || reloading)
            {
                if (dialog.empty()) dialog = reloading ? "a map is loading" : "a command is still running";
                AnswerBlocked(dialog);
                return;
            }
            if (!fromTick) return;
            RunQueued();
            Forget();
        }

        LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_BRIDGE)
            {
                Pump(false);
                return 0;
            }
            return DefWindowProcW(h, msg, wp, lp);
        }

        HINSTANCE Self()
        {
            HMODULE m = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&WndProc), &m);
            return m;
        }

        void OnConsoleLine(const std::string& line) { Broadcast(kEventConsole, "0 console " + OneLine(line) + "\n"); }

        // ---- plugin callbacks -----------------------------------------------------------------------------
        int OnLoad(void)
        {
            radiant::Bind();
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = WndProc;
            wc.hInstance = Self();
            wc.lpszClassName = kWindowClass;
            if (!RegisterClassExW(&wc) || !(g_window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr)))
            {
                rp::Log("cannot create the dispatch window (%lu)", GetLastError());
                UnregisterClassW(kWindowClass, wc.hInstance);
                return 0;
            }
            g_pipeName = L"\\\\.\\pipe\\t7_radiant_bridge_" + std::to_wstring(GetCurrentProcessId());
            if (!StartPipe(g_pipeName, OnPayload, OnClosed))
            {
                rp::Log("cannot start the pipe");
                DestroyWindow(g_window);
                UnregisterClassW(kWindowClass, wc.hInstance);
                return 0;
            }
            if (!radiant::HookConsole(OnConsoleLine)) rp::Log("Radiant's console print was not found: no console events");
            rp::LogW(L"listening on %ls", g_pipeName.c_str());
            return 1;
        }

        void OnUnload(void)
        {
            radiant::StopConsole();
            StopPipe();
            CaptureCancel();
            {
                std::lock_guard<std::mutex> lock(g_inboxMutex);
                g_inbox.clear();
            }
            g_live.clear();
            DestroyWindow(g_window);
            g_window = nullptr;
            UnregisterClassW(kWindowClass, Self());
        }

        void OnTick(void)
        {
            if (g_depth == 0) CaptureTick();
            Pump(true);
        }

        void OnDraw(const ModToolsDrawContext* ctx) { CaptureDraw(ctx); }

        void OnMapLoaded(const wchar_t* path) { Broadcast(kEventMap, "0 map loaded " + OneLine(Utf8(path ? path : L"")) + "\n"); }

        void OnMapSaved(const wchar_t* path, int, int) { Broadcast(kEventMap, "0 map saved " + OneLine(Utf8(path ? path : L"")) + "\n"); }

        void OnScopeChanged(const wchar_t* path, int depth)
        {
            Broadcast(kEventMap, "0 map scope " + std::to_string(depth) + " " + OneLine(Utf8(path ? path : L"")) + "\n");
        }

        const char* OnCommand(const char*)
        {
            static std::string reply;
            reply = Utf8(g_pipeName) + " connections=" + std::to_string(ConnectionCount());
            return reply.c_str();
        }
    }

    void Complete(const std::shared_ptr<Conn>& c, uint64_t id, const Reply& r)
    {
        c->running = false;
        Send(c, FormatReply(id, r));
    }
}

extern "C" MODTOOLS_PLUGIN_EXPORT const ModToolsPluginApi* ModToolsPluginEntry(ModToolsPlugin* self, const ModToolsHostApi* host)
{
    rp::Bind(self, host);
    static ModToolsPluginApi api = {};
    api.size = sizeof api;
    api.apiVersion = MODTOOLS_PLUGIN_API_VERSION;
    api.name = "T7RadiantBridge";
    api.displayName = "t7kb Radiant bridge";
    api.version = bridge::kPluginVersion;
    api.author = "McReaper";
    api.description = "Lets t7kb, and the AI agent behind it, read and edit the open map, run Radiant's commands, move the camera and see the views, over a named pipe.";
    api.OnLoad = bridge::OnLoad;
    api.OnUnload = bridge::OnUnload;
    api.OnTick = bridge::OnTick;
    api.OnDraw = bridge::OnDraw;
    api.OnMapLoaded = bridge::OnMapLoaded;
    api.OnMapSaved = bridge::OnMapSaved;
    api.OnScopeChanged = bridge::OnScopeChanged;
    api.OnCommand = bridge::OnCommand;
    api.apps = MODTOOLS_APP_RADIANT;
    api.url = "https://github.com/McReaper/t7_radiant_bridge";
    return &api;
}
