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

    static int ExactEntry(const std::vector<MenuEntry>& entries, const std::string& want)
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].separator && MenuKey(entries[i].label) == want) return int(i);
        return -1;
    }

    static int WordPrefixEntry(const std::vector<MenuEntry>& entries, const std::string& want)
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].separator) continue;
            std::string k = MenuKey(entries[i].label);
            if (k.size() > want.size() && k.compare(0, want.size(), want) == 0 && k[want.size()] == ' ') return int(i);
        }
        return -1;
    }

    int FindMenuEntry(const std::vector<MenuEntry>& entries, const std::string& segment)
    {
        size_t used = 0;
        return FindMenuEntryAt(entries, { segment }, 0, used);
    }

    int FindMenuEntryAt(const std::vector<MenuEntry>& entries, const std::vector<std::string>& segments, size_t first, size_t& used)
    {
        std::vector<std::string> wants;   // segments[first], then joined to each next one
        std::string joined;
        for (size_t s = first; s < segments.size(); ++s)
        {
            joined += (s > first ? "/" : "") + segments[s];
            wants.push_back(MenuKey(joined));
        }
        for (int (*match)(const std::vector<MenuEntry>&, const std::string&) : { ExactEntry, WordPrefixEntry })
            for (size_t n = 0; n < wants.size(); ++n)
            {
                int i = match(entries, wants[n]);
                if (i >= 0)
                {
                    used = n + 1;
                    return i;
                }
            }
        used = 0;
        return -1;
    }
}
