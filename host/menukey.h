// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/qt.cpp at 89423aa) on 2026-10-21. Copyright (C)
// 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// How a menu path ("File/Save") meets Radiant's menu labels. No Qt, so host/test/host_test.cpp checks it.
#pragma once
#include <string>

namespace host
{
    // A label as menu paths compare it: no '&', lower case, no trailing dots or spaces, nothing from a tab on ("Undo\tCtrl+Z").
    // The dots go before the tab does, as in MTPlugins, so both hosts reach the same entries.
    std::string MenuKey(const std::string& label);
}
