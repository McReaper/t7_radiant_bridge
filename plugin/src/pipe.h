// The named pipe: one accept thread, one thread per connection. Pipe threads only move bytes; requests go to the
// dispatcher (bridge.cpp), which runs them on Radiant's main thread.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "text.h"

namespace bridge
{
    enum Event : uint32_t
    {
        kEventConsole = 1,
        kEventMap = 2,
    };

    struct Pending
    {
        Header h;
        std::string body;
    };

    struct Conn
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        HANDLE sendEvent = nullptr;          // auto-reset: frames wait in `out`
        std::mutex m;
        std::deque<std::string> out;
        size_t outBytes = 0;
        std::atomic<bool> closed{ false };
        std::atomic<bool> done{ false };     // its thread has returned
        std::atomic<uint32_t> events{ 0 };   // the Event bits it subscribed to
        std::thread thread;

        // Radiant's main thread only
        bool hello = false;
        bool running = false;                // one of its requests has not replied yet
        std::deque<Pending> pending;

        ~Conn();
    };

    using PayloadFn = void (*)(const std::shared_ptr<Conn>& c, std::string&& payload);   // a pipe thread, per request frame
    using ClosedFn = void (*)(const std::shared_ptr<Conn>& c);                          // a pipe thread, once

    bool StartPipe(const std::wstring& name, PayloadFn onPayload, ClosedFn onClosed);
    void StopPipe();
    // Queues a payload to one connection, any thread. A client that stops reading is dropped once 512 MiB wait.
    void Send(const std::shared_ptr<Conn>& c, const std::string& payload);
    // Sends a payload to every connection subscribed to `event`, any thread.
    void Broadcast(uint32_t event, const std::string& payload);
    size_t ConnectionCount();
}
