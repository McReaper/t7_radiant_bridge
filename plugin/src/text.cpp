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
}
