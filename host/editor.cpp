// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/hooks.cpp at 89423aa) on 2026-10-23.
// Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "editor.h"
#include "addrs.h"
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <mutex>
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

        // ---- the edit pipeline: Radiant function shapes and helpers ----
        using Map_ImportBuffer_t = __int64(__fastcall*)(char** text, unsigned flags, unsigned char regenGuids);
        using ParseSession_t     = void(__fastcall*)(const char* name);
        using Void_t             = void(__fastcall*)();
        using IsOpen_t           = bool(__fastcall*)();
        using UndoBegin_t        = __int64(__fastcall*)(const char* label);
        using Collection1_t      = __int64(__fastcall*)(void* collection);
        using Collection2_t      = __int64(__fastcall*)(void* collection, void* brushInst);
        using EndBatch_t         = __int64(__fastcall*)(void* collection, int, __int64, char);
        using Deselect_t         = __int64(__fastcall*)(char clearDrag);
        using InstFn_t           = void(__fastcall*)(void* entityInst);
        using ListFn_t           = BYTE** (__fastcall*)(void* world, BYTE** out);
        constexpr unsigned kImportFlags = 4;   // Radiant's paste value: the mesh/curve syntax in Brush_Parse (0 is fatal)

        // GUID text ("{...}" or bare) -> GUID. Radiant writes upper-case braced; the client sends it back as written.
        bool ParseGuidText(const std::string& text, GUID& out)
        {
            wchar_t w[64] = {};
            if (text.size() < 36 || text.size() > 40) return false;
            for (size_t i = 0; i < text.size(); ++i) w[i] = wchar_t(text[i]);
            if (text[0] != '{') { std::wstring b = L"{" + std::wstring(w) + L"}"; return CLSIDFromString(b.c_str(), &out) == S_OK; }
            return CLSIDFromString(w, &out) == S_OK;
        }
        bool GuidFromText(const std::string& text, BYTE* out16)
        {
            GUID g;
            if (!ParseGuidText(text, g)) return false;
            memcpy(out16, &g, 16);
            return true;
        }

        // Radiant keeps the label POINTER of every undo operation: labels live in storage that is never freed
        // (a caller's string would dangle after the next map load).
        const char* InternLabel(const char* label)
        {
            static std::mutex m;
            static std::set<std::string> labels;
            std::lock_guard<std::mutex> lock(m);
            return labels.insert(label && *label ? label : "plugin").first->c_str();
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

    void DeselectAll()
    {
        if (!rt::VerifyBinary()) return;
        Func<Deselect_t>(rt::Select_Deselect)(1);
    }

    void SelectBrushInsts(const std::vector<void*>& insts)
    {
        if (!rt::VerifyBinary()) return;
        void* sel = Global<void>(rt::g_selectedBrushes);
        Func<Collection1_t>(rt::Selection_BeginBatch)(sel);
        for (void* b : insts) Func<Collection2_t>(rt::Select_Brush)(sel, b);
        Func<EndBatch_t>(rt::Selection_EndBatch)(sel, 3, 0, 1);
    }

    void BeginUndo(const char* label)
    {
        if (!rt::VerifyBinary() || Func<IsOpen_t>(rt::Undo_IsOpen)()) return;   // join an open operation rather than nest
        Func<UndoBegin_t>(rt::Undo_Begin)(InternLabel(label));
        Func<Collection1_t>(rt::Undo_AddBrushList)(Global<void>(rt::g_selectedBrushes));
    }

    void EndUndo()
    {
        if (!rt::VerifyBinary() || !Func<IsOpen_t>(rt::Undo_IsOpen)()) return;
        void* sel = Global<void>(rt::g_selectedBrushes);
        Func<Collection1_t>(rt::Selection_CloseUndo)(sel);
        Func<Void_t>(rt::Undo_End)();
    }

    bool ImportMapText(const std::string& text, const char* label)
    {
        if (!rt::VerifyBinary()) return false;
        bool undoOpen = Func<IsOpen_t>(rt::Undo_IsOpen)();   // join an edit's open bracket, else make our own
        std::string buf = text;                              // the importer advances the pointer, so give it a writable copy
        char* p = &buf[0];
        void* sel = Global<void>(rt::g_selectedBrushes);
        Func<ParseSession_t>(rt::BeginParseSession)("t7_radiant_bridge");
        if (!undoOpen) { Func<UndoBegin_t>(rt::Undo_Begin)(InternLabel(label)); Func<Collection1_t>(rt::Undo_AddBrushList)(sel); }
        Func<Collection1_t>(rt::Selection_BeginBatch)(sel);
        char* keepLinks = Global<char>(rt::g_importKeepLinks);   // keep target/linkto (Radiant's paste remap drops them)
        char keepLinksWas = *keepLinks;
        *keepLinks = 1;
        Func<Map_ImportBuffer_t>(rt::Map_ImportBuffer)(&p, kImportFlags, 0);
        *keepLinks = keepLinksWas;
        Func<EndBatch_t>(rt::Selection_EndBatch)(sel, 3, 0, 1);
        if (!undoOpen) { Func<Collection1_t>(rt::Selection_CloseUndo)(sel); Func<Void_t>(rt::Undo_End)(); }
        Func<Void_t>(rt::EndParseSession)();
        return true;
    }

    void* FindBrushInst(void* brush)
    {
        if (!brush || !rt::VerifyBinary()) return nullptr;
        BYTE* owner = *reinterpret_cast<BYTE**>(reinterpret_cast<BYTE*>(brush) + rt::off_Brush_Owner);
        void* world = Global<void>(rt::g_world);
        BYTE* first = nullptr; BYTE* endTmp = nullptr;
        Func<ListFn_t>(rt::EntityInst_Begin)(world, &first);
        BYTE** endPtr = Func<ListFn_t>(rt::EntityInst_End)(world, &endTmp);
        BYTE* end = endPtr ? *endPtr : nullptr;
        size_t guard = 0;
        for (BYTE* inst = first; inst && inst != end && guard++ < 200000; inst = *reinterpret_cast<BYTE**>(inst + rt::off_EInst_Next))
        {
            if (*reinterpret_cast<BYTE**>(inst + rt::off_EInst_Entity) != owner) continue;
            BYTE* sentinel = inst + rt::off_EInst_ListSentinel;
            size_t g2 = 0;
            for (BYTE* n = *reinterpret_cast<BYTE**>(sentinel + rt::off_BInst_ListNext);
                 n && n != sentinel && g2++ < 100000;
                 n = *reinterpret_cast<BYTE**>(n + rt::off_BInst_ListNext))
                if (*reinterpret_cast<BYTE**>(n + rt::off_BInst_Brush) == brush) return n;
            return nullptr;
        }
        return nullptr;
    }

    bool DeleteBrushes(const std::vector<void*>& brushes, const char* label)
    {
        if (brushes.empty()) return true;
        if (!rt::VerifyBinary()) return false;
        std::vector<void*> insts;
        for (void* b : brushes) if (void* bi = FindBrushInst(b)) insts.push_back(bi);
        if (insts.empty()) return false;
        bool undoOpen = Func<IsOpen_t>(rt::Undo_IsOpen)();
        void* sel = Global<void>(rt::g_selectedBrushes);
        DeselectAll();
        SelectBrushInsts(insts);
        if (!undoOpen) Func<UndoBegin_t>(rt::Undo_Begin)(InternLabel(label));
        Func<Collection1_t>(rt::Undo_AddBrushList)(sel);   // the brushes about to go must be in the record, inside a bracket too
        Func<Void_t>(rt::Select_Delete)();
        if (!undoOpen) { Func<Collection1_t>(rt::Selection_CloseUndo)(sel); Func<Void_t>(rt::Undo_End)(); }
        return true;
    }

    size_t FindBrushesByGuid(const std::vector<std::string>& guids, std::vector<void*>& out)
    {
        out.assign(guids.size(), nullptr);
        if (guids.empty() || !rt::VerifyBinary()) return 0;
        struct Key { GUID g; size_t index; };
        std::vector<Key> keys;
        for (size_t i = 0; i < guids.size(); ++i) { GUID g; if (ParseGuidText(guids[i], g)) keys.push_back({ g, i }); }
        if (keys.empty()) return 0;
        size_t found = 0, remaining = keys.size();
        ForEachEntity([&](void* ent) {
            if (!remaining) return;
            ForEachEntityBrush(ent, [&](void* bv) {
                if (!remaining) return;
                const GUID* bg = reinterpret_cast<const GUID*>(reinterpret_cast<BYTE*>(bv) + rt::off_Brush_Guid);
                for (Key& k : keys)
                {
                    if (k.index == size_t(-1) || memcmp(&k.g, bg, sizeof(GUID)) != 0) continue;
                    out[k.index] = bv; k.index = size_t(-1); ++found; --remaining;
                    break;
                }
            });
        });
        return found;
    }

    bool EntityFixedSize(void* ent)
    {
        if (!ent || !rt::VerifyBinary()) return false;
        BYTE* eclass = *reinterpret_cast<BYTE**>(reinterpret_cast<BYTE*>(ent) + rt::off_Entity_EClass);
        return eclass && eclass[72] != 0;
    }

    void RequestRedraw(unsigned views)
    {
        if (views && rt::VerifyBinary()) *Global<unsigned>(rt::g_redrawFlags) |= views;
    }

    namespace
    {
        // The selected brushes as brush_t*, from the selection instance list.
        std::vector<void*> SelectedBrushes()
        {
            std::vector<void*> out;
            for (void* bi : SelectedBrushInsts())
                if (void* b = *reinterpret_cast<BYTE**>(reinterpret_cast<BYTE*>(bi) + rt::off_BInst_Brush)) out.push_back(b);
            return out;
        }

        // The live EntityInsts of `ent` found in Radiant's GUID registry (values only compared, nodes walked as a tree).
        std::vector<void*> RegisteredInstsOf(void* ent)
        {
            std::vector<void*> out;
            BYTE* head = *Global<BYTE*>(rt::g_entityRegistry);
            if (!head || !ent) return out;
            std::vector<BYTE*> live;
            void* world = Global<void>(rt::g_world);
            BYTE* first = nullptr; BYTE* endTmp = nullptr;
            Func<ListFn_t>(rt::EntityInst_Begin)(world, &first);
            BYTE** endPtr = Func<ListFn_t>(rt::EntityInst_End)(world, &endTmp);
            BYTE* end = endPtr ? *endPtr : nullptr;
            size_t guard = 0;
            for (BYTE* inst = first; inst && inst != end && guard++ < 200000; inst = *reinterpret_cast<BYTE**>(inst + rt::off_EInst_Next))
                if (*reinterpret_cast<BYTE**>(inst + rt::off_EInst_Entity) == ent) live.push_back(inst);
            if (live.empty()) return out;
            std::vector<BYTE*> stack;
            stack.push_back(*reinterpret_cast<BYTE**>(head + 8));   // root = head->parent (the head itself when empty)
            guard = 0;
            while (!stack.empty() && guard++ < 1000000)
            {
                BYTE* node = stack.back(); stack.pop_back();
                if (!node || node[25]) continue;                    // isnil (the head sentinel)
                BYTE* inst = *reinterpret_cast<BYTE**>(node + 48);
                for (BYTE* l : live) if (l == inst) { out.push_back(inst); break; }
                stack.push_back(*reinterpret_cast<BYTE**>(node + 0));
                stack.push_back(*reinterpret_cast<BYTE**>(node + 16));
            }
            return out;
        }

        // Writing a GUID over a registered entity would strand its registry entry (Radiant's own duplicate fixer takes
        // the instances out, changes the GUID, puts them back): do the same. Brushes: the entity's selected ones (the
        // import selected them), else all of its brushes.
        void RestampEntity(void* ent, const std::vector<void*>& sel, const std::string& entityGuid, const std::string* brushGuids, size_t nGuids)
        {
            std::vector<void*> registered = RegisteredInstsOf(ent);
            for (void* inst : registered) Func<InstFn_t>(rt::EntityRegistry_Remove)(inst);
            GuidFromText(entityGuid, reinterpret_cast<BYTE*>(ent) + rt::off_Entity_Guid);
            for (void* inst : registered) Func<InstFn_t>(rt::EntityRegistry_Add)(inst);
            std::vector<void*> brushes;
            for (void* b : sel) if (BrushOwner(b) == ent) brushes.push_back(b);
            if (brushes.empty()) ForEachEntityBrush(ent, [&](void* b) { brushes.push_back(b); });
            size_t n = brushes.size() < nGuids ? brushes.size() : nGuids;
            for (size_t i = 0; i < n; ++i) GuidFromText(brushGuids[i], reinterpret_cast<BYTE*>(brushes[i]) + rt::off_Brush_Guid);
        }
    }

    bool RestampImportedGuids(const std::string& entityGuid, const std::vector<std::string>& brushGuids)
    {
        if (!rt::VerifyBinary()) return false;
        std::vector<void*> sel = SelectedBrushes();
        if (entityGuid.empty())
        {
            if (sel.size() != brushGuids.size()) return false;
            for (size_t i = 0; i < sel.size(); ++i) GuidFromText(brushGuids[i], reinterpret_cast<BYTE*>(sel[i]) + rt::off_Brush_Guid);
            return true;
        }
        BYTE* list = Global<BYTE>(rt::g_entityList);
        BYTE* ent = *reinterpret_cast<BYTE**>(list + rt::off_Entity_ListPrev);   // the import's newest entity
        if (!ent || ent == list) return false;
        RestampEntity(ent, sel, entityGuid, brushGuids.data(), brushGuids.size());
        return true;
    }

    size_t RestampImportedEntities(const std::vector<std::string>& entityGuids, const std::vector<std::vector<std::string>>& brushGuids)
    {
        if (!rt::VerifyBinary()) return 0;
        size_t n = entityGuids.size();
        if (n == 0 || brushGuids.size() != n) return 0;
        // one import appends its entities to the global list in text order: the last n entries, oldest first
        BYTE* list = Global<BYTE>(rt::g_entityList);
        std::vector<void*> ents(n, nullptr);
        BYTE* e = list;
        for (size_t i = n; i-- > 0;)
        {
            e = *reinterpret_cast<BYTE**>(e + rt::off_Entity_ListPrev);
            if (!e || e == list) return 0;
            ents[i] = e;
        }
        std::vector<void*> sel = SelectedBrushes();
        for (size_t i = 0; i < n; ++i) RestampEntity(ents[i], sel, entityGuids[i], brushGuids[i].data(), brushGuids[i].size());
        return n;
    }
}
