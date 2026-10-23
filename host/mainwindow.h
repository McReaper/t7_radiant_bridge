// The editor's main window, found among the process's top-level windows.
#pragma once
#include <windows.h>

namespace host
{
    // The best candidate for Radiant's main window in this process, or nullptr
    // until it exists. Rescans each call (it appears after the splash).
    HWND FindMainWindow();
}
