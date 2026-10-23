// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hooks.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "editor.h"
#include "addrs.h"
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <set>

#pragma comment(lib, "shlwapi.lib")

namespace host::editor
{
    using rt::Global;
    using rt::Func;

    namespace
    {
        using Entity_Write_t = int(__fastcall*)(BYTE* entity, void* crtFile, char selectedOnly);

        std::string GuidText(const BYTE* guid16)
        {
            wchar_t w[64] = {};
            if (StringFromGUID2(*reinterpret_cast<const GUID*>(guid16), w, 64) <= 0) return "";
            char a[64] = {};
            WideCharToMultiByte(CP_ACP, 0, w, -1, a, 64, nullptr, nullptr);
            return a;
        }
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

    void ForEachEntityBrush(void* ent, const std::function<void(void*)>& fn)
    {
        if (!ent) return;
        BYTE* e = reinterpret_cast<BYTE*>(ent);
        BYTE* b = *reinterpret_cast<BYTE**>(e + rt::off_Entity_BrushNext);
        size_t guard = 0;
        while (b && b != e && guard++ < 1000000)
        {
            fn(b);
            b = *reinterpret_cast<BYTE**>(b + rt::off_Brush_Next);
        }
    }

    void* WorldEntity()
    {
        if (!rt::VerifyBinary()) return nullptr;
        BYTE* inst = *Global<BYTE*>(rt::g_worldEntity);
        return inst ? *reinterpret_cast<BYTE**>(inst + rt::off_EInst_Entity) : nullptr;
    }

    void* BrushOwner(void* brush) { return brush ? *reinterpret_cast<BYTE**>(reinterpret_cast<BYTE*>(brush) + rt::off_Brush_Owner) : nullptr; }
    void* BrushOfInst(void* inst) { return inst ? *reinterpret_cast<BYTE**>(reinterpret_cast<BYTE*>(inst) + rt::off_BInst_Brush) : nullptr; }

    std::vector<void*> SelectedBrushInsts()
    {
        std::vector<void*> out;
        if (!rt::VerifyBinary()) return out;
        BYTE* collection = Global<BYTE>(rt::g_selectedBrushes);
        BYTE* sentinel = *reinterpret_cast<BYTE**>(collection + rt::off_Collection_List);
        if (!sentinel) return out;
        size_t guard = 0;
        for (BYTE* node = *reinterpret_cast<BYTE**>(sentinel + rt::off_BInst_Next);
             node && node != sentinel && guard++ < 100000;
             node = *reinterpret_cast<BYTE**>(node + rt::off_BInst_Next))
            out.push_back(node);
        return out;
    }
    std::string EntityGuid(void* ent) { return ent ? GuidText(reinterpret_cast<BYTE*>(ent) + rt::off_Entity_Guid) : ""; }
    std::string BrushGuid(void* brush) { return brush ? GuidText(reinterpret_cast<BYTE*>(brush) + rt::off_Brush_Guid) : ""; }

    std::vector<void*> FindEntitiesByGuid(const std::vector<std::string>& guids)
    {
        std::vector<void*> out(guids.size(), nullptr);
        if (guids.empty()) return out;
        // a linear walk, matching each entity's GUID text (case-insensitively, as Radiant writes upper case)
        ForEachEntity([&](void* e) {
            std::string g = EntityGuid(e);
            for (size_t i = 0; i < guids.size(); ++i)
                if (!out[i] && _stricmp(g.c_str(), guids[i].c_str()) == 0) out[i] = e;
        });
        return out;
    }

