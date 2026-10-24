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

    std::vector<std::string> MenuPathSegments(const std::string& path)
    {
        std::vector<std::string> out;
        size_t pos = 0;
        for (;;)
        {
            size_t slash = path.find('/', pos);
            out.push_back(path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos));
            if (slash == std::string::npos) return out;
            pos = slash + 1;
        }
    }

    int FindMenuEntry(const std::vector<MenuEntry>& entries, const std::string& segment)
    {
        std::string want = MenuKey(segment);
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].separator && MenuKey(entries[i].label) == want) return int(i);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].separator) continue;
            std::string k = MenuKey(entries[i].label);
            if (k.size() > want.size() && k.compare(0, want.size(), want) == 0 && k[want.size()] == ' ') return int(i);
        }
        return -1;
    }
}
