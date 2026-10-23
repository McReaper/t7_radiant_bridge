#include "coexist.h"
#include <windows.h>
#include <string>

namespace host::coexist
{
    namespace
    {
        // The directory of the running exe (Radiant's is <root>\bin).
        std::wstring ExeDir()
        {
            wchar_t p[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
            std::wstring s(p);
            size_t slash = s.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : s.substr(0, slash);
        }

        bool FileExists(const std::wstring& p)
        {
            DWORD a = GetFileAttributesW(p.c_str());
            return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        }

        // MTPlugins' d3d9.dll loader in bin\, enabled unless RadiantLoader.ini
        // says enabled=0.
        bool LoaderInstalledAndEnabled()
        {
            std::wstring bin = ExeDir();
            if (!FileExists(bin + L"\\d3d9.dll")) return false;
            std::wstring ini = bin + L"\\RadiantLoader.ini";
            if (!FileExists(ini)) return false;   // another program's d3d9.dll
            return GetPrivateProfileIntW(L"loader", L"enabled", 1, ini.c_str()) != 0;
        }
    }

    bool MTPluginsPresent()
    {
        return GetModuleHandleW(L"RadiantPlugins.dll") != nullptr || LoaderInstalledAndEnabled();
    }
}
