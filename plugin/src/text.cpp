#include "text.h"
#include <charconv>
#include <cmath>
#include <cstring>

namespace bridge
{
    Reply Ok(std::string summary, std::string body)
    {
        Reply r;
        r.text = std::move(summary);
        r.body = std::move(body);
        return r;
    }

    Reply Err(std::string code, std::string message)
    {
        Reply r;
        r.ok = false;
        r.code = std::move(code);
        r.text = OneLine(std::move(message));
        return r;
    }

    bool ParseRequest(const std::string& payload, Header& h, std::string& body, std::string& why)
    {
        size_t nl = payload.find('\n');
        std::string line = payload.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        body = nl == std::string::npos ? std::string() : payload.substr(nl + 1);
        size_t sp = line.find(' ');
        std::string id = line.substr(0, sp);
        if (id.empty() || id.size() > 18 || id.find_first_not_of("0123456789") != std::string::npos)
        {
            why = "a request starts with its id";
            return false;
        }
        h.id = std::stoull(id);
        if (h.id == 0)
        {
            why = "request ids start at 1 (0 is for events)";
            return false;
        }
        std::string rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        size_t sp2 = rest.find(' ');
        h.command = rest.substr(0, sp2);
        h.args = sp2 == std::string::npos ? std::string() : rest.substr(sp2 + 1);
        if (h.command.empty())
        {
            why = "no command after the id";
            return false;
        }
        return true;
    }

    std::string FormatReply(uint64_t id, const Reply& r)
    {
        std::string out = std::to_string(id);
        if (r.ok)
        {
            out += " ok";
            if (!r.text.empty()) out += " " + OneLine(r.text);
            out += "\n";
            out += r.body;
        }
        else
            out += " err " + r.code + " " + r.text + "\n";
        return out;
    }

    std::string Frame(const std::string& payload)
    {
        uint32_t n = uint32_t(payload.size());
        std::string out(4, '\0');
        for (int i = 0; i < 4; ++i) out[size_t(i)] = char((n >> (8 * i)) & 0xff);
        return out + payload;
    }

    std::vector<std::string> Words(const std::string& s)
    {
        std::vector<std::string> out;
        size_t pos = 0;
        while ((pos = s.find_first_not_of(' ', pos)) != std::string::npos)
        {
            size_t end = s.find(' ', pos);
            out.push_back(s.substr(pos, end - pos));
            pos = end;
        }
        return out;
    }

    std::string Rest(const std::string& s, size_t n)
    {
        size_t pos = s.find_first_not_of(' ');
        for (size_t i = 0; i < n && pos != std::string::npos; ++i)
        {
            pos = s.find(' ', pos);
            if (pos != std::string::npos) pos = s.find_first_not_of(' ', pos);
        }
        return pos == std::string::npos ? std::string() : s.substr(pos);
    }

    std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    std::vector<std::string> Lines(const std::string& body)
    {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < body.size())
        {
            size_t nl = body.find('\n', pos);
            std::string l = Trim(body.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (!l.empty()) out.push_back(l);
            pos = nl == std::string::npos ? body.size() : nl + 1;
        }
        return out;
    }

    std::string Join(const std::vector<std::string>& v, char sep)
    {
        std::string out;
        for (const std::string& s : v)
        {
            if (!out.empty()) out += sep;
            out += s;
        }
        return out;
    }

    bool ParseFloat(const std::string& s, float& out)
    {
        const char* first = s.data();
        const char* last = s.data() + s.size();
        if (first != last && *first == '+' && last - first > 1 && first[1] != '-') ++first;    // from_chars refuses a leading plus
        float v = 0;
        auto [end, ec] = std::from_chars(first, last, v);
        if (ec != std::errc() || end != last || first == last || !std::isfinite(v)) return false;
        out = v;
        return true;
    }

    bool ParseFloats(const std::vector<std::string>& w, size_t from, float* out, size_t n)
    {
        if (w.size() < from + n) return false;
        for (size_t i = 0; i < n; ++i)
            if (!ParseFloat(w[from + i], out[i])) return false;
        return true;
    }

    std::string Num(float v)
    {
        if (v == 0) return "0";    // and not "-0"
        char buf[64];
        auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
        return ec == std::errc() ? std::string(buf, end) : std::string("0");
    }

    std::string Num3(const float v[3]) { return Num(v[0]) + "," + Num(v[1]) + "," + Num(v[2]); }

