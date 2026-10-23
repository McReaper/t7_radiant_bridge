// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/qt.cpp at 89423aa) on 2026-10-21. Copyright (C)
// 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "menukey.h"
#include <cctype>

namespace host
{
    std::string MenuKey(const std::string& label)
    {
        std::string k;
        for (char c : label)
            if (c != '&') k += char(tolower(static_cast<unsigned char>(c)));
        while (!k.empty() && (k.back() == '.' || k.back() == ' ')) k.pop_back();
        size_t tab = k.find('\t');
        if (tab != std::string::npos) k = k.substr(0, tab);
        return k;
    }
}
