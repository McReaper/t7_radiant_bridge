// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/plugins.cpp and src/mainthread.cpp at 89423aa)
// on 2026-10-23. Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "seh.h"
#include "log.h"
#include <windows.h>

namespace host::seh
{
    namespace
    {
        // The exception filter records what faulted; no C++ objects here (the
        // enclosing __try forbids unwinding).
        DWORD g_code = 0;
        void* g_addr = nullptr;

        int Filter(EXCEPTION_POINTERS* ep)
        {
            g_code = ep->ExceptionRecord->ExceptionCode;
            g_addr = ep->ExceptionRecord->ExceptionAddress;
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // No destructors in scope: this function holds the __try.
        bool Guarded(void (*fn)(void*), void* arg)
        {
            __try { fn(arg); return true; }
            __except (Filter(GetExceptionInformation())) { return false; }
        }

        void LogFault(const char* what)
        {
            HMODULE mod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(g_addr), &mod);
            wchar_t name[MAX_PATH] = L"?";
            if (mod) GetModuleFileNameW(mod, name, MAX_PATH);
            uintptr_t off = g_addr && mod ? reinterpret_cast<uintptr_t>(g_addr) - reinterpret_cast<uintptr_t>(mod) : 0;
            HLOG("host: plugin faulted in %s (exception 0x%08X at %ls+0x%llX): disabled", what, g_code, name, static_cast<unsigned long long>(off));
        }
    }

    bool Run(void (*fn)(void*), void* arg, const char* what)
    {
        if (Guarded(fn, arg)) return true;
        LogFault(what);
        return false;
    }
}
