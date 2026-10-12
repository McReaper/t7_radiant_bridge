#include "radiant.h"
#include "modtools_plugin.hpp"
#include "text.h"
#include <windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace bridge
{
    std::string Utf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }

    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
        std::wstring w(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
        return w;
    }
}

namespace bridge::radiant
{
    namespace
    {
        // IDA addresses of Radiant_modtools.exe (no ASLR), as MTPlugins found them (RadiantPlus actionlog.cpp and
        // fxswap.cpp, the host's radiant_addrs.h).
        constexpr uintptr_t kSysPrintf = 0x140D33210;          // int (const char* fmt, ...): console and Radiant.log, "HH:MM:SS: " prefixed
        constexpr uintptr_t kUndoIsOpen = 0x140C16ED0;         // bool (): the top undo operation is still open
        constexpr uintptr_t kUndoBegin = 0x140C164D0;          // void (const char* label), the label kept by pointer
        constexpr uintptr_t kUndoAddEntity = 0x140C168D0;      // void (entity_t*)
        constexpr uintptr_t kUndoEnd = 0x140C16520;            // void (): marks the map modified, relabels Edit > Undo
        constexpr uintptr_t kSetKeyValue = 0x140204A80;        // bool (entity_t*, const std::string& key, const std::string& value)
        constexpr uintptr_t kEntityInfoRefresh = 0x140858AB0;  // QtEntityInfo::EntitySelectionChanged(this)
        constexpr uintptr_t kMainWin = 0x1444C6560;            // RadiantMainWin*
        constexpr uintptr_t kBuildLighting = 0x1402F31D0;      // void (CCamWnd*, bool sun, bool probes): the 3D view's bolt
        constexpr size_t kMainWinBridge = 64;                  // RadiantMainWin -> its Qt bridge
        constexpr size_t kBridgeCamWnd = 240;                  // bridge -> CCamWnd*
        constexpr size_t kBridgeEntityInfo = 312;              // bridge -> QtEntityInfo*
        constexpr size_t kCamViewMode = 232;                   // CCamWnd int: 4 = real lighting, the only mode the bake steps in

        // MSVC 2012's std::string, which Radiant's functions take: 16 bytes inline or a pointer, then size and capacity.
        struct MsvcString
        {
            union
            {
                char buf[16];
                const char* ptr;
            };
            size_t size;
            size_t cap;
        };

        using SysPrintfFn = int (*)(const char* fmt, ...);
        using BoolFn = bool (*)();
        using LabelFn = void (*)(const char* label);
        using EntityFn = void (*)(void* entity);
        using VoidFn = void (*)();
        using SetKeyValueFn = bool (*)(void* entity, const MsvcString* key, const MsvcString* value);
        using BuildLightingFn = void(__fastcall*)(void* cam, uint8_t sun, uint8_t probes);
        using MetacastFn = void* (*)(void* obj, const char* cls);   // QObject's vtable slot 1

        SysPrintfFn g_sysPrintf = nullptr;    // the original, once hooked
        BoolFn g_undoIsOpen = nullptr;
        LabelFn g_undoBegin = nullptr;
        EntityFn g_undoAddEntity = nullptr;
        VoidFn g_undoEnd = nullptr;
        SetKeyValueFn g_setKeyValue = nullptr;
        EntityFn g_entityInfoRefresh = nullptr;
        void** g_mainWin = nullptr;
        BuildLightingFn g_buildLighting = nullptr;
        DWORD g_mainThread = 0;

        struct Qt
        {
            void* (*activeModal)() = nullptr;
            uintptr_t (*winId)(const void*) = nullptr;
            bool (*isCheckable)(const void*) = nullptr;
            bool (*isChecked)(const void*) = nullptr;
            bool (*isEnabled)(const void*) = nullptr;
            void (*activate)(void*, int) = nullptr;   // QAction::activate(ActionEvent), 0 = Trigger
        } qt;

        template <class T> T At(uintptr_t ida)
        {
            return reinterpret_cast<T>(rp::host->RadiantAddress ? rp::host->RadiantAddress(rp::self, ida) : nullptr);
        }

        template <class T> void Get(T& fn, HMODULE m, const char* name)
        {
            fn = m ? reinterpret_cast<T>(GetProcAddress(m, name)) : nullptr;
        }

        void* Metacast(void* obj, const char* cls) { return obj ? (*reinterpret_cast<MetacastFn**>(obj))[1](obj, cls) : nullptr; }

        char* Bridge()
        {
            char* main = g_mainWin ? static_cast<char*>(*g_mainWin) : nullptr;
            return main ? *reinterpret_cast<char**>(main + kMainWinBridge) : nullptr;
        }

        // ---- the console ------------------------------------------------------------------------------------
        std::atomic<LineFn> g_onLine{ nullptr };

