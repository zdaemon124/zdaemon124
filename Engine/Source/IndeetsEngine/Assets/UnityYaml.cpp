#include "IndeetsEngine/Assets/UnityYaml.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ie::UnityYaml {
namespace {

using json = nlohmann::json;

struct Line {
    int indent = 0;
    std::string text; // without indentation and trailing '\r'
};

bool IsBlank(const Line& l) { return l.text.empty() || l.text[0] == '#'; }

void AppendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

uint32_t Hex(const std::string& s, size_t pos, size_t count)
{
    uint32_t v = 0;
    for (size_t i = 0; i < count && pos + i < s.size(); ++i) {
        char c = s[pos + i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
    }
    return v;
}

// Joins the lines of a multi-line quoted scalar (YAML folding: a line break is a space,
// an empty line is a newline).
std::string Fold(const std::vector<std::string>& parts)
{
    std::string out;
    bool pendingSpace = false;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string p = parts[i];
        if (i > 0) {
            size_t start = p.find_first_not_of(" \t");
            p = start == std::string::npos ? "" : p.substr(start);
        }
        if (i + 1 < parts.size()) {
            size_t end = p.find_last_not_of(" \t");
            p = end == std::string::npos ? "" : p.substr(0, end + 1);
        }
        if (i > 0 && p.empty() && i + 1 < parts.size()) {
            out += '\n';
            pendingSpace = false;
            continue;
        }
        if (pendingSpace)
            out += ' ';
        out += p;
        pendingSpace = true;
    }
    return out;
}

std::string Unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '\\' || i + 1 >= s.size()) {
            out += c;
            continue;
        }
        char e = s[++i];
        switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '0': out += '\0'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case ' ': out += ' '; break;
        case 'x': AppendUtf8(out, Hex(s, i + 1, 2)); i += 2; break;
        case 'u': {
            uint32_t cp = Hex(s, i + 1, 4);
            i += 4;
            // Surrogate pair (😀).
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < s.size() + 1 && s.compare(i + 1, 2, "\\u") == 0) {
                uint32_t low = Hex(s, i + 3, 4);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    i += 6;
                }
            }
            AppendUtf8(out, cp);
            break;
        }
        case 'U': AppendUtf8(out, Hex(s, i + 1, 8)); i += 8; break;
        default: out += e; break;
        }
    }
    return out;
}

// ---- flow collections: {a: 1, b: {c: 2}} and [1, 2]
class FlowParser {
public:
    explicit FlowParser(const std::string& s) : m_S(s) {}

    json Value()
    {
        Skip();
        if (m_P >= m_S.size())
            return "";
        char c = m_S[m_P];
        if (c == '{')
            return Map();
        if (c == '[')
            return Seq();
        if (c == '"' || c == '\'')
            return Quoted();
        size_t start = m_P;
        while (m_P < m_S.size() && m_S[m_P] != ',' && m_S[m_P] != '}' && m_S[m_P] != ']')
            ++m_P;
        return Trim(m_S.substr(start, m_P - start));
    }

private:
    static std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos)
            return "";
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }

    void Skip()
    {
        while (m_P < m_S.size() && (m_S[m_P] == ' ' || m_S[m_P] == '\t'))
            ++m_P;
    }

    json Quoted()
    {
        char q = m_S[m_P++];
        std::string raw;
        while (m_P < m_S.size()) {
            char c = m_S[m_P++];
            if (q == '\'' && c == '\'') {
                if (m_P < m_S.size() && m_S[m_P] == '\'') {
                    raw += '\'';
                    ++m_P;
                    continue;
                }
                break;
            }
            if (q == '"' && c == '\\' && m_P < m_S.size()) {
                raw += c;
                raw += m_S[m_P++];
                continue;
            }
            if (q == '"' && c == '"')
                break;
            raw += c;
        }
        return q == '"' ? Unescape(raw) : raw;
    }

    json Map()
    {
        json obj = json::object();
        ++m_P; // {
        for (;;) {
            Skip();
            if (m_P >= m_S.size())
                break;
            if (m_S[m_P] == '}') {
                ++m_P;
                break;
            }
            if (m_S[m_P] == ',') {
                ++m_P;
                continue;
            }
            size_t start = m_P;
            while (m_P < m_S.size() && m_S[m_P] != ':' && m_S[m_P] != '}' && m_S[m_P] != ',')
                ++m_P;
            std::string key = Trim(m_S.substr(start, m_P - start));
            if (m_P < m_S.size() && m_S[m_P] == ':') {
                ++m_P;
                obj[key] = Value();
            } else {
                obj[key] = "";
            }
        }
        return obj;
    }

    json Seq()
    {
        json arr = json::array();
        ++m_P; // [
        for (;;) {
            Skip();
            if (m_P >= m_S.size())
                break;
            if (m_S[m_P] == ']') {
                ++m_P;
                break;
            }
            if (m_S[m_P] == ',') {
                ++m_P;
                continue;
            }
            arr.push_back(Value());
        }
        return arr;
    }

    const std::string& m_S;
    size_t m_P = 0;
};

