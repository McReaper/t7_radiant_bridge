// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hooks.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "editor.h"
#include "addrs.h"
#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace host::editor
{
    using rt::Global;

    namespace
    {
        std::wstring Widen(const char* s)
        {
            if (!s) return L"";
            int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
            std::wstring w(n ? n - 1 : 0, L'\0');
            if (n) MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
            return w;
        }

        // A Radiant map name ("x.map", "map_source\zm\x.map" or absolute) to a full path.
        std::wstring ResolveMapPath(const std::wstring& name)
        {
            auto full = [](const std::wstring& p) { wchar_t b[MAX_PATH * 2]; DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, b, nullptr); return n ? std::wstring(b) : p; };
            if (!PathIsRelativeW(name.c_str()) || PathFileExistsW(name.c_str())) return full(name);
            wchar_t root[MAX_PATH * 2] = {};
            if (GetEnvironmentVariableW(L"TA_TOOLS_PATH", root, MAX_PATH * 2))
            {
                std::wstring a = std::wstring(root) + L"\\" + name;             if (PathFileExistsW(a.c_str())) return full(a);
                std::wstring b = std::wstring(root) + L"\\map_source\\" + name; if (PathFileExistsW(b.c_str())) return full(b);
            }
            return full(name);
        }

        BYTE* CamWnd()
        {
            BYTE* mainWin = *Global<BYTE*>(rt::g_pMainWin);
            if (!mainWin) return nullptr;
            BYTE* qe = *reinterpret_cast<BYTE**>(mainWin + rt::off_MainWin_QEChildren);
            return qe ? *reinterpret_cast<BYTE**>(qe + rt::off_QEChildren_CamWnd) : nullptr;
        }
    }

    std::wstring CurrentMapPath()
    {
        if (!rt::VerifyBinary()) return L"";
        return ResolveMapPath(Widen(Global<char>(rt::g_currentMapName)));
    }

    int Modified() { return rt::VerifyBinary() ? *Global<int>(rt::g_modified) : 0; }
    int PrefabDepth() { return rt::VerifyBinary() ? *Global<int>(rt::g_prefabDepth) : 0; }

    void ForEachEntity(const std::function<void(void*)>& fn)
    {
        if (!rt::VerifyBinary()) return;
        BYTE* list = Global<BYTE>(rt::g_entityList);
        BYTE* e = *reinterpret_cast<BYTE**>(list + rt::off_Entity_ListNext);
        size_t guard = 0;
        while (e && e != list && guard++ < 200000)
        {
            fn(e);
            e = *reinterpret_cast<BYTE**>(e + rt::off_Entity_ListNext);
        }
    }

    size_t SelectionCount()
    {
        if (!rt::VerifyBinary()) return 0;
        BYTE* collection = Global<BYTE>(rt::g_selectedBrushes);
        BYTE* sentinel = *reinterpret_cast<BYTE**>(collection + rt::off_Collection_List);
        if (!sentinel) return 0;
        size_t n = 0, guard = 0;
        for (BYTE* node = *reinterpret_cast<BYTE**>(sentinel + rt::off_BInst_Next);
             node && node != sentinel && guard++ < 100000;
             node = *reinterpret_cast<BYTE**>(node + rt::off_BInst_Next))
        {
            if (*reinterpret_cast<BYTE**>(node + rt::off_BInst_Brush)) ++n;
        }
        return n;
    }

    bool GetCamera(float origin[3], float angles[3])
    {
        BYTE* cam = rt::VerifyBinary() ? CamWnd() : nullptr;
        if (!cam) return false;
        memcpy(origin, cam + rt::off_CamWnd_Origin, 3 * sizeof(float));
        memcpy(angles, cam + rt::off_CamWnd_Angles, 3 * sizeof(float));
        return true;
    }

    bool SetCamera(const float origin[3], const float angles[3])
    {
        BYTE* cam = rt::VerifyBinary() ? CamWnd() : nullptr;
        if (!cam) return false;
        memcpy(cam + rt::off_CamWnd_Origin, origin, 3 * sizeof(float));
        memcpy(cam + rt::off_CamWnd_Angles, angles, 3 * sizeof(float));
        return true;
    }
}