    bool CleanValue(const std::string& s) { return s.find_first_of("\"\r\n") == std::string::npos; }

    std::string OneLine(std::string s)
    {
        for (char& c : s)
            if (c == '\r' || c == '\n') c = ' ';
        return s;
    }

    bool ParseEdit(const std::string& body, EditPlan& out, std::string& why)
    {
        out = EditPlan{};
        enum { None, Replace, Import } section = None;
        bool imported = false;
        size_t pos = 0;
        while (pos < body.size())
        {
            size_t nl = body.find('\n', pos);
            size_t end = nl == std::string::npos ? body.size() : nl + 1;
            std::string line = body.substr(pos, end - pos);
            std::string head = Trim(line);
            pos = end;
            std::vector<std::string> w = Words(head);
            if (!w.empty() && w[0] == "delete" && line[0] == 'd')
            {
                if (w.size() == 1) { why = "delete needs a GUID"; return false; }
                out.deletes.insert(out.deletes.end(), w.begin() + 1, w.end());
                section = None;
            }
            else if (!w.empty() && w[0] == "replace" && line[0] == 'r')
            {
                if (w.size() != 2) { why = "replace takes one GUID, its text on the next lines"; return false; }
                out.replaces.push_back({ w[1], std::string() });
                section = Replace;
            }
            else if (head == "import" && line[0] == 'i')
            {
                if (imported) { why = "one import section at most"; return false; }
                imported = true;
                section = Import;
            }
            else if (section == Replace) out.replaces.back().second += line;
            else if (section == Import) out.import += line;
            else if (!head.empty()) { why = "text before any delete, replace or import: " + head; return false; }
        }
        for (const auto& r : out.replaces)
            if (Trim(r.second).empty()) { why = "replace " + r.first + " has no text"; return false; }
        if (out.deletes.empty() && out.replaces.empty() && Trim(out.import).empty()) { why = "an edit with nothing to do"; return false; }
        return true;
    }

    namespace
    {
        bool Numbers(const std::vector<std::string>& w, size_t from, size_t n)
        {
            float f;
            for (size_t i = from; i < from + n; ++i)
                if (i >= w.size() || !ParseFloat(w[i], f)) return false;
            return true;
        }

        bool Integer(const std::string& s)
        {
            int v;
            auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            return r.ec == std::errc() && r.ptr == s.data() + s.size();
        }

        // "( x y z ) ( x y z ) ( x y z ) <material> <6 numbers> <lightmap> <6 numbers> [decal settings]", the three
        // points making a plane.
        bool Face(const std::vector<std::string>& w)
        {
            if (w.size() < 29) return false;
            float p[9];
            for (int i = 0; i < 3; ++i)
                if (w[i * 5] != "(" || w[i * 5 + 4] != ")" || !ParseFloats(w, i * 5 + 1, p + i * 3, 3)) return false;
            if (!Numbers(w, 16, 6) || !Numbers(w, 23, 6)) return false;
            float a[3] = { p[3] - p[0], p[4] - p[1], p[5] - p[2] }, b[3] = { p[6] - p[0], p[7] - p[1], p[8] - p[2] };
            float n[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
            return std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) > 1e-4f;
        }

        // "v x y z [c r g b a] t u v lu lv [f n]"
        bool Vertex(const std::vector<std::string>& w)
        {
            if (w.size() < 9 || w[0] != "v" || !Numbers(w, 1, 3)) return false;
            size_t i = 4;
            if (w[i] == "c")
            {
                for (size_t k = i + 1; k < i + 5; ++k)
                    if (k >= w.size() || !Integer(w[k])) return false;
                i += 5;
            }
            if (i + 5 > w.size() || w[i] != "t" || !Numbers(w, i + 1, 4)) return false;
            i += 5;
            return i == w.size() || (i + 2 == w.size() && w[i] == "f" && Integer(w[i + 1]));
        }

        bool Quoted(const std::vector<std::string>& w, const char* name)
        {
            return w.size() >= 2 && w[0] == name && w[1].size() >= 2 && w[1].front() == '"';
        }

        // An entity's own line: guid, layer, or "key" "value".
        bool EntityLine(const std::string& l, const std::vector<std::string>& w)
        {
            if (Quoted(w, "guid") || Quoted(w, "layer")) return true;
            size_t q = 0;
            for (char ch : l) q += ch == '"';
            return l.front() == '"' && l.back() == '"' && q == 4;
        }

