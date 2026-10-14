#include "pipe.h"
#include "modtools_plugin.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace bridge
{
    namespace
    {
        constexpr uint32_t kMaxRequest = 64u << 20;
        constexpr size_t kMaxQueued = size_t(512) << 20;

        std::wstring g_name;
        PayloadFn g_onPayload = nullptr;
        ClosedFn g_onClosed = nullptr;
        HANDLE g_stop = nullptr;    // manual-reset: the plugin is unloading
        std::thread g_accept;
        std::mutex g_connsMutex;
        std::vector<std::shared_ptr<Conn>> g_conns;

        // Waits for an overlapped operation, or for the stop event, which cancels it. True when it completed.
        bool Wait(HANDLE h, OVERLAPPED& ov, DWORD& got)
        {
            HANDLE evs[2] = { ov.hEvent, g_stop };
            if (WaitForMultipleObjects(2, evs, FALSE, INFINITE) != WAIT_OBJECT_0)
            {
                CancelIoEx(h, &ov);
                GetOverlappedResult(h, &ov, &got, TRUE);
                return false;
            }
            return GetOverlappedResult(h, &ov, &got, FALSE) != 0;
        }

        bool WriteAll(HANDLE h, HANDLE ev, const std::string& s)
        {
            size_t done = 0;
            while (done < s.size())
            {
                OVERLAPPED ov{};
                ov.hEvent = ev;
                DWORD got = 0, n = DWORD(std::min<size_t>(s.size() - done, size_t(1) << 20));
                if (!WriteFile(h, s.data() + done, n, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
                if (!Wait(h, ov, got) || got == 0) return false;
                done += got;
            }
            return true;
        }

        bool Flush(Conn& c, HANDLE ev)
        {
            for (;;)
            {
                std::string next;
                {
                    std::lock_guard<std::mutex> lock(c.m);
                    if (c.out.empty()) return true;
                    next = std::move(c.out.front());
                    c.out.pop_front();
                    c.outBytes -= next.size();
                }
                if (!WriteAll(c.pipe, ev, next)) return false;
            }
        }

        // Cuts the frames out of `buf` and hands them over. False on a frame over the limit.
        bool Deliver(const std::shared_ptr<Conn>& c, std::string& buf)
        {
            size_t at = 0;
            while (buf.size() - at >= 4)
            {
                uint32_t len = 0;
                memcpy(&len, buf.data() + at, 4);
                if (len > kMaxRequest) return false;
                if (buf.size() - at - 4 < len) break;
                g_onPayload(c, buf.substr(at + 4, len));
                at += 4 + size_t(len);
            }
            buf.erase(0, at);
            return true;
        }

        // A connection's thread: reads requests, writes what Send queued, until the client leaves or the plugin unloads.
        void Serve(std::shared_ptr<Conn> c)
        {
            HANDLE readEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            HANDLE writeEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::vector<char> chunk(64 * 1024);
            std::string buf;
            OVERLAPPED rov{};
            bool reading = false;
            while (!c->closed)
            {
                if (!reading)
                {
                    rov = OVERLAPPED{};
                    rov.hEvent = readEv;
                    if (!ReadFile(c->pipe, chunk.data(), DWORD(chunk.size()), nullptr, &rov) && GetLastError() != ERROR_IO_PENDING) break;
                    reading = true;
                }
                HANDLE evs[3] = { readEv, c->sendEvent, g_stop };
                DWORD w = WaitForMultipleObjects(3, evs, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0 + 1)
                {
                    if (c->closed || !Flush(*c, writeEv)) break;
                    continue;
                }
                if (w != WAIT_OBJECT_0) break;
                DWORD got = 0;
                reading = false;
                if (!GetOverlappedResult(c->pipe, &rov, &got, FALSE) || got == 0) break;
                buf.append(chunk.data(), got);
                if (!Deliver(c, buf)) break;
            }
            c->closed = true;
            if (reading)
            {
                DWORD got = 0;
                CancelIoEx(c->pipe, &rov);
                GetOverlappedResult(c->pipe, &rov, &got, TRUE);
            }
            DisconnectNamedPipe(c->pipe);
            CloseHandle(c->pipe);
            c->pipe = INVALID_HANDLE_VALUE;
            CloseHandle(readEv);
            CloseHandle(writeEv);
            g_onClosed(c);
            c->done = true;
        }

        // Joins the threads of connections that ended (g_connsMutex held).
        void Reap()
        {
            for (auto it = g_conns.begin(); it != g_conns.end();)
            {
                if (!(*it)->done)
                {
                    ++it;
                    continue;
                }
                if ((*it)->thread.joinable()) (*it)->thread.join();
                it = g_conns.erase(it);
            }
        }

        void Accept()
        {
            HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            bool first = true, warned = false;
            while (WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT)
            {
                // the first instance must be ours: another process holding the name would get our clients
                DWORD open = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
                DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
                HANDLE h = CreateNamedPipeW(g_name.c_str(), open, mode, PIPE_UNLIMITED_INSTANCES, 1 << 20, 1 << 20, 0, nullptr);
                if (h == INVALID_HANDLE_VALUE)
                {
                    if (!warned) rp::Log("pipe: CreateNamedPipe failed (%lu), retrying every second", GetLastError());
                    warned = true;
                    WaitForSingleObject(g_stop, 1000);
                    continue;
                }
                first = false;
                OVERLAPPED ov{};
                ov.hEvent = ev;
                DWORD got = 0;
                bool connected = ConnectNamedPipe(h, &ov) != 0;
                if (!connected)
                {
                    DWORD e = GetLastError();
                    connected = e == ERROR_PIPE_CONNECTED || (e == ERROR_IO_PENDING && Wait(h, ov, got));
                }
                if (!connected)
                {
                    CloseHandle(h);
                    continue;
                }
                auto c = std::make_shared<Conn>();
                c->pipe = h;
                c->sendEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                std::lock_guard<std::mutex> lock(g_connsMutex);
                Reap();
                g_conns.push_back(c);
                c->thread = std::thread(Serve, c);
            }
            CloseHandle(ev);
        }
    }

    Conn::~Conn()
    {
        if (sendEvent) CloseHandle(sendEvent);
    }

    bool StartPipe(const std::wstring& name, PayloadFn onPayload, ClosedFn onClosed)
    {
        g_name = name;
        g_onPayload = onPayload;
        g_onClosed = onClosed;
        g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stop) return false;
        g_accept = std::thread(Accept);
        return true;
    }

    void StopPipe()
    {
        if (!g_stop) return;
        SetEvent(g_stop);
        if (g_accept.joinable()) g_accept.join();
        std::vector<std::shared_ptr<Conn>> all;
        {
            std::lock_guard<std::mutex> lock(g_connsMutex);
            all.swap(g_conns);
        }
        for (auto& c : all)
            if (c->thread.joinable()) c->thread.join();
        CloseHandle(g_stop);
        g_stop = nullptr;
    }

    void Send(const std::shared_ptr<Conn>& c, const std::string& payload)
    {
        if (!c || c->closed) return;
        std::string f = Frame(payload);
        {
            std::lock_guard<std::mutex> lock(c->m);
            if (c->outBytes + f.size() > kMaxQueued) c->closed = true;
            else
            {
                c->outBytes += f.size();
                c->out.push_back(std::move(f));
            }
        }
        SetEvent(c->sendEvent);
    }

    void Broadcast(uint32_t event, const std::string& payload)
    {
        std::vector<std::shared_ptr<Conn>> to;
        {
            std::lock_guard<std::mutex> lock(g_connsMutex);
            for (auto& c : g_conns)
                if (!c->closed && (c->events & event)) to.push_back(c);
        }
        for (auto& c : to) Send(c, payload);
    }

    size_t ConnectionCount()
    {
        std::lock_guard<std::mutex> lock(g_connsMutex);
        size_t n = 0;
        for (auto& c : g_conns)
            if (!c->closed) ++n;
        return n;
    }
}
