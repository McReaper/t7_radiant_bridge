// The protocol's text: request headers, replies, frames, arguments and numbers. No Windows or Radiant calls, so
// test/text_test.cpp checks all of it without Radiant.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bridge
{
    // A request's header line, "<id> <command> [args…]".
    struct Header
    {
        uint64_t id = 0;
        std::string command;
        std::string args;   // the rest of the line after the command, as sent
    };

    // A reply before it is framed: ok with a summary and a body, or err with a code and a message.
    struct Reply
    {
        bool ok = true;
        std::string code;   // err only
        std::string text;   // the summary, or the error message
        std::string body;
    };

    Reply Ok(std::string summary = std::string(), std::string body = std::string());
    Reply Err(std::string code, std::string message);

    // Splits a payload into its header and body. False with the reason when the header is malformed; h.id is kept when
    // it could be read, so the error can answer that request.
    bool ParseRequest(const std::string& payload, Header& h, std::string& body, std::string& why);
    // "<id> ok[ <summary>]\n<body>" or "<id> err <code> <message>\n"
    std::string FormatReply(uint64_t id, const Reply& r);
    // A 4-byte little-endian length, then the payload.
    std::string Frame(const std::string& payload);

    std::vector<std::string> Words(const std::string& s);    // split on runs of spaces
    std::string Rest(const std::string& s, size_t n);         // what follows the first n words, "" when nothing does
    std::string Trim(const std::string& s);
    std::vector<std::string> Lines(const std::string& body); // trimmed, without empty lines
    std::string Join(const std::vector<std::string>& v, char sep);
    bool ParseFloat(const std::string& s, float& out);       // the whole word, finite, whatever the locale
    bool ParseFloats(const std::vector<std::string>& w, size_t from, float* out, size_t n);
    std::string Num(float v);                                 // the shortest decimal that reads back as v
    std::string Num3(const float v[3]);                       // "x,y,z"
    bool CleanValue(const std::string& s);                    // no double quote or line break, Radiant's rule for keys and values
    std::string OneLine(std::string s);                       // line breaks to spaces, for a header line

    // An edit's body: what to delete, the point entities to replace in place (keeping their GUIDs), and the text to
    // import. Sections start at a line "delete <guid>…", "replace <guid>" or "import"; the lines after a replace or an
    // import, up to the next section, are its text, kept verbatim.
    struct EditPlan
    {
        std::vector<std::string> deletes;
        std::vector<std::pair<std::string, std::string>> replaces;   // GUID, the entity's new text
        std::string import;
    };
    bool ParseEdit(const std::string& body, EditPlan& out, std::string& why);

    // Whether entity text is what Radiant's import reads, checked before it gets any: Radiant takes a line it cannot
    // parse for a fatal error (an "EntityDef::Parse" dialog, then a failed assertion), not for a refusal. Entities hold
    // guid, layer and "key" "value" lines and brushes; a brush its guid, layer and contents lines and either four faces
    // or more, each through three points that make a plane, or one curve or mesh patch of v vertex rows.
    bool CheckMapText(const std::string& text, std::string& why);
}
