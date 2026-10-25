// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/qt.cpp and src/dock.cpp at 89423aa) on
// 2026-10-24. Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "qt.h"
#include "addrs.h"
#include "log.h"
#include "menukey.h"
#include <windows.h>
#include <vector>

namespace host::qt
{
    namespace
    {
        const wchar_t* kCore = L"Qt5Core64r.dll";
        const wchar_t* kWidgets = L"Qt5Widgets64r.dll";

        // MS x64 ABI: `this` in rcx; a QString or QList returned by value goes through a hidden pointer after it.
        void* (*p_QString_ctor)(void*, const char*);
        void (*p_QString_dtor)(void*);
        void (*p_QListData_dispose)(void*);
        void* (*p_QMainWindow_menuBar)(void*);
        void* (*p_QWidget_actions)(void*, void*);
        void* (*p_QAction_text)(void*, void*);
        void* (*p_QAction_menu)(void*);
        bool (*p_QAction_isEnabled)(void*);
        bool (*p_QAction_isSeparator)(void*);
        void (*p_QAction_trigger)(void*);
        bool (*p_QWidget_isVisible)(void*);
        void* (*p_qFindChild_helper)(const void*, const void*, const void*, int);
        const void* p_QObject_meta;

        bool g_bound = false, g_ok = false;

        template <typename T> void Res(T& out, const wchar_t* dll, const char* name, bool& ok)
        {
            HMODULE m = GetModuleHandleW(dll);
            out = m ? reinterpret_cast<T>(GetProcAddress(m, name)) : nullptr;
            if (!out)
            {
                HLOG("qt: missing %S!%s", dll, name);
                ok = false;
            }
        }

        // A QString is one pointer to its QArrayData: int ref, int size, uint alloc bits, qptrdiff offset to the UTF-16.
        struct QStr
        {
            void* d = nullptr;
            QStr() = default;
            explicit QStr(const char* utf8) { p_QString_ctor(this, utf8 ? utf8 : ""); }
            ~QStr()
            {
                if (d) p_QString_dtor(this);
            }
            QStr(const QStr&) = delete;
            QStr& operator=(const QStr&) = delete;
            std::string Utf8() const
            {
                if (!d) return {};
                int n = *reinterpret_cast<int*>(static_cast<char*>(d) + 4);
                ptrdiff_t off = *reinterpret_cast<ptrdiff_t*>(static_cast<char*>(d) + 16);
                auto* w = reinterpret_cast<const wchar_t*>(static_cast<char*>(d) + off);
                if (n <= 0) return {};
                int bytes = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
                std::string s(size_t(bytes), '\0');
                WideCharToMultiByte(CP_UTF8, 0, w, n, &s[0], bytes, nullptr, nullptr);
                return s;
            }
        };

        // A QList<T*> returned by value: one pointer to { ref, alloc, begin, end, array[] }. Copy out, then drop our
        // reference (the shared empty list has ref -1).
        std::vector<void*> TakePointerList(void* d)
        {
            std::vector<void*> out;
            if (!d) return out;
            char* p = static_cast<char*>(d);
            int begin = *reinterpret_cast<int*>(p + 8), end = *reinterpret_cast<int*>(p + 12);
            auto** arr = reinterpret_cast<void**>(p + 16);
            for (int i = begin; i < end && i - begin < 100000; ++i) out.push_back(arr[i]);
            auto* ref = reinterpret_cast<volatile long*>(p);
            if (*ref != -1 && InterlockedDecrement(ref) == 0)
            {
                void* list = d;
                p_QListData_dispose(&list);
            }
            return out;
        }

        // RadiantMainWin is a QMainWindow.
        void* MainWindow() { return rt::VerifyBinary() ? *rt::Global<void*>(rt::g_pMainWin) : nullptr; }

        std::vector<void*> Actions(void* widget)
        {
            void* d = nullptr;
            if (!widget) return {};
            p_QWidget_actions(widget, &d);
            return TakePointerList(d);
        }

        std::string ActionText(void* a)
        {
            QStr s;
            p_QAction_text(a, &s);
            return s.Utf8();
        }

        bool IsSeparator(void* a) { return a && p_QAction_isSeparator(a); }
    }

