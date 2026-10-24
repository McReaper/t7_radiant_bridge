// The protocol's commands (PROTOCOL.md), each run on Radiant's main thread by the dispatcher.
#pragma once
#include <memory>
#include <string>
#include "modtools_plugin.h"
#include "pipe.h"
#include "text.h"
#include "version.h"

namespace bridge
{
    // The requests that answer while a modal dialog is open.
    bool IsFast(const std::string& command);

    // Runs one request. False when its reply comes later through Complete (a capture waits for frames). `blocked`:
    // a dialog named `dialog` is open, so only the fast requests are asked to run.
    bool Run(const std::shared_ptr<Conn>& c, const Pending& p, Reply& out, bool blocked, const std::string& dialog);

    // The capture in progress: OnDraw counts frames and projects its points, OnTick takes the picture.
    bool CaptureActive();
    void CaptureDraw(const ModToolsDrawContext* ctx);
    void CaptureTick();
    void CaptureCancel();

    // The dispatcher's: sends a late reply and lets that connection's next request start.
    void Complete(const std::shared_ptr<Conn>& c, uint64_t id, const Reply& r);
}
