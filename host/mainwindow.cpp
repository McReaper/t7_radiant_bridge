// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/mainthread.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "mainwindow.h"
#include <cwchar>

namespace host
{
    namespace
    {
        struct FindCtx { DWORD pid; HWND best; int bestScore; };

        BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
        {
            FindCtx* ctx = reinterpret_cast<FindCtx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != ctx->pid || GetWindow(hwnd, GW_OWNER) != nullptr || !IsWindowVisible(hwnd)) return TRUE;

            wchar_t cls[128] = {}, title[256] = {};
            GetClassNameW(hwnd, cls, 128);
            GetWindowTextW(hwnd, title, 256);
            int score = 0;
            if (wcsstr(cls, L"Qt5QWindow")) score += 2;
            if (wcsstr(cls, L"ToolSaveBits") || wcsstr(cls, L"Splash")) score -= 10;
            if (title[0]) score += 1;
            if (wcsstr(title, L"Radiant")) score += 4;
            RECT r; GetWindowRect(hwnd, &r);
            int w = r.right - r.left, h = r.bottom - r.top;
            if (w > 600 && h > 400) score += 2;
            if (w > 1000 && h > 700) score += 6;   // the editor frame is big, the splash is ~656x519
            if (wcsstr(title, L".map")) score += 3;   // after a load the title holds the map path
            if (score > ctx->bestScore) { ctx->bestScore = score; ctx->best = hwnd; }
            return TRUE;
        }
    }

    HWND FindMainWindow()
    {
        FindCtx ctx{ GetCurrentProcessId(), nullptr, 0 };
        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.best;
    }
}
