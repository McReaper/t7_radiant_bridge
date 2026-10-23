// The standalone host: T7RadiantBridge with its own minimal plugin host, so it
// runs in a Radiant that MTPlugins doesn't load into. t7kb injects it
// (LoadLibraryW) when it opens a map; nothing is written into the BO3 install.
//
// Milestone 1 (this file as it stands): the DLL loads off the loader lock,
// steps aside when MTPlugins is present, checks the Radiant build, and logs.
// It installs no hooks and loads no plugin yet — safe to inject and confirm the
// loading end to end. The hooks, the host-API table and the plugin dispatch
// come next, each tested live.
#include <windows.h>
#include <cwctype>
#include <string>
#include "exe.h"
#include "coexist.h"
#include "log.h"

namespace
{
    HMODULE g_self = nullptr;

    constexpr const char* kHostVersion = "1.2.0";

    std::wstring ExeName()
    {
        wchar_t p[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
        std::wstring s(p);
        size_t slash = s.find_last_of(L"\\/");
        std::wstring name = slash == std::wstring::npos ? s : s.substr(slash + 1);
        for (wchar_t& c : name) c = static_cast<wchar_t>(towlower(c));
        return name;
    }

    bool Headless()
    {
        std::wstring cl = GetCommandLineW();
        for (wchar_t& c : cl) c = static_cast<wchar_t>(towlower(c));
        return cl.find(L" -led") != std::wstring::npos;   // the Light step's bake: no window, no bridge
    }

    bool VerifyRadiant()
    {
        host::exe::Identity id;
        const unsigned char* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        if (!base || !host::exe::Read(base, 0x1000, id))
        {
            HLOG("host: could not read the exe's PE header");
            return false;
        }
        if (!host::exe::IsRadiant(id))
        {
            HLOG("host: this Radiant (timestamp 0x%08X, size 0x%X) is not the build the host's addresses describe "
                 "(0x%08X, 0x%X): staying passive", id.timestamp, id.sizeOfImage, host::exe::kRadiant.timestamp, host::exe::kRadiant.sizeOfImage);
            return false;
        }
        return true;
    }

    DWORD WINAPI InitThread(LPVOID)
    {
        host::log::Init(g_self);
        HLOG("==== T7RadiantBridge standalone host %s attached to %ls, pid %lu ====", kHostVersion, ExeName().c_str(), GetCurrentProcessId());

        if (ExeName() != L"radiant_modtools.exe")
        {
            HLOG("host: not Radiant_modtools.exe: staying passive");
            return 0;
        }
        if (Headless())
        {
            HLOG("host: a headless -led run: staying passive");
            return 0;
        }

        // MTPlugins' loader can load its host a moment after us (its delay_ms), so recheck for a few seconds.
        for (int i = 0; i < 40; ++i)
        {
            if (!host::coexist::MTPluginsPresent()) break;
            if (i == 0) HLOG("host: MTPlugins is hosting plugins here; t7kb goes through it, so this host stays passive");
            Sleep(250);
            if (i == 39)
            {
                HLOG("host: MTPlugins still present after 10 s: staying passive");
                return 0;
            }
        }

        if (!VerifyRadiant()) return 0;

        HLOG("host: Radiant verified, MTPlugins absent. Milestone 1: ready, no hooks installed yet.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        // Pin the DLL: our hooks will point into it, and it must not be unloaded under them.
        HMODULE keep = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &keep);
        // Off the loader lock: InitThread calls LoadLibrary-ish work and waits.
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
