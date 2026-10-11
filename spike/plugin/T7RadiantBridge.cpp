// T7RadiantBridge (M0 spike): a named pipe that lets an outside client read and edit the open map, move the camera,
// capture a view and run Radiant's own menu commands. Throwaway code: it answers the spike's questions (issue #1).
//
// Pipe: \\.\pipe\t7_radiant_bridge_<pid>, one client at a time. A message is a 4-byte little-endian length, then UTF-8.
// Request: first line "<command> <args...>", the rest is the body. Reply: first line "ok" or "err <reason>", then the body.
// The pipe thread only moves bytes; every command runs on Radiant's main thread, from OnTick.
#define MODTOOLS_PLUGIN_IMPL
#include "modtools_plugin.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace
{
    // ---- text helpers -------------------------------------------------------------------------------------------
    std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
        std::wstring w(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
        return w;
    }
    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }
    std::vector<std::string> Words(const std::string& line)
    {
        std::vector<std::string> out; std::istringstream in(line); std::string w;
        while (in >> w) out.push_back(w);
        return out;
    }
    // everything after the first `n` words of `line`
    std::string Rest(const std::string& line, size_t n)
    {
        size_t pos = 0;
        for (size_t i = 0; i < n; ++i)
        {
            pos = line.find_first_not_of(' ', pos);
            if (pos == std::string::npos) return std::string();
            pos = line.find(' ', pos);
            if (pos == std::string::npos) return std::string();
        }
        pos = line.find_first_not_of(' ', pos);
        return pos == std::string::npos ? std::string() : line.substr(pos);
    }
    std::string Ok(const std::string& body = "") { return "ok\n" + body; }
    std::string Err(const std::string& why) { return "err " + why + "\n"; }

    // ---- jobs: filled by the pipe thread, run on the main thread ------------------------------------------------
    struct Job
    {
        std::string line, body, reply;
        bool done = false;
        bool abandoned = false;    // the client gave up waiting: never run it later
        // a capture waits for frames presented after its redraw request
        bool started = false;
        int viewIndex = 0;
        unsigned frameAtStart = 0;
        ULONGLONG startTick = 0;
    };

    std::mutex g_mutex;
    std::condition_variable g_cv;
    std::deque<std::shared_ptr<Job>> g_queue;      // pipe thread -> main thread
    std::vector<std::shared_ptr<Job>> g_active;    // main thread only
    std::atomic<bool> g_stop{ false };
    HANDLE g_stopEvent = nullptr;
    std::thread g_thread;
    unsigned g_frames[4] = {};
    std::atomic<unsigned long long> g_ticks{ 0 };    // every OnTick, nested ones included
    std::atomic<unsigned long long> g_nestedTicks{ 0 };
    bool g_inTick = false;                          // a command (a menu's modal dialog) can pump a nested tick                     // OnDraw count per view: 0 YZ, 1 XZ, 2 XY, 3 the 3D camera

    void Finish(const std::shared_ptr<Job>& job, std::string reply)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        job->reply = std::move(reply);
        job->done = true;
        g_cv.notify_all();
    }

    // ---- capture: PrintWindow on the view's own swap-chain window -----------------------------------------------
    std::string CaptureBmp(HWND h, const std::string& path)
    {
        RECT r{};
        if (!h || !GetClientRect(h, &r)) return Err("no window for that view");
        int w = r.right - r.left, ht = r.bottom - r.top;
        if (w <= 0 || ht <= 0) return Err("the view has no size");
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -ht; bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        std::string result;
        if (!bmp) result = Err("CreateDIBSection failed");
        else
        {
            HGDIOBJ old = SelectObject(mem, bmp);
            ULONGLONG t0 = GetTickCount64();
            BOOL printed = PrintWindow(h, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
            ULONGLONG took = GetTickCount64() - t0;
            const uint8_t* px = static_cast<const uint8_t*>(bits);
            bool black = true;
            for (size_t i = 0; i < size_t(w) * size_t(ht) * 4 && black; i += 4) black = (px[i] | px[i + 1] | px[i + 2]) == 0;
            BITMAPFILEHEADER fh{};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            fh.bfSize = fh.bfOffBits + DWORD(w) * DWORD(ht) * 4;
            std::ofstream f(Widen(path), std::ios::binary);
            if (!printed) result = Err("PrintWindow failed (" + std::to_string(GetLastError()) + ")");
            else if (!f) result = Err("cannot write " + path);
            else
            {
                f.write(reinterpret_cast<const char*>(&fh), sizeof fh);
                f.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof(BITMAPINFOHEADER));
                f.write(static_cast<const char*>(bits), std::streamsize(size_t(w) * size_t(ht) * 4));
                result = Ok(path + " " + std::to_string(w) + "x" + std::to_string(ht) + " printwindow_ms=" + std::to_string(took) + (black ? " ALL_BLACK" : "") + "\n");
            }
            SelectObject(mem, old);
            DeleteObject(bmp);
        }
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return result;
    }

    // ---- commands (main thread) ---------------------------------------------------------------------------------
    std::string SelectionText()
    {
        std::string out;
        for (const ModToolsSelectedBrush& s : rp::GetSelection())
        {
            char line[256];
            snprintf(line, sizeof line, "%s %s %.1f %.1f %.1f %.1f %.1f %.1f\n", s.brushGuid, s.entityGuid, s.mins[0], s.mins[1], s.mins[2], s.maxs[0], s.maxs[1], s.maxs[2]);
            out += line;
        }
        return out;
    }

    // the brush instances behind entity or brush GUIDs (selection is cleared first so every brush is linked)
    std::vector<ModToolsBrushInst> InstsOf(const std::vector<std::string>& guids, std::vector<ModToolsBrush>* brushesOut, std::string& missing)
    {
        rp::host->DeselectAll(rp::self);
        std::vector<ModToolsBrushInst> insts;
        std::vector<ModToolsEntity> ents = rp::FindEntitiesByGuid(guids);
        std::vector<ModToolsBrush> brushes = rp::FindBrushesByGuid(guids);
        for (size_t i = 0; i < guids.size(); ++i)
        {
            std::vector<ModToolsBrush> found;
            if (ents[i]) rp::ForEachEntityBrush(ents[i], [&](ModToolsBrush b) { found.push_back(b); });
            else if (brushes[i]) found.push_back(brushes[i]);
            else { missing += guids[i] + " "; continue; }
            for (ModToolsBrush b : found)
            {
                if (brushesOut) brushesOut->push_back(b);
                if (ModToolsBrushInst inst = rp::host->BrushInstOf(rp::self, b)) insts.push_back(inst);
            }
        }
        return insts;
    }

    std::string MenuEntries(const std::string& path)
    {
        size_t n = rp::host->MenuEntries(rp::self, path.c_str(), nullptr, 0);
        std::string s(n, '\0');
        if (n) rp::host->MenuEntries(rp::self, path.c_str(), &s[0], n + 1);
        return s;
    }

    std::string ReadFileText(const std::string& path, bool& ok)
    {
        std::ifstream f(Widen(path), std::ios::binary);
        ok = bool(f);
        std::ostringstream ss; ss << f.rdbuf();
        return ss.str();
    }

    std::string Import(const std::string& label, const std::string& text, bool raw)
    {
        if (text.empty()) return Err("import needs .map text in the body");
        if (!raw) rp::host->DeselectAll(rp::self);
        std::string full = raw ? text : std::string(rp::detail::EmptyWorldBlock()) + text;
        if (!rp::host->ImportMapText(rp::self, full.c_str(), label.c_str())) return Err("Radiant refused the import");
        return Ok(SelectionText());
    }

    std::string RunSimple(const std::string& line, const std::string& body);

    std::string Batch(const std::string& label, const std::string& body)
    {
        rp::host->BeginUndo(rp::self, label.c_str());
        std::string out;
        std::istringstream in(body); std::string sub;
        while (std::getline(in, sub))
        {
            if (!sub.empty() && sub.back() == '\r') sub.pop_back();
            if (sub.empty()) continue;
            out += "> " + sub + "\n" + RunSimple(sub, "");
        }
        rp::host->EndUndo(rp::self);
        return Ok(out);
    }

    std::string RunSimple(const std::string& line, const std::string& body)
    {
        std::vector<std::string> w = Words(line);
        if (w.empty()) return Err("empty command");
        const std::string& cmd = w[0];
        if (cmd == "ping")
        {
            char info[512];
            snprintf(info, sizeof info, "pid=%lu host=%u.%u.%u api=%u map=%s modified=%d prefabDepth=%d\n", GetCurrentProcessId(),
                     rp::host->hostVersion >> 16, (rp::host->hostVersion >> 8) & 0xff, rp::host->hostVersion & 0xff, rp::host->apiVersion,
                     Narrow(rp::CurrentMapPath()).c_str(), rp::host->IsMapModified(rp::self), rp::host->PrefabDepth(rp::self));
            return Ok(info);
        }
        if (cmd == "entities")
        {
            std::string out; int n = 0;
            rp::ForEachEntity([&](ModToolsEntity e) { std::string t; if (rp::WriteEntityText(e, t)) { out += "// entity " + std::to_string(n) + "\n" + t; } ++n; });
            return Ok(out);
        }
        if (cmd == "entity" && w.size() == 2)
        {
            ModToolsEntity e = rp::FindEntitiesByGuid({ w[1] })[0];
            std::string t;
            return e && rp::WriteEntityText(e, t) ? Ok(t) : Err("no entity " + w[1]);
        }
        if (cmd == "selection") return Ok(SelectionText());
        if (cmd == "deselect") { rp::host->DeselectAll(rp::self); return Ok(); }
        if (cmd == "select" && w.size() >= 2)
        {
            std::string missing;
            std::vector<ModToolsBrushInst> insts = InstsOf(std::vector<std::string>(w.begin() + 1, w.end()), nullptr, missing);
            rp::host->SelectBrushInsts(rp::self, insts.data(), insts.size());
            return missing.empty() ? Ok(SelectionText()) : Err("not found: " + missing);
        }
        if (cmd == "delete" && w.size() >= 3)       // delete <label> <guid...>
        {
            std::string missing; std::vector<ModToolsBrush> brushes;
            InstsOf(std::vector<std::string>(w.begin() + 2, w.end()), &brushes, missing);
            if (brushes.empty()) return Err("nothing to delete; not found: " + missing);
            return rp::host->DeleteBrushes(rp::self, brushes.data(), brushes.size(), w[1].c_str()) ? Ok(std::to_string(brushes.size()) + " brushes\n") : Err("Radiant refused the delete");
        }
        if (cmd == "setkey" && w.size() >= 3)      // setkey <guid> <key> [value...]
        {
            ModToolsEntity e = rp::FindEntitiesByGuid({ w[1] })[0];
            if (!e) return Err("no entity " + w[1]);
            std::string value = Rest(line, 3);
            std::string after = rp::SetKey(e, w[2], value, "t7kb: set key");
            return after.empty() ? Err("refused, see RadiantPlugins.log") : Ok(after + "\n");
        }
        if (cmd == "import") return Import(w.size() > 1 ? Rest(line, 1) : "t7kb: import", body, false);
        if (cmd == "import-raw") return Import(w.size() > 1 ? Rest(line, 1) : "t7kb: import", body, true);
        if (cmd == "import-file" && w.size() >= 2)  // import-file <path>, for batches
        {
            bool ok = false; std::string text = ReadFileText(Rest(line, 1), ok);
            return ok ? Import("t7kb: import", text, text.find("\"worldspawn\"") != std::string::npos) : Err("cannot read " + Rest(line, 1));
        }
        if (cmd == "menu" && w.size() >= 2)
        {
            int r = rp::host->TriggerMenuAction(rp::self, Rest(line, 1).c_str());
            return r == 1 ? Ok("triggered\n") : r == 0 ? Err("no such menu entry") : Err("menu entry disabled");
        }
        if (cmd == "menus") return Ok(MenuEntries(w.size() > 1 ? Rest(line, 1) : ""));
        if (cmd == "camera" && w.size() == 1)
        {
            float o[3], a[3];
            if (!rp::host->GetCamera(rp::self, o, a)) return Err("no camera");
            char s[160]; snprintf(s, sizeof s, "%.1f %.1f %.1f %.1f %.1f %.1f\n", o[0], o[1], o[2], a[0], a[1], a[2]);
            return Ok(s);
        }
        if (cmd == "camera" && w.size() == 7)
        {
            float o[3] = { std::stof(w[1]), std::stof(w[2]), std::stof(w[3]) }, a[3] = { std::stof(w[4]), std::stof(w[5]), std::stof(w[6]) };
            if (!rp::host->SetCamera(rp::self, o, a)) return Err("SetCamera refused");
            rp::host->RequestRedraw(rp::self, MODTOOLS_VIEW_3D | MODTOOLS_VIEW_2D);
            return Ok();
        }
        if (cmd == "prefab" && w.size() == 2)
        {
            int r = w[1] == "enter" ? rp::host->EnterSelectedPrefab(rp::self) : w[1] == "leave" ? rp::host->LeavePrefab(rp::self) : -99;
            return r == -99 ? Err("prefab enter|leave") : Ok("result=" + std::to_string(r) + " depth=" + std::to_string(rp::host->PrefabDepth(rp::self)) + "\n");
        }
        if (cmd == "reload" && w.size() >= 2)
        {
            // a selection makes Radiant's load ask "Copy the current selection?" in a modal box, which blocks the main thread
            rp::host->DeselectAll(rp::self);
            return rp::host->ReloadMap(rp::self, Widen(Rest(line, 1)).c_str(), 1) ? Ok() : Err("ReloadMap refused (inside a prefab, or a mouse button is held anywhere)");
        }
        if ((cmd == "children" && w.size() == 3) || (cmd == "click-child" && w.size() == 4))   // children <objectName> <class> | click-child <objectName> <class> <index>
        {
            const ModToolsUiApi* ui = rp::host->ui;
            ModToolsWidget parent = ui ? ui->FindEditorWidget(w[1].c_str()) : nullptr;
            if (!parent) return Err("no widget named " + w[1]);
            if (cmd == "click-child")
            {
                ModToolsWidget c = ui->FindChild(parent, w[2].c_str(), std::stoi(w[3]));
                if (!c) return Err("no such child");
                ui->ClickButton(c);
                return Ok("clicked\n");
            }
            std::string out;
            for (int i = 0; i < 64; ++i)
            {
                ModToolsWidget c = ui->FindChild(parent, w[2].c_str(), i);
                if (!c) break;
                int ok = 0; double v = ui->WidgetNumber(c, &ok);
                out += std::to_string(i) + (ok ? " value=" + std::to_string(v) : std::string()) + (ui->IsChecked(c) ? " checked" : "") + "\n";
            }
            return Ok(out);
        }
        if (cmd == "batch") return Err("batch cannot nest");
        return Err("unknown command '" + cmd + "'");
    }

    // returns false while the job must wait for later ticks
    bool Run(const std::shared_ptr<Job>& job)
    {
        std::vector<std::string> w = Words(job->line);
        if (!w.empty() && w[0] == "capture" && w.size() >= 3)     // capture 3d|xy|xz|yz <path.bmp>
        {
            int idx = w[1] == "3d" ? 3 : w[1] == "xy" ? 2 : w[1] == "xz" ? 1 : w[1] == "yz" ? 0 : -1;
            if (idx < 0) { Finish(job, Err("capture 3d|xy|xz|yz <path>")); return true; }
            if (!job->started)
            {
                job->started = true; job->viewIndex = idx; job->frameAtStart = g_frames[idx]; job->startTick = GetTickCount64();
                rp::host->RequestRedraw(rp::self, idx == 3 ? MODTOOLS_VIEW_3D : MODTOOLS_VIEW_2D);
                return false;
            }
            bool fresh = g_frames[idx] >= job->frameAtStart + 2;
            if (!fresh && GetTickCount64() - job->startTick < 3000) return false;
            HWND h = static_cast<HWND>(idx == 3 ? rp::host->CamWindow(rp::self) : rp::host->XYWindow(rp::self, idx));
            std::string r = CaptureBmp(h, Rest(job->line, 2));
            if (!fresh && r.rfind("ok", 0) == 0) r += "STALE no new frame within 3 s\n";
            Finish(job, r);
            return true;
        }
        try
        {
            if (!w.empty() && w[0] == "batch") Finish(job, Batch(w.size() > 1 ? Rest(job->line, 1) : "t7kb: batch", job->body));
            else Finish(job, RunSimple(job->line, job->body));
        }
        catch (const std::exception& e) { Finish(job, Err(std::string("exception: ") + e.what())); }
        return true;
    }

    // ---- the pipe thread ----------------------------------------------------------------------------------------
    bool WaitIo(HANDLE pipe, OVERLAPPED& ov, DWORD& got)
    {
        HANDLE evs[2] = { ov.hEvent, g_stopEvent };
        if (WaitForMultipleObjects(2, evs, FALSE, INFINITE) != WAIT_OBJECT_0)
        {
            CancelIoEx(pipe, &ov);
            GetOverlappedResult(pipe, &ov, &got, TRUE);
            return false;
        }
        return GetOverlappedResult(pipe, &ov, &got, FALSE) != 0;
    }

    bool Transfer(HANDLE pipe, HANDLE ev, char* p, DWORD n, bool write)
    {
        DWORD done = 0;
        while (done < n)
        {
            OVERLAPPED ov{}; ov.hEvent = ev; DWORD got = 0;
            BOOL ok = write ? WriteFile(pipe, p + done, n - done, nullptr, &ov) : ReadFile(pipe, p + done, n - done, nullptr, &ov);
            if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
            if (!WaitIo(pipe, ov, got) || got == 0) return false;
            done += got;
        }
        return true;
    }

    void Serve(HANDLE pipe, HANDLE ev)
    {
        for (;;)
        {
            uint32_t len = 0;
            if (!Transfer(pipe, ev, reinterpret_cast<char*>(&len), 4, false) || len > (64u << 20)) return;
            std::string msg(len, '\0');
            if (len && !Transfer(pipe, ev, &msg[0], len, false)) return;
            auto job = std::make_shared<Job>();
            size_t nl = msg.find('\n');
            job->line = msg.substr(0, nl);
            if (!job->line.empty() && job->line.back() == '\r') job->line.pop_back();
            job->body = nl == std::string::npos ? std::string() : msg.substr(nl + 1);
            std::string reply;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_queue.push_back(job);
                if (!g_cv.wait_for(lock, std::chrono::seconds(60), [&] { return job->done || g_stop.load(); }) || !job->done)
                {
                    job->abandoned = true;
                    reply = Err("timeout: Radiant's main thread did not run the command within 60 s (a modal dialog?); it will not run later");
                }
                else reply = job->reply;
            }
            uint32_t out = uint32_t(reply.size());
            if (!Transfer(pipe, ev, reinterpret_cast<char*>(&out), 4, true) || (out && !Transfer(pipe, ev, &reply[0], out, true))) return;
        }
    }

    void PipeThread()
    {
        std::wstring name = L"\\\\.\\pipe\\t7_radiant_bridge_" + std::to_wstring(GetCurrentProcessId());
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        while (!g_stop)
        {
            HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1 << 20, 1 << 20, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) { rp::Log("pipe: CreateNamedPipe failed (%lu)", GetLastError()); WaitForSingleObject(g_stopEvent, 1000); continue; }
            OVERLAPPED ov{}; ov.hEvent = ev; DWORD got = 0;
            bool connected = ConnectNamedPipe(pipe, &ov) != 0;
            if (!connected)
            {
                DWORD e = GetLastError();
                connected = e == ERROR_PIPE_CONNECTED || (e == ERROR_IO_PENDING && WaitIo(pipe, ov, got));
            }
            if (connected) Serve(pipe, ev);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        CloseHandle(ev);
    }

    // ---- plugin callbacks ---------------------------------------------------------------------------------------
    int OnLoad(void)
    {
        g_stop = false;
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_thread = std::thread(PipeThread);
        rp::Log("listening on \\\\.\\pipe\\t7_radiant_bridge_%lu", GetCurrentProcessId());
        return 1;
    }

    void OnUnload(void)
    {
        g_stop = true;
        SetEvent(g_stopEvent);
        { std::lock_guard<std::mutex> lock(g_mutex); g_cv.notify_all(); }
        if (g_thread.joinable()) g_thread.join();
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        g_queue.clear();
        g_active.clear();
    }

    void OnTick(void)
    {
        ++g_ticks;
        if (g_inTick) { ++g_nestedTicks; return; }
        g_inTick = true;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            while (!g_queue.empty()) { g_active.push_back(g_queue.front()); g_queue.pop_front(); }
        }
        for (size_t i = 0; i < g_active.size();)
        {
            bool abandoned;
            { std::lock_guard<std::mutex> lock(g_mutex); abandoned = g_active[i]->abandoned; }
            if (abandoned || Run(g_active[i])) g_active.erase(g_active.begin() + std::ptrdiff_t(i));
            else ++i;
        }
        g_inTick = false;
    }

    void OnDraw(const ModToolsDrawContext* ctx)
    {
        if (!ctx) return;
        if (ctx->view == MODTOOLS_VIEW_3D) ++g_frames[3];
        else if (ctx->xy.type >= 0 && ctx->xy.type < 3) ++g_frames[ctx->xy.type];
    }

    const char* OnCommand(const char* cmd)
    {
        static std::string reply;
        if (cmd && std::string(cmd) == "ticks")
        {
            reply = "ticks=" + std::to_string(g_ticks.load()) + " nested=" + std::to_string(g_nestedTicks.load()) + " inTick=" + std::to_string(int(g_inTick)) + " active=" + std::to_string(g_active.size());
            return reply.c_str();
        }
        reply = std::string("pipe \\\\.\\pipe\\t7_radiant_bridge_") + std::to_string(GetCurrentProcessId()) + (cmd ? std::string(" (got '") + cmd + "')" : "");
        return reply.c_str();
    }
}

extern "C" MODTOOLS_PLUGIN_EXPORT const ModToolsPluginApi* ModToolsPluginEntry(ModToolsPlugin* self, const ModToolsHostApi* host)
{
    rp::Bind(self, host);
    static ModToolsPluginApi api = {};
    api.size = sizeof api;
    api.apiVersion = MODTOOLS_PLUGIN_API_VERSION;
    api.name = "T7RadiantBridge";
    api.displayName = "t7kb Radiant bridge (spike)";
    api.version = "0.0.1";
    api.author = "McReaper";
    api.description = "M0 spike: lets t7kb read and edit the open map, move the camera, capture a view and run Radiant's commands over a named pipe.";
    api.OnLoad = OnLoad;
    api.OnUnload = OnUnload;
    api.OnTick = OnTick;
    api.OnDraw = OnDraw;
    api.OnCommand = OnCommand;
    api.apps = MODTOOLS_APP_RADIANT;
    api.url = "https://github.com/McReaper/t7_radiant_bridge";
    return &api;
}