    bool WriteEntityText(void* ent, std::string& out)
    {
        if (!ent || !rt::VerifyBinary()) return false;
        // Radiant's writer takes a FILE* from its own CRT (MSVCR110). One scratch file per thread is rewound per entity.
        static HMODULE crt = GetModuleHandleW(L"MSVCR110.dll");
        using wfopen_t = void* (__cdecl*)(const wchar_t*, const wchar_t*);
        using fclose_t = int (__cdecl*)(void*);
        using fseek_t = int (__cdecl*)(void*, long, int);
        using ftell_t = long (__cdecl*)(void*);
        using fread_t = size_t (__cdecl*)(void*, size_t, size_t, void*);
        static auto cr_wfopen = crt ? reinterpret_cast<wfopen_t>(GetProcAddress(crt, "_wfopen")) : nullptr;
        static auto cr_fclose = crt ? reinterpret_cast<fclose_t>(GetProcAddress(crt, "fclose")) : nullptr;
        static auto cr_fseek = crt ? reinterpret_cast<fseek_t>(GetProcAddress(crt, "fseek")) : nullptr;
        static auto cr_ftell = crt ? reinterpret_cast<ftell_t>(GetProcAddress(crt, "ftell")) : nullptr;
        static auto cr_fread = crt ? reinterpret_cast<fread_t>(GetProcAddress(crt, "fread")) : nullptr;
        if (!cr_wfopen || !cr_fclose || !cr_fseek || !cr_ftell || !cr_fread) return false;

        // A plain static, not thread_local: an injected DLL's dynamic TLS is unreliable on the main thread, which
        // existed before the injection, and the bridge's handlers (the only callers) all run on the main thread.
        static void* t_file = nullptr;
        if (!t_file)
        {
            wchar_t dir[MAX_PATH]; GetTempPathW(MAX_PATH, dir);
            wchar_t path[MAX_PATH]; _snwprintf_s(path, _TRUNCATE, L"%sT7RadiantBridge_ent_%lu.map", dir, GetCurrentProcessId());
            t_file = cr_wfopen(path, L"w+bTD");
            if (!t_file) return false;
        }
        out.clear();
        if (cr_fseek(t_file, 0, SEEK_SET) != 0) { cr_fclose(t_file); t_file = nullptr; return false; }
        Func<Entity_Write_t>(rt::Entity_Write)(reinterpret_cast<BYTE*>(ent), t_file, 0);
        long size = cr_ftell(t_file);
        if (size < 0 || cr_fseek(t_file, 0, SEEK_SET) != 0) { cr_fclose(t_file); t_file = nullptr; return false; }
        out.resize(size_t(size));
        if (size > 0 && cr_fread(&out[0], 1, size_t(size), t_file) != size_t(size)) { out.clear(); cr_fclose(t_file); t_file = nullptr; return false; }
        return true;
    }

    bool WriteEntityTextWith(void* entV, const std::vector<void*>& extraBrushes, std::string& out)
    {
        if (!entV || !rt::VerifyBinary()) return false;
        BYTE* ent = reinterpret_cast<BYTE*>(entV);
        std::set<BYTE*> linked;
        ForEachEntityBrush(ent, [&](void* b) { linked.insert(reinterpret_cast<BYTE*>(b)); });
        std::vector<std::pair<BYTE**, BYTE*>> restore;   // each word written, with its old value, undone in reverse
        for (void* bv : extraBrushes)
        {
            BYTE* b = reinterpret_cast<BYTE*>(bv);
            if (!b || linked.count(b)) continue;
            if (*reinterpret_cast<BYTE**>(b + rt::off_Brush_Owner) != ent) continue;
            BYTE** tailNext = reinterpret_cast<BYTE**>(*reinterpret_cast<BYTE**>(ent + rt::off_Entity_BrushPrev) + rt::off_Brush_Next);
            BYTE** entPrevW = reinterpret_cast<BYTE**>(ent + rt::off_Entity_BrushPrev);
            BYTE** bPrev = reinterpret_cast<BYTE**>(b + rt::off_Brush_Prev);
            BYTE** bNext = reinterpret_cast<BYTE**>(b + rt::off_Brush_Next);
            restore.push_back({ bPrev, *bPrev }); restore.push_back({ bNext, *bNext });
            restore.push_back({ tailNext, *tailNext }); restore.push_back({ entPrevW, *entPrevW });
            *bPrev = *entPrevW; *bNext = ent; *tailNext = b; *entPrevW = b;
            linked.insert(b);
        }
        bool ok = WriteEntityText(ent, out);
        for (size_t i = restore.size(); i-- > 0;) *restore[i].first = restore[i].second;
        return ok;
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