    bool Bind()
    {
        if (g_bound) return g_ok;
        g_bound = true;
        bool ok = true;
        Res(p_QString_ctor, kCore, "??0QString@@QEAA@PEBD@Z", ok);
        Res(p_QString_dtor, kCore, "??1QString@@QEAA@XZ", ok);
        Res(p_QListData_dispose, kCore, "?dispose@QListData@@QEAAXXZ", ok);
        Res(p_qFindChild_helper, kCore, "?qt_qFindChild_helper@@YAPEAVQObject@@PEBV1@AEBVQString@@AEBUQMetaObject@@V?$QFlags@W4FindChildOption@Qt@@@@@Z", ok);
        Res(p_QObject_meta, kCore, "?staticMetaObject@QObject@@2UQMetaObject@@B", ok);
        Res(p_QMainWindow_menuBar, kWidgets, "?menuBar@QMainWindow@@QEBAPEAVQMenuBar@@XZ", ok);
        Res(p_QWidget_actions, kWidgets, "?actions@QWidget@@QEBA?AV?$QList@PEAVQAction@@@@XZ", ok);
        Res(p_QAction_text, kWidgets, "?text@QAction@@QEBA?AVQString@@XZ", ok);
        Res(p_QAction_menu, kWidgets, "?menu@QAction@@QEBAPEAVQMenu@@XZ", ok);
        Res(p_QAction_isEnabled, kWidgets, "?isEnabled@QAction@@QEBA_NXZ", ok);
        Res(p_QAction_isSeparator, kWidgets, "?isSeparator@QAction@@QEBA_NXZ", ok);
        Res(p_QAction_trigger, kWidgets, "?trigger@QAction@@QEAAXXZ", ok);
        Res(p_QWidget_isVisible, kWidgets, "?isVisible@QWidget@@QEBA_NXZ", ok);
        g_ok = ok;
        HLOG("qt: %s", ok ? "menus and named widgets bound" : "unavailable: command, menus, save and action are off");
        return g_ok;
    }

    void* FindMenuAction(const std::string& path)
    {
        void* mw = Bind() ? MainWindow() : nullptr;
        void* holder = mw ? p_QMainWindow_menuBar(mw) : nullptr;
        void* found = nullptr;
        std::vector<std::string> segments = MenuPathSegments(path);
        for (size_t s = 0; s < segments.size();)
        {
            if (!holder) return nullptr;
            std::vector<void*> acts = Actions(holder);
            std::vector<MenuEntry> entries;
            for (void* a : acts) entries.push_back(IsSeparator(a) ? MenuEntry{ std::string(), true } : MenuEntry{ ActionText(a), false });
            size_t used = 0;
            int i = FindMenuEntryAt(entries, segments, s, used);
            if (i < 0) return nullptr;
            found = acts[size_t(i)];
            holder = p_QAction_menu(found);
            s += used;
        }
        return found;
    }

    std::string DescribeMenu(const std::string& path)
    {
        void* mw = Bind() ? MainWindow() : nullptr;
        if (!mw) return std::string();
        void* action = path.empty() ? nullptr : FindMenuAction(path);
        void* holder = path.empty() ? p_QMainWindow_menuBar(mw) : action ? p_QAction_menu(action) : nullptr;
        std::string out;
        for (void* a : Actions(holder))
        {
            if (IsSeparator(a))
            {
                out += "---\n";
                continue;
            }
            out += ActionText(a) + "\t" + (p_QAction_isEnabled(a) ? "1" : "0") + "\t" + (p_QAction_menu(a) ? "1" : "0") + "\n";
        }
        return out;
    }

    bool ActionEnabled(void* action) { return action && Bind() && p_QAction_isEnabled(action); }

    void TriggerAction(void* action)
    {
        if (action && Bind()) p_QAction_trigger(action);
    }

    void* FindEditorWidget(const char* objectName)
    {
        void* mw = objectName && *objectName && Bind() ? MainWindow() : nullptr;
        if (!mw) return nullptr;
        QStr name(objectName);
        return p_qFindChild_helper(mw, &name, p_QObject_meta, 1 /* Qt::FindChildrenRecursively */);
    }

    bool WidgetVisible(void* widget) { return !Bind() || !widget || p_QWidget_isVisible(widget); }
}
