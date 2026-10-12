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

int main()
{
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
