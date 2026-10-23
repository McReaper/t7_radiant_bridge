// The standalone host's own log, T7RadiantBridge.log beside the DLL: the
// injected build has no MTPlugins RadiantPlugins.log to share. Truncated each
// start, capped, every line tagged with the pid; also to OutputDebugString.
#pragma once

namespace host::log
{
    // Opens the log beside the DLL (hModule), truncating it. Safe before it: a
    // log call with no open file only reaches OutputDebugString.
    void Init(void* hModule);

    void Printf(const char* fmt, ...);
}

#define HLOG(...) ::host::log::Printf(__VA_ARGS__)