        // One print is one entry of Radiant's console, line end or not ("All Probes Done (1.61 s)" has none);
        // a print of several lines gives one line each.
        void Forward(const char* text)
        {
            LineFn fn = g_onLine.load();
            if (!fn || !text) return;
            for (const std::string& l : Lines(text)) fn(l);
        }

        int HookedSysPrintf(const char* fmt, ...)
        {
            char small[4096];
            std::string big;
            const char* text = small;
            va_list ap;
            va_start(ap, fmt);
            va_list again;
            va_copy(again, ap);
            int n = vsnprintf(small, sizeof small, fmt, ap);
            if (n < 0) small[0] = 0;
            else if (size_t(n) >= sizeof small)
            {
                big.resize(size_t(n) + 1);
                vsnprintf(&big[0], big.size(), fmt, again);
                text = big.c_str();
            }
            va_end(again);
            va_end(ap);
            Forward(text);
            return g_sysPrintf("%s", text);
        }

        // ---- raw calls, each behind SEH so a fault is a refusal rather than Radiant's crash ---------------
        // 1 set, 0 refused by Radiant, -1 faulted
        int CallSetKey(void* e, const char* label, const MsvcString* k, const MsvcString* v)
        {
            __try
            {
                g_undoBegin(label);
                g_undoAddEntity(e);
                bool set = g_setKeyValue(e, k, v);
                g_undoEnd();
                return set ? 1 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }

        bool CallBuildLighting(void* cam)
        {
            __try
            {
                g_buildLighting(cam, 1, 1);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        MsvcString RadString(const std::string& s)    // s must outlive the call
        {
            MsvcString m = {};
            m.size = s.size();
            if (s.size() < 16)
            {
                memcpy(m.buf, s.c_str(), s.size() + 1);
                m.cap = 15;
            }
            else
            {
                m.ptr = s.c_str();
                m.cap = s.size();
            }
            return m;
        }

        // Undo_Begin keeps the label's pointer: keep every label alive.
        const char* Intern(const std::string& s)
        {
            static std::set<std::string> pool;
            return pool.insert(s).first->c_str();
        }

        // Entity Info shows a key set behind its back once told the selection changed.
        void RefreshEntityInfo()
        {
            char* bridge = Bridge();
            void* info = bridge ? *reinterpret_cast<void**>(bridge + kBridgeEntityInfo) : nullptr;
            if (g_entityInfoRefresh && info && Metacast(info, "QtEntityInfo")) g_entityInfoRefresh(info);
        }

        BOOL CALLBACK FindModalBox(HWND h, LPARAM out)
        {
            wchar_t cls[16] = {};
            HWND owner = GetWindow(h, GW_OWNER);
            if (IsWindowVisible(h) && GetClassNameW(h, cls, 16) && wcscmp(cls, L"#32770") == 0 && owner && !IsWindowEnabled(owner))
            {
                *reinterpret_cast<HWND*>(out) = h;
                return FALSE;
            }
            return TRUE;
        }
    }

    void Bind()
    {
        g_mainThread = GetCurrentThreadId();
        g_undoIsOpen = At<BoolFn>(kUndoIsOpen);
        g_undoBegin = At<LabelFn>(kUndoBegin);
        g_undoAddEntity = At<EntityFn>(kUndoAddEntity);
        g_undoEnd = At<VoidFn>(kUndoEnd);
        g_setKeyValue = At<SetKeyValueFn>(kSetKeyValue);
        g_entityInfoRefresh = At<EntityFn>(kEntityInfoRefresh);
        g_mainWin = At<void**>(kMainWin);
        g_buildLighting = At<BuildLightingFn>(kBuildLighting);
        HMODULE w = GetModuleHandleW(L"Qt5Widgets64r.dll");
        Get(qt.activeModal, w, "?activeModalWidget@QApplication@@SAPEAVQWidget@@XZ");
        Get(qt.winId, w, "?winId@QWidget@@QEBA_KXZ");
        Get(qt.isCheckable, w, "?isCheckable@QAction@@QEBA_NXZ");
        Get(qt.isChecked, w, "?isChecked@QAction@@QEBA_NXZ");
        Get(qt.isEnabled, w, "?isEnabled@QAction@@QEBA_NXZ");
        Get(qt.activate, w, "?activate@QAction@@QEAAXW4ActionEvent@1@@Z");
        if (!g_undoIsOpen || !g_undoBegin || !g_undoAddEntity || !g_undoEnd || !g_setKeyValue) rp::Log("this Radiant build is not the one the bridge knows: setkey is off");
        if (!g_mainWin || !g_buildLighting) rp::Log("this Radiant build is not the one the bridge knows: bake is off");
        if (!qt.isChecked || !qt.activate) rp::Log("Qt's QAction functions were not found: action is off");
        if (!qt.activeModal || !qt.winId) rp::Log("Qt's activeModalWidget was not found: only Win32 dialogs count as modal");
    }

    bool HookConsole(LineFn fn)
    {
        void* target = At<void*>(kSysPrintf);
        if (!target || !rp::host->Detour) return false;
        g_onLine = fn;
        if (rp::host->Detour(rp::self, target, reinterpret_cast<void*>(&HookedSysPrintf), reinterpret_cast<void**>(&g_sysPrintf))) return true;
        g_onLine = nullptr;
        return false;
    }

    void StopConsole() { g_onLine = nullptr; }

    std::string SetKey(void* entity, const std::string& key, const std::string& value)
    {
        if (!g_undoIsOpen || !g_undoBegin || !g_undoAddEntity || !g_undoEnd || !g_setKeyValue) return "this Radiant build is not the one the bridge knows";
        if (g_undoIsOpen()) return "busy";
        std::string k = key, v = value;
        MsvcString rk = RadString(k), rv = RadString(v);
        int set = CallSetKey(entity, Intern("set " + key), &rk, &rv);
        if (set < 0) return "Radiant faulted setting the key";
        if (set == 0)
        {
            rp::host->TriggerMenuAction(rp::self, "Edit/Undo");    // the step it left holds nothing
            return "Radiant refused that value for " + key + ": a typed key (origin, angles) changes through replace";
        }
        RefreshEntityInfo();
        rp::host->RequestRedraw(rp::self, MODTOOLS_VIEW_3D | MODTOOLS_VIEW_2D);
        return std::string();
    }

    std::string Bake()
    {
        char* bridge = Bridge();
        char* cam = bridge ? *reinterpret_cast<char**>(bridge + kBridgeCamWnd) : nullptr;
        if (!g_buildLighting || !cam) return "this Radiant build is not the one the bridge knows";
        if (*reinterpret_cast<int*>(cam + kCamViewMode) != 4) return "the bake runs only under real lighting: command View/Render Method/Real Lighting";
        if (!CallBuildLighting(cam)) return "Radiant faulted starting the bake";
        return std::string();
    }

    int RenderMethod()
    {
        char* bridge = Bridge();
        char* cam = bridge ? *reinterpret_cast<char**>(bridge + kBridgeCamWnd) : nullptr;
        return cam ? *reinterpret_cast<int*>(cam + kCamViewMode) : -1;
    }

    std::string Action(const std::string& name, bool trigger, ActionState& out)
    {
        const ModToolsUiApi* ui = rp::host->ui;
        if (!ui || !ui->FindEditorWidget || !qt.isCheckable || !qt.isChecked || !qt.isEnabled || !qt.activate) return "Qt's QAction functions were not found";
        void* a = ui->FindEditorWidget(name.c_str());
        if (!a || !Metacast(a, "QAction")) return "not-found";
        if (trigger)
        {
            if (!qt.isEnabled(a)) return "disabled";
            qt.activate(a, 0);
        }
        out.checkable = qt.isCheckable(a);
        out.checked = qt.isChecked(a);
        out.enabled = qt.isEnabled(a);
        return std::string();
    }

    std::string ModalTitle()
    {
        HWND h = nullptr;
        if (qt.activeModal && qt.winId)
        {
            void* w = qt.activeModal();
            if (w) h = reinterpret_cast<HWND>(qt.winId(w));
        }
        if (!h) EnumThreadWindows(g_mainThread, FindModalBox, reinterpret_cast<LPARAM>(&h));
        if (!h) return std::string();
        wchar_t t[256] = {};
        int n = GetWindowTextW(h, t, 256);
        return n > 0 ? OneLine(Utf8(std::wstring(t, size_t(n)))) : std::string("an untitled dialog");
    }

    std::string PrintView(void* hwnd, std::string& pixels, int& w, int& h)
    {
        HWND win = static_cast<HWND>(hwnd);
        RECT r{};
        if (!win || !GetClientRect(win, &r)) return "the view has no window";
        w = r.right - r.left;
        h = r.bottom - r.top;
        if (w <= 0 || h <= 0) return "the view has no size";
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;    // top row first
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        std::string why;
        if (!bmp) why = "CreateDIBSection failed";
        else
        {
            HGDIOBJ old = SelectObject(mem, bmp);
            if (!PrintWindow(win, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT)) why = "PrintWindow failed (" + std::to_string(GetLastError()) + ")";
            else
            {
                pixels.assign(static_cast<const char*>(bits), size_t(w) * size_t(h) * 4);
                for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = static_cast<char>(-1);    // alpha 255
            }
            SelectObject(mem, old);
            DeleteObject(bmp);
        }
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return why;
    }

    bool MouseHeld()
    {
        return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
    }
}