// ---- block structure
class BlockParser {
public:
    explicit BlockParser(std::vector<Line>& lines, size_t begin, size_t end) : m_Lines(lines), m_I(begin), m_End(end) {}

    json Parse()
    {
        SkipBlank();
        if (m_I >= m_End)
            return json::object();
        return Block(m_Lines[m_I].indent);
    }

private:
    void SkipBlank()
    {
        while (m_I < m_End && IsBlank(m_Lines[m_I]))
            ++m_I;
    }

    static bool IsSeqItem(const Line& l) { return l.text == "-" || l.text.rfind("- ", 0) == 0; }

    json Block(int indent)
    {
        SkipBlank();
        if (m_I < m_End && IsSeqItem(m_Lines[m_I]) && m_Lines[m_I].indent == indent)
            return Sequence(indent);
        return Mapping(indent);
    }

    // Finds "key: value" / "key:" and returns the split position, or npos.
    static size_t KeySplit(const std::string& t)
    {
        if (t.empty() || t[0] == '"' || t[0] == '\'' || t[0] == '{' || t[0] == '[')
            return std::string::npos;
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i] == ':' && (i + 1 == t.size() || t[i + 1] == ' '))
                return i;
        return std::string::npos;
    }

    json Mapping(int indent)
    {
        json obj = json::object();
        while (true) {
            SkipBlank();
            if (m_I >= m_End)
                break;
            Line& line = m_Lines[m_I];
            if (line.indent != indent || IsSeqItem(line))
                break;
            size_t split = KeySplit(line.text);
            if (split == std::string::npos) {
                ++m_I; // not a mapping line; skip
                continue;
            }
            std::string key = line.text.substr(0, split);
            std::string rest = split + 1 < line.text.size() ? line.text.substr(split + 2) : "";
            ++m_I;
            obj[key] = Value(rest, indent);
        }
        return obj;
    }

    json Sequence(int indent)
    {
        json arr = json::array();
        while (true) {
            SkipBlank();
            if (m_I >= m_End)
                break;
            Line& line = m_Lines[m_I];
            if (line.indent != indent || !IsSeqItem(line))
                break;
            std::string rest = line.text.size() > 2 ? line.text.substr(2) : "";
            if (rest.empty()) {
                ++m_I;
                SkipBlank();
                if (m_I < m_End && m_Lines[m_I].indent > indent)
                    arr.push_back(Block(m_Lines[m_I].indent));
                else
                    arr.push_back("");
                continue;
            }
            if (KeySplit(rest) != std::string::npos) {
                // "- key: value" starts a mapping whose keys sit two columns further in.
                line.indent = indent + 2;
                line.text = rest;
                arr.push_back(Mapping(indent + 2));
                continue;
            }
            ++m_I;
            arr.push_back(Value(rest, indent));
        }
        return arr;
    }

    // Value after "key:" (or a sequence item). `ownerIndent` is the indentation of that key.
    json Value(std::string rest, int ownerIndent)
    {
        while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t'))
            rest.pop_back();
        if (rest.empty()) {
            SkipBlank();
            if (m_I < m_End) {
                const Line& next = m_Lines[m_I];
                if (next.indent > ownerIndent || (next.indent == ownerIndent && IsSeqItem(next)))
                    return Block(next.indent);
            }
            return "";
        }
        if (rest[0] == '{' || rest[0] == '[') {
            // Flow collections may wrap onto following lines.
            int depth = 0;
            auto balance = [&](const std::string& s) {
                bool inQuote = false;
                char q = 0;
                for (char c : s) {
                    if (inQuote) {
                        if (c == q) inQuote = false;
                        continue;
                    }
                    if (c == '"' || c == '\'') { inQuote = true; q = c; }
                    else if (c == '{' || c == '[') ++depth;
                    else if (c == '}' || c == ']') --depth;
                }
            };
            balance(rest);
            while (depth > 0 && m_I < m_End) {
                rest += ' ' + m_Lines[m_I].text;
                balance(m_Lines[m_I].text);
                ++m_I;
            }
            return FlowParser(rest).Value();
        }
        if (rest[0] == '"' || rest[0] == '\'') {
            char q = rest[0];
            std::vector<std::string> parts{rest.substr(1)};
            auto closed = [&](const std::string& s) {
                for (size_t i = 0; i < s.size(); ++i) {
                    if (q == '"' && s[i] == '\\') { ++i; continue; }
                    if (s[i] == q) {
                        if (q == '\'' && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
                        return true;
                    }
                }
                return false;
            };
            while (!closed(parts.back()) && m_I < m_End) {
                std::string l = std::string(size_t(m_Lines[m_I].indent), ' ') + m_Lines[m_I].text;
                parts.push_back(l);
                ++m_I;
            }
            // Strip the closing quote from the last part.
            std::string& last = parts.back();
            for (size_t i = 0; i < last.size(); ++i) {
                if (q == '"' && last[i] == '\\') { ++i; continue; }
                if (last[i] == q) {
                    if (q == '\'' && i + 1 < last.size() && last[i + 1] == '\'') { ++i; continue; }
                    last.resize(i);
                    break;
                }
            }
            std::string folded = Fold(parts);
            if (q == '"')
                return Unescape(folded);
            std::string out;
            for (size_t i = 0; i < folded.size(); ++i) {
                out += folded[i];
                if (folded[i] == '\'' && i + 1 < folded.size() && folded[i + 1] == '\'')
                    ++i;
            }
            return out;
        }
        if (rest[0] == '|' || rest[0] == '>') {
            bool literal = rest[0] == '|';
            std::string out;
            int blockIndent = -1;
            while (m_I < m_End && (m_Lines[m_I].text.empty() || m_Lines[m_I].indent > ownerIndent)) {
                const Line& l = m_Lines[m_I];
                if (blockIndent < 0 && !l.text.empty())
                    blockIndent = l.indent;
                if (!out.empty())
                    out += literal ? '\n' : ' ';
                if (!l.text.empty())
                    out += std::string(size_t(std::max(0, l.indent - blockIndent)), ' ') + l.text;
                ++m_I;
            }
            return out;
        }
        // Plain scalar, possibly continued on deeper-indented lines.
        std::vector<std::string> parts{rest};
        while (m_I < m_End && !IsBlank(m_Lines[m_I]) && m_Lines[m_I].indent > ownerIndent) {
            parts.push_back(m_Lines[m_I].text);
            ++m_I;
        }
        return parts.size() == 1 ? parts[0] : Fold(parts);
    }

    std::vector<Line>& m_Lines;
    size_t m_I;
    size_t m_End;
};

