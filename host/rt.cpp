// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/radiant_addrs.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "addrs.h"
#include "exe.h"
#include "log.h"
#include <windows.h>

namespace host::rt
{
    uintptr_t Rva(uintptr_t idaAddress)
    {
        static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        return base + (idaAddress - kImageBase);
    }

    bool VerifyBinary()
    {
        static int cached = -1;
        if (cached >= 0) return cached != 0;
        const unsigned char* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        host::exe::Identity id;
        bool ok = base && host::exe::Read(base, 0x1000, id) && host::exe::IsRadiant(id);
        HLOG("rt: exe timestamp=0x%08X size=0x%X -> %s", id.timestamp, id.sizeOfImage, ok ? "MATCH" : "MISMATCH");
        cached = ok ? 1 : 0;
        return ok;
    }
}
