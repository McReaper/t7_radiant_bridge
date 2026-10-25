// Tests the host logic that needs no Radiant: the exe identity read on a
// buffer, how a menu path picks its entries, and how a view projects a point.
#include "exe.h"
#include "menukey.h"
#include "project.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) { printf("FAIL: %s\n", what); ++g_fail; }
}

static bool Near(float a, float b) { return fabsf(a - b) < 0.05f; }

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
    Check(FindMenuEntry({ { "Undo Paste", true } }, "Undo") == -1, "a separator with a label does not match a prefix either");

    // Radiant's Selection/Select By menu has labels that hold a '/'
    std::vector<MenuEntry> selectBy = { { "Select by Samplesize" }, { "Select by Key/Value" }, { "Select by Key/Value All(*)" }, { "Select by FX Properties" } };
    std::vector<std::string> keyValue = MenuPathSegments("Selection/Select By/Select by Key/Value");
    size_t used = 99;
    Check(FindMenuEntryAt(selectBy, keyValue, 2, used) == 1 && used == 2, "a label holding '/' is named by its two segments");
    Check(FindMenuEntryAt(selectBy, MenuPathSegments("Select by Key/Value All(*)"), 0, used) == 2 && used == 2, "and its longer neighbour by theirs");
    Check(FindMenuEntryAt(selectBy, MenuPathSegments("Select by FX Properties/More"), 0, used) == 3 && used == 1, "a whole label takes one segment, the rest left for the next level");
    Check(FindMenuEntryAt(selectBy, MenuPathSegments("Select by Key/Nope"), 0, used) == -1 && used == 0, "no entry: -1, nothing used");
    Check(FindMenuEntryAt(selectBy, keyValue, 4, used) == -1 && used == 0, "past the last segment: -1");
    std::vector<MenuEntry> undoRedo = { { "Undo Paste" }, { "Undo/Redo" } };
    Check(FindMenuEntryAt(undoRedo, MenuPathSegments("Undo/Redo"), 0, used) == 1 && used == 2, "an exact key over two segments wins over a word prefix of one");
    Check(FindMenuEntryAt(undoRedo, MenuPathSegments("Undo/Nope"), 0, used) == 0 && used == 1, "a word prefix of one segment when nothing is exact");
    std::vector<MenuEntry> sameStart = { { "Show" }, { "Show/Hide" } };
    Check(FindMenuEntryAt(sameStart, MenuPathSegments("Show/Hide"), 0, used) == 0 && used == 1, "a whole label of one segment wins over a longer one");

    // the editor camera at the origin looking down +x, a 1600x900 frame, FOV 100 (a 16:9 view's horizontal FOV)
    CamParams cam{ { 0, 0, 0 }, { 0, 0, 0 }, 1600, 900, false, false, 100.0f };
    float sx = 0, sy = 0;
    float ahead[3] = { 100, 0, 0 };
    Check(ProjectCam(cam, ahead, sx, sy) && Near(sx, 800) && Near(sy, 450), "a point straight ahead is the frame's centre");
    // at the 16:9 frame's horizontal FOV edge: tan(50 deg) * 100 units to the side lands on the right border
    float edge[3] = { 100, -100 * tanf(50.0f * 3.14159265f / 180.0f), 0 };
    Check(ProjectCam(cam, edge, sx, sy) && Near(sx, 1600) && Near(sy, 450), "the FOV's edge to the right (-y) is the right border");
    float leftUp[3] = { 100, 10, 10 };
    Check(ProjectCam(cam, leftUp, sx, sy) && sx < 800 && sy < 450, "+y is left and +z is up");
    float behind[3] = { -100, 0, 0 };
    Check(!ProjectCam(cam, behind, sx, sy), "a point behind the camera does not project");
    CamParams yawed = cam;
    yawed.angles[1] = 90;
    float north[3] = { 0, 100, 0 };
    Check(ProjectCam(yawed, north, sx, sy) && Near(sx, 800) && Near(sy, 450), "yaw 90 looks down +y");
    CamParams pitched = cam;
    pitched.angles[0] = 45;
    float upAhead[3] = { 100, 0, 100 };
    Check(ProjectCam(pitched, upAhead, sx, sy) && Near(sx, 800) && Near(sy, 450), "positive pitch looks up");
    // the game view letterboxes a 16:9 lens: in a 1600x1000 frame it is 1600x900, so the FOV edge is still the border
    CamParams game{ { 0, 0, 0 }, { 0, 0, 0 }, 1600, 1000, true, false, 100.0f };
    Check(ProjectCam(game, edge, sx, sy) && Near(sx, 1600), "the game view's 16:9 lens spans the frame's width");
    // in a 2000x900 frame the lens is 1600x900, centred: the FOV edge is 800 pixels right of the centre
    CamParams wide{ { 0, 0, 0 }, { 0, 0, 0 }, 2000, 900, true, false, 100.0f };
    Check(ProjectCam(wide, edge, sx, sy) && Near(sx, 1800), "a frame wider than 16:9 pillarboxes the game view's lens");
    float close[3] = { 0.5f, 0, 0 };
    Check(!ProjectCam(cam, close, sx, sy), "a point less than a unit in front does not project");
    CamParams probe{ { 0, 0, 0 }, { 0, 0, 0 }, 1600, 900, true, true, 0 };
    float diag[3] = { 100, -100, 0 };
    Check(ProjectCam(probe, diag, sx, sy) && Near(sx, 800 + 450), "the probe view's 90 degree lens fits the shorter side");

    XYParams top{ 2, { 100, 200, 0 }, 0.5f };
    float pt[3] = { 300, 400, 999 };
    Check(ProjectXY(top, 800, 600, pt, sx, sy) && Near(sx, 500) && Near(sy, 200), "XY: +x right, +y up, z ignored");
    XYParams front{ 1, { 0, 0, 0 }, 1.0f };
    float pf[3] = { 10, 999, 20 };
    Check(ProjectXY(front, 800, 600, pf, sx, sy) && Near(sx, 410) && Near(sy, 280), "XZ: +x right, +z up");
    XYParams side{ 0, { 0, 0, 0 }, 1.0f };
    float ps[3] = { 999, 10, 20 };
    Check(ProjectXY(side, 800, 600, ps, sx, sy) && Near(sx, 410) && Near(sy, 280), "YZ: +y right, +z up");

    if (g_fail == 0) printf("host_test: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
