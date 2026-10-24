// Tests the host logic that needs no Radiant: the exe identity read on a
// buffer, and how a menu path picks its entries.
#include "exe.h"
#include "menukey.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) { printf("FAIL: %s\n", what); ++g_fail; }
}

// A minimal x64 PE image header with the given timestamp and SizeOfImage.
static std::vector<unsigned char> FakePe(uint32_t timestamp, uint32_t sizeOfImage, uint16_t machine = 0x8664, uint16_t magic = 0x20B)
{
    std::vector<unsigned char> b(0x400, 0);
    b[0] = 'M'; b[1] = 'Z';
    uint32_t lfanew = 0x80;
    std::memcpy(&b[0x3C], &lfanew, 4);
    b[lfanew] = 'P'; b[lfanew + 1] = 'E';   // signature "PE\0\0"
    std::memcpy(&b[lfanew + 4], &machine, 2);
    std::memcpy(&b[lfanew + 8], &timestamp, 4);
    std::memcpy(&b[lfanew + 24], &magic, 2);                // optional header magic
    std::memcpy(&b[lfanew + 24 + 56], &sizeOfImage, 4);     // SizeOfImage
    return b;
}

int main()
{
    using namespace host;

    exe::Identity id;
    auto radiant = FakePe(0x5DF45023, 0x6D95000);
    Check(exe::Read(radiant.data(), radiant.size(), id), "reads a valid x64 PE");
    Check(id.timestamp == 0x5DF45023 && id.sizeOfImage == 0x6D95000, "reads the timestamp and size");
    Check(exe::IsRadiant(id), "recognises the Radiant build");

    auto other = FakePe(0x11111111, 0x6D95000);
    Check(exe::Read(other.data(), other.size(), id) && !exe::IsRadiant(id), "a different build is not Radiant");

    auto x86 = FakePe(0x5DF45023, 0x6D95000, 0x14C);
    Check(!exe::Read(x86.data(), x86.size(), id), "an x86 image is rejected");
    auto notpe = FakePe(0x5DF45023, 0x6D95000);
    notpe[1] = 'X';
    Check(!exe::Read(notpe.data(), notpe.size(), id), "a non-MZ image is rejected");
    Check(!exe::Read(radiant.data(), 2, id), "a buffer too short to hold the headers is rejected");
    Check(!exe::Read(nullptr, 0, id), "a null image is rejected");

    Check(MenuKey("&File") == "file", "drops the accelerator '&'");
    Check(MenuKey("Save As...") == "save as", "drops trailing dots and lowercases");
    Check(MenuKey("Undo\tCtrl+Z") == "undo", "drops the accelerator after a tab");
    Check(MenuKey("CSG/Make Detail  ") == "csg/make detail", "drops trailing spaces");

    Check(MenuPathSegments("File/Save") == std::vector<std::string>{ "File", "Save" }, "splits a path at '/'");
    Check(MenuPathSegments("Edit") == std::vector<std::string>{ "Edit" }, "a single segment");
    Check(MenuPathSegments("") == std::vector<std::string>{ "" }, "an empty path is one empty segment");
    Check(MenuPathSegments("View/") == std::vector<std::string>{ "View", "" }, "a trailing '/' ends with an empty segment");

    std::vector<MenuEntry> edit = { { "&Undo Paste\tCtrl+Z" }, { "", true }, { "Undo" }, { "Cut" }, { "Select &All..." } };
    Check(FindMenuEntry(edit, "undo") == 2, "an exact key wins over an earlier word prefix");
    Check(FindMenuEntry(edit, "Select All") == 4, "matches without '&', dots or case");
    Check(FindMenuEntry(edit, "Copy") == -1, "an absent entry is -1");
    std::vector<MenuEntry> undoOnly = { { "Undo Paste" }, { "Undone" } };
    Check(FindMenuEntry(undoOnly, "Undo") == 0, "a word prefix matches when no key is equal");
    Check(FindMenuEntry({ { "Undone" } }, "Undo") == -1, "a prefix inside a word does not match");
    Check(FindMenuEntry({ { "", true } }, "") == -1, "a separator never matches");

    if (g_fail == 0) printf("host_test: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