std::vector<Line> SplitLines(const std::string& text)
{
    std::vector<Line> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string raw = text.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        Line l;
        size_t first = raw.find_first_not_of(' ');
        l.indent = first == std::string::npos ? 0 : int(first);
        l.text = first == std::string::npos ? "" : raw.substr(first);
        lines.push_back(std::move(l));
        if (end == text.size())
            break;
        pos = end + 1;
    }
    return lines;
}

} // namespace

std::vector<Document> Parse(const std::string& text)
{
    std::vector<Line> lines = SplitLines(text);
    std::vector<Document> docs;
    size_t i = 0;
    auto isHeader = [&](size_t k) { return lines[k].indent == 0 && lines[k].text.rfind("---", 0) == 0; };

    // Files without document headers (.meta) are a single mapping.
    bool anyHeader = false;
    for (size_t k = 0; k < lines.size() && !anyHeader; ++k)
        anyHeader = isHeader(k);
    if (!anyHeader) {
        size_t begin = 0;
        while (begin < lines.size() && lines[begin].text.rfind('%', 0) == 0)
            ++begin;
        Document doc;
        doc.body = BlockParser(lines, begin, lines.size()).Parse();
        docs.push_back(std::move(doc));
        return docs;
    }

    while (i < lines.size()) {
        if (!isHeader(i)) {
            ++i;
            continue;
        }
        Document doc;
        const std::string& h = lines[i].text; // --- !u!4 &123 stripped
        if (size_t u = h.find("!u!"); u != std::string::npos)
            doc.classId = std::atoi(h.c_str() + u + 3);
        if (size_t a = h.find('&'); a != std::string::npos)
            doc.fileId = std::strtoll(h.c_str() + a + 1, nullptr, 10);
        doc.stripped = h.find("stripped") != std::string::npos;
        size_t begin = ++i;
        while (i < lines.size() && !isHeader(i))
            ++i;
        json top = BlockParser(lines, begin, i).Parse();
        if (top.is_object() && top.size() == 1) {
            doc.type = top.begin().key();
            doc.body = top.begin().value();
            if (!doc.body.is_object())
                doc.body = json::object();
        } else {
            doc.body = top;
        }
        docs.push_back(std::move(doc));
    }
    return docs;
}

std::vector<Document> ParseFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return Parse(buffer.str());
}

double Number(const nlohmann::json& value, double fallback)
{
    if (value.is_number())
        return value.get<double>();
    if (value.is_string()) {
        const std::string& s = value.get_ref<const std::string&>();
        if (s.empty())
            return fallback;
        char* end = nullptr;
        double v = std::strtod(s.c_str(), &end);
        return end != s.c_str() ? v : fallback;
    }
    return fallback;
}

int64_t Integer(const nlohmann::json& value, int64_t fallback)
{
    if (value.is_number_integer())
        return value.get<int64_t>();
    if (value.is_string()) {
        const std::string& s = value.get_ref<const std::string&>();
        if (s.empty())
            return fallback;
        char* end = nullptr;
        long long v = std::strtoll(s.c_str(), &end, 10);
        return end != s.c_str() ? int64_t(v) : fallback;
    }
    return value.is_number() ? int64_t(value.get<double>()) : fallback;
}

std::string String(const nlohmann::json& value)
{
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_null())
        return "";
    return value.dump();
}

} // namespace ie::UnityYaml