        // A brush's own line, before its faces or its patch.
        bool BrushLine(const std::vector<std::string>& w)
        {
            return Quoted(w, "guid") || Quoted(w, "layer") || (w[0] == "contents" && w.back().back() == ';');
        }

        // One patch block, from the line after its "{" up to its "}": settings, material, lightmap, dimensions,
        // then as many rows of vertices as they say.
        struct Patch
        {
            int names = 0, rows = 0, cols = 0, row = -1, inRow = 0;
            bool dims = false;
            std::string why;

            bool Line(const std::string& l, const std::vector<std::string>& w)
            {
                if (l == "(")
                {
                    if (!dims || row >= 0) return Fail("a patch row opens before the dimensions line, or inside a row");
                    row = inRow = 0;
                    return true;
                }
                if (l == ")")
                {
                    if (row < 0 || inRow != cols) return Fail("a patch row has " + std::to_string(inRow) + " vertices, its dimensions say " + std::to_string(cols));
                    row = -1;
                    ++rows;
                    return true;
                }
                if (row >= 0) return Vertex(w) ? (++inRow, true) : Fail("not a patch vertex: " + l);
                if (dims) return true;    // trailing settings (decalLayerSort …)
                if (w.size() == 4 && Integer(w[0]) && Integer(w[1]) && Integer(w[2]) && Integer(w[3]))
                {
                    if (names != 2) return Fail("a patch needs its material and lightmap before its dimensions");
                    dims = true;
                    rows = 0;
                    cols = std::stoi(w[1]);
                    want = std::stoi(w[0]);
                    return true;
                }
                if (w.size() == 1 && l.back() != ';') ++names;
                return true;
            }
            bool Close()
            {
                if (!dims) return Fail("a patch without its dimensions line");
                if (row >= 0) return Fail("a patch row is never closed");
                if (rows != want) return Fail("a patch has " + std::to_string(rows) + " rows, its dimensions say " + std::to_string(want));
                return true;
            }
            bool Fail(std::string w)
            {
                why = std::move(w);
                return false;
            }
            int want = 0;
        };
    }

    bool CheckMapText(const std::string& text, std::string& why)
    {
        enum { Top, InEntity, InBrush, BrushPatchWord, InPatch } at = Top;
        int faces = 0, entities = 0;
        bool patch = false;
        Patch pt;
        auto fail = [&](const std::string& w) { why = w; return false; };
        for (const std::string& l : Lines(text))
        {
            std::vector<std::string> w = Words(l);
            if (l.rfind("//", 0) == 0 && at != InPatch) continue;
            switch (at)
            {
            case Top:
                if (l != "{") return fail("text outside an entity: " + l);
                at = InEntity;
                ++entities;
                break;
            case InEntity:
                if (l == "{") { at = InBrush; faces = 0; patch = false; }
                else if (l == "}") at = Top;
                else if (!EntityLine(l, w)) return fail("not an entity's line: " + l);
                break;
            case InBrush:
                if (l == "}")
                {
                    if (!patch && faces > 0 && faces < 4) return fail("a brush with " + std::to_string(faces) + " faces: it needs 4 at least");
                    at = InEntity;
                }
                else if (l == "curve" || l == "mesh")
                {
                    if (faces || patch) return fail("a brush is either faces or one patch");
                    at = BrushPatchWord;
                }
                else if (l.front() == '(')
                {
                    if (patch) return fail("a brush is either faces or one patch");
                    if (!Face(w)) return fail("not a brush face (three points making a plane, a material, a lightmap, six numbers after each): " + l);
                    ++faces;
                }
                else if (!BrushLine(w)) return fail("not a brush's line: " + l);
                break;
            case BrushPatchWord:
                if (l != "{") return fail("a patch's block does not open: " + l);
                pt = Patch{};
                at = InPatch;
                break;
            case InPatch:
                if (l == "}")
                {
                    if (!pt.Close()) return fail(pt.why);
                    patch = true;
                    at = InBrush;
                }
                else if (!pt.Line(l, w)) return fail(pt.why);
                break;
            }
        }
        if (at != Top) return fail("the text ends inside a block: a } is missing");
        if (!entities) return fail("no entity in the text");
        return true;
    }
}
