// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/qt.h at 89423aa) on 2026-10-24. Copyright (C)
// 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// The editor's menus and named widgets, through Radiant's Qt 5.3 DLLs: functions resolved by mangled export name, no Qt
// headers. Trimmed to what the bridge's command, menus, save and action reach, and the capture's visible-pane check.
// Main thread only.
#pragma once
#include <string>

namespace host::qt
{
    // Resolves the exports once (logs any missing). False: the menus and named widgets are unavailable.
    bool Bind();

    // The QAction a menu path names from the menu bar down ("Edit/Undo": '&', a trailing "..." and case ignored;
    // MenuKey and FindMenuEntry say how), or nullptr.
    void* FindMenuAction(const std::string& path);
    // One "label<TAB>enabled<TAB>submenu" line per entry of the menu at `path` ("" = the menu bar), "---" per
    // separator; "" when there is no such menu.
    std::string DescribeMenu(const std::string& path);
    bool ActionEnabled(void* action);
    void TriggerAction(void* action);   // QAction::trigger: what the user's click or shortcut does

    // A QObject anywhere under the main window by its object name, or nullptr.
    void* FindEditorWidget(const char* objectName);
    // QWidget::isVisible; true when Qt is unavailable (nothing is skipped then).
    bool WidgetVisible(void* widget);
}
