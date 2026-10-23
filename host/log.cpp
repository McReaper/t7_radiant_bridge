// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/log.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// Simplified: one file beside the DLL, no shared RadiantPlugins.log and no per-plugin prefix.
#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace host::log
{
    namespace
    {
        std::mutex g_mutex;
        std::wstring g_path;
        long g_written = 0;
        constexpr long kCap = 1 << 20;   // 1 MiB: a fault should leave a short, readable tail

        std::wstring PathBeside(void* hModule)
        {
            wchar_t p[MAX_PATH * 2] = {};
            GetModuleFileNameW(static_cast<HMODULE>(hModule), p, MAX_PATH * 2);
            std::wstring s(p);
            size_t slash = s.find_last_of(L"\\/");
            std::wstring dir = slash == std::wstring::npos ? L"." : s.substr(0, slash);
            return dir + L"\\T7RadiantBridge.log";
        }
    }

    void Init(void* hModule)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_path = PathBeside(hModule);
        g_written = 0;
        if (FILE* f = nullptr; _wfopen_s(&f, g_path.c_str(), L"w") == 0 && f) fclose(f);   // truncate
    }

    void Printf(const char* fmt, ...)
    {
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (n > static_cast<int>(sizeof buf) - 2) n = sizeof buf - 2;
        buf[n] = '\n';
        buf[n + 1] = 0;

        char line[2080];
        int m = snprintf(line, sizeof line, "[%lu] %s", GetCurrentProcessId(), buf);
        OutputDebugStringA(line);

        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_path.empty() || g_written >= kCap) return;
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_path.c_str(), L"a") != 0 || !f) return;
        fwrite(line, 1, m > 0 ? static_cast<size_t>(m) : 0, f);
        fclose(f);
        g_written += m > 0 ? m : 0;
    }
}
