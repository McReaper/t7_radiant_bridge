// The protocol's text helpers (src/text.cpp), checked without Radiant. Run by ctest; exits 1 on any failure.
#include "text.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace bridge;

static int g_failed = 0;

#define CHECK(cond)                                                    \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
        {                                                              \
            std::printf("%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                \
        }                                                              \
    } while (0)

static void TestParseRequest()
{
    Header h;
    std::string body, why;
    CHECK(ParseRequest("12 import t7kb: room A\n{\n}\n", h, body, why));
    CHECK(h.id == 12 && h.command == "import" && h.args == "t7kb: room A" && body == "{\n}\n");

    CHECK(ParseRequest("3 state\r\n", h, body, why));
    CHECK(h.id == 3 && h.command == "state" && h.args.empty() && body.empty());

    CHECK(ParseRequest("4 selection", h, body, why));    // no line end, no body
    CHECK(h.command == "selection" && body.empty());

    CHECK(ParseRequest("5 setkey {G} script_noteworthy ", h, body, why));
    CHECK(h.args == "{G} script_noteworthy ");

    h = Header{};
    CHECK(!ParseRequest("state\n", h, body, why));
    CHECK(h.id == 0 && !why.empty());
    CHECK(!ParseRequest("0 state\n", h, body, why));
    CHECK(!ParseRequest("-1 state\n", h, body, why));
    CHECK(!ParseRequest("1x state\n", h, body, why));
    CHECK(!ParseRequest("1234567890123456789 state\n", h, body, why));

    h = Header{};
    CHECK(!ParseRequest("7\n", h, body, why));
    CHECK(h.id == 7);    // kept, so the error answers request 7
    CHECK(!ParseRequest("8  state\n", h, body, why));
    CHECK(h.id == 8);
    CHECK(!ParseRequest("", h, body, why));
}

static void TestFormatReply()
{
    CHECK(FormatReply(1, Ok()) == "1 ok\n");
    CHECK(FormatReply(2, Ok("deleted=3")) == "2 ok deleted=3\n");
    CHECK(FormatReply(3, Ok("w=2 h=1", "body\n")) == "3 ok w=2 h=1\nbody\n");
    CHECK(FormatReply(4, Ok("a\nb")) == "4 ok a b\n");
    CHECK(FormatReply(5, Err("busy", "Choose path...")) == "5 err busy Choose path...\n");
    CHECK(FormatReply(6, Err("refused", "two\r\nlines")) == "6 err refused two  lines\n");
    CHECK(FormatReply(0, Err("bad-request", "x")) == "0 err bad-request x\n");
}

static void TestFrame()
{
    std::string f = Frame("abc");
    CHECK(f.size() == 7 && f[0] == 3 && f[1] == 0 && f[2] == 0 && f[3] == 0 && f.substr(4) == "abc");
    std::string big(0x01020304 & 0xffff, 'x');
    std::string g = Frame(big);
    CHECK(static_cast<unsigned char>(g[0]) == 0x04 && static_cast<unsigned char>(g[1]) == 0x03 && g[2] == 0 && g[3] == 0);
    CHECK(Frame("") == std::string(4, '\0'));
}

static void TestWordsAndRest()
{
    CHECK(Words("") == std::vector<std::string>{});
    CHECK(Words("  ") == std::vector<std::string>{});
    CHECK((Words("a  b c ") == std::vector<std::string>{ "a", "b", "c" }));
    CHECK(Rest("{G} key value with  spaces", 2) == "value with  spaces");
    CHECK(Rest("{G} key", 2).empty());
    CHECK(Rest("{G} key ", 2).empty());
    CHECK(Rest("{G}  key  v", 2) == "v");
    CHECK(Rest(" a b", 0) == "a b");
    CHECK(Rest("a", 5).empty());
    CHECK(Trim(" \t a b \r\n") == "a b");
    CHECK(Trim(" \r\n").empty());
}

static void TestLines()
{
    CHECK((Lines("{A}\r\n\n  {B}  \n{C}") == std::vector<std::string>{ "{A}", "{B}", "{C}" }));
    CHECK(Lines("").empty());
    CHECK(Lines("\n \r\n").empty());
    CHECK(Join({ "a", "b" }, ',') == "a,b");
    CHECK(Join({}, ',').empty());
}

static void TestNumbers()
{
    float v = 0;
    CHECK(ParseFloat("1.5", v) && v == 1.5f);
    CHECK(ParseFloat("-128", v) && v == -128.0f);
    CHECK(ParseFloat("+2", v) && v == 2.0f);
    CHECK(ParseFloat("1e3", v) && v == 1000.0f);
    CHECK(!ParseFloat("", v));
    CHECK(!ParseFloat("+", v));
    CHECK(!ParseFloat("+-1", v));
    CHECK(!ParseFloat("x", v));
    CHECK(!ParseFloat("1.5x", v));
    CHECK(!ParseFloat("inf", v));
    CHECK(!ParseFloat("nan", v));
    float six[6];
    CHECK(ParseFloats({ "pose", "0", "1", "2", "3", "4", "5" }, 1, six, 6) && six[5] == 5.0f);
    CHECK(!ParseFloats({ "0", "1" }, 0, six, 3));
    CHECK(!ParseFloats({ "0", "y", "2" }, 0, six, 3));

    CHECK(Num(0.1f) == "0.1");
    CHECK(Num(-0.0f) == "0");
    CHECK(Num(1234.5f) == "1234.5");
    CHECK(Num(100000.0f) == "100000");
    CHECK(Num(-131072.0f) == "-131072");
    float back = 0;
    CHECK(ParseFloat(Num(0.3333333f), back) && back == 0.3333333f);
    float p[3] = { 1, -2.5f, 0 };
    CHECK(Num3(p) == "1,-2.5,0");
}

static void TestValues()
{
    CHECK(CleanValue("") && CleanValue("a b") && CleanValue("models/x"));
    CHECK(!CleanValue("a\"b") && !CleanValue("a\nb") && !CleanValue("a\r"));
    CHECK(OneLine("a\r\nb\nc") == "a  b c");
}

static void TestParseEdit()
{
    EditPlan p;
    std::string why;
    CHECK(ParseEdit("delete {A} {B}\ndelete {C}\nreplace {E}\n{\nguid \"{E}\"\n\"classname\" \"light\"\n}\n"
                    "import\n{\n\"classname\" \"worldspawn\"\n {\n  ( 0 0 0 ) x\n }\n}\n",
                    p, why));
    CHECK(p.deletes.size() == 3 && p.deletes[0] == "{A}" && p.deletes[2] == "{C}");
    CHECK(p.replaces.size() == 1 && p.replaces[0].first == "{E}" && p.replaces[0].second == "{\nguid \"{E}\"\n\"classname\" \"light\"\n}\n");
    CHECK(p.import == "{\n\"classname\" \"worldspawn\"\n {\n  ( 0 0 0 ) x\n }\n}\n");

    // CRLF lines are kept as they are, an indented "import" is text, sections may come in any order
    CHECK(ParseEdit("import\r\n{\r\n import\r\n}\r\nreplace {E}\r\n{\r\n}\r\n", p, why));
    CHECK(p.import == "{\r\n import\r\n}\r\n" && p.replaces[0].second == "{\r\n}\r\n" && p.deletes.empty());

    CHECK(ParseEdit("delete {A}", p, why) && p.deletes.size() == 1 && p.import.empty());

    CHECK(!ParseEdit("", p, why) && why == "an edit with nothing to do");
    CHECK(!ParseEdit("import\n\n", p, why) && why == "an edit with nothing to do");
    CHECK(!ParseEdit("{\n}\n", p, why) && why.find("text before any") == 0);
    CHECK(!ParseEdit("delete\n", p, why) && why == "delete needs a GUID");
    CHECK(!ParseEdit("replace {A} {B}\n{\n}\n", p, why) && why.find("replace takes one GUID") == 0);
    CHECK(!ParseEdit("replace {A}\n\ndelete {B}\n", p, why) && why == "replace {A} has no text");
    CHECK(!ParseEdit("import\n{\n}\nimport\n{\n}\n", p, why) && why == "one import section at most");
}

static const char* kBox =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "// brush 0\n"
    "{\n"
    " guid \"{5EC8DE49-C58E-11F1-B542-00155DD185EA}\"\n"
    " contents detail;\n"
    " ( 248 256 -64 ) ( -216 256 -64 ) ( -216 -232 -64 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -216 -232 -16 ) ( -216 256 -16 ) ( 248 256 -16 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -216 -232 -16 ) ( 248 -232 -16 ) ( 248 -232 -24 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 248 -232 -16 ) ( 248 256 -16 ) ( 248 256 -24 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0 decalLayerSort \"x\"\n"
    "}\n"
    "}\n";

static const char* kCurve =
    "{\n"
    "guid \"{A}\"\n"
    "\"classname\" \"script_brushmodel\"\n"
    " {\n"
    "  guid \"{AF3C9C64-C5B3-11F1-B542-00155DD185EA}\"\n"
    "  curve\n"
    "  {\n"
    "  toolFlags;\n"
    "   aac_wood_mahogany_01\n"
    "   lightmap_gray\n"
    "   2 2 16 1\n"
    "   (\n"
    "\tv -96 -64 -16 t 0 0 0 0\n"
    "\tv 0 -64 200 c 255 0 0 255 t 128 0 8 0 f 2\n"
    "   )\n"
    "   (\n"
    "\tv -96 0 -16 t 0 64 0 4\n"
    "\tv 0 0 200 t 128 64 8 4\n"
    "   )\n"
    "  }\n"
    " }\n"
    "}\n";

static void TestCheckMapText()
{
    std::string why;
    CHECK(CheckMapText(kBox, why));
    CHECK(CheckMapText(kCurve, why));
    CHECK(CheckMapText(std::string(kBox) + "{\nguid \"{E}\"\nlayer \"000_Global/Lights\"\n\"classname\" \"light\"\n\"origin\" \"0 0 64\"\n}\n", why));

    // the text that stopped Radiant: a line in a brush that is no face
    CHECK(!CheckMapText("{\n\"classname\" \"worldspawn\"\n{\n this is not a face\n}\n}\n", why) && why == "not a brush's line: this is not a face");
    std::string box = kBox;
    auto without = [&](const std::string& what, const std::string& with) {
        std::string t = box;
        size_t at = t.find(what);
        CHECK(at != std::string::npos);    // a case that changes nothing would pass for nothing
        return at == std::string::npos ? std::string() : t.replace(at, what.size(), with);
    };
    CHECK(!CheckMapText(without("( 248 256 -64 ) ( -216 256 -64 ) ( -216 -232 -64 )", "( 0 0 0 ) ( 1 1 1 ) ( 2 2 2 )"), why) && why.find("not a brush face") == 0);
    CHECK(!CheckMapText(without("-64 ) caulk 64 64 0 0 0 0 lightmap_gray", "-64 ) caulk 64 64 0 0 0 lightmap_gray"), why) && why.find("not a brush face") == 0);
    CHECK(!CheckMapText(without(" ( 248 -232 -16 ) ( 248 256 -16 ) ( 248 256 -24 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0 decalLayerSort \"x\"\n", ""), why) && why == "a brush with 3 faces: it needs 4 at least");
    CHECK(!CheckMapText(without("\"classname\" \"worldspawn\"", "\"classname\" worldspawn"), why) && why.find("not an entity's line") == 0);
    CHECK(!CheckMapText(without("}\n}\n", "}\n"), why) && why == "the text ends inside a block: a } is missing");
    CHECK(!CheckMapText("\"classname\" \"light\"\n", why) && why.find("text outside an entity") == 0);
    CHECK(!CheckMapText("// nothing\n", why) && why == "no entity in the text");

    std::string curve = kCurve;
    auto curveWith = [&](const std::string& what, const std::string& with) {
        std::string t = curve;
        size_t at = t.find(what);
        CHECK(at != std::string::npos);
        return at == std::string::npos ? std::string() : t.replace(at, what.size(), with);
    };
    CHECK(!CheckMapText(curveWith("\tv 0 0 200 t 128 64 8 4\n", ""), why) && why == "a patch row has 1 vertices, its dimensions say 2");
    CHECK(!CheckMapText(curveWith("2 2 16 1", "3 2 16 1"), why) && why == "a patch has 2 rows, its dimensions say 3");
    CHECK(!CheckMapText(curveWith("t 128 64 8 4", "t 128 64 8"), why) && why.find("not a patch vertex") == 0);
    CHECK(!CheckMapText(curveWith("c 255 0 0 255", "c 255 0 0"), why));
    CHECK(!CheckMapText(curveWith("f 2", "f"), why));
    CHECK(!CheckMapText(curveWith("   lightmap_gray\n", ""), why) && why == "a patch needs its material and lightmap before its dimensions");
    CHECK(!CheckMapText(curveWith("  curve\n  {\n", "  curve\n  (\n"), why) && why.find("a patch's block does not open") == 0);
}

int main()
{
    TestCheckMapText();
    TestParseEdit();
    TestParseRequest();
    TestFormatReply();
    TestFrame();
    TestWordsAndRest();
    TestLines();
    TestNumbers();
    TestValues();
    if (g_failed) std::printf("%d check(s) failed\n", g_failed);
    else std::printf("all checks passed\n");
    return g_failed ? 1 : 0;
}
