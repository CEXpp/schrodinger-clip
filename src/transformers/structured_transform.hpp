#pragma once
#include "../common.hpp"
#include "../classifier.hpp"

namespace sclip {

struct ValNode {
    enum Kind { Null, Bool, Int, Float, String, Array, Object } kind = Null;
    bool boolVal = false;
    int64_t intVal = 0;
    double floatVal = 0.0;
    std::string strVal;
    std::vector<ValNode> arrItems;
    std::vector<std::pair<std::string, ValNode>> objFields; // Preserves key order
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : s_(src), pos_(0) {}

    bool Parse(ValNode& out) {
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        return pos_ == s_.size();
    }

private:
    const std::string& s_;
    size_t pos_;

    void SkipWs() {
        while (pos_ < s_.size() && static_cast<unsigned char>(s_[pos_]) <= 0x20) ++pos_;
    }

    bool ParseValue(ValNode& out) {
        SkipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return ParseObject(out);
        if (c == '[') return ParseArray(out);
        if (c == '"') {
            out.kind = ValNode::String;
            return ParseString(out.strVal);
        }
        if (c == 't' && s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out.kind = ValNode::Bool;
            out.boolVal = true;
            return true;
        }
        if (c == 'f' && s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out.kind = ValNode::Bool;
            out.boolVal = false;
            return true;
        }
        if (c == 'n' && s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out.kind = ValNode::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return false;
    }

    static void AppendUtf8Codepoint(std::string& dst, uint32_t cp) {
        if (cp <= 0x7F) {
            dst.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            dst.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            dst.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            dst.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseHex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool ParseString(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 2 <= s_.size() &&
                            s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = 0;
                            if (!ParseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                            }
                        }
                        AppendUtf8Codepoint(out, cp);
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(ValNode& out) {
        size_t start = pos_;
        bool isFloat = false;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return false;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            isFloat = true;
            ++pos_;
            if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return false;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            isFloat = true;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return false;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        std::string numStr = s_.substr(start, pos_ - start);
        try {
            if (!isFloat) {
                out.kind = ValNode::Int;
                out.intVal = std::stoll(numStr);
                out.floatVal = static_cast<double>(out.intVal);
            } else {
                out.kind = ValNode::Float;
                out.floatVal = std::stod(numStr);
            }
            return true;
        } catch (...) {
            try {
                out.kind = ValNode::Float;
                out.floatVal = std::stod(numStr);
                return true;
            } catch (...) {
                return false;
            }
        }
    }

    bool ParseArray(ValNode& out) {
        ++pos_; // skip '['
        out.kind = ValNode::Array;
        out.arrItems.clear();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            ValNode elem;
            if (!ParseValue(elem)) return false;
            out.arrItems.push_back(std::move(elem));
            SkipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
        return false;
    }

    bool ParseObject(ValNode& out) {
        ++pos_; // skip '{'
        out.kind = ValNode::Object;
        out.objFields.clear();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (pos_ < s_.size()) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            ValNode val;
            if (!ParseValue(val)) return false;
            out.objFields.emplace_back(std::move(key), std::move(val));
            SkipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
        return false;
    }
};

inline ValNode ParseScalarHeuristic(const std::string& raw) {
    std::string t = Trim(raw);
    ValNode v;
    if (t.empty() || t == "null" || t == "~" || t == "NULL") {
        v.kind = ValNode::Null;
        return v;
    }
    if ((t.front() == '"' && t.back() == '"' && t.size() >= 2) ||
        (t.front() == '\'' && t.back() == '\'' && t.size() >= 2)) {
        v.kind = ValNode::String;
        v.strVal = t.substr(1, t.size() - 2);
        return v;
    }
    if (t == "true" || t == "True" || t == "TRUE") {
        v.kind = ValNode::Bool;
        v.boolVal = true;
        return v;
    }
    if (t == "false" || t == "False" || t == "FALSE") {
        v.kind = ValNode::Bool;
        v.boolVal = false;
        return v;
    }
    // Check numeric
    bool hasDot = false;
    bool okNum = !t.empty();
    size_t i = (t[0] == '-') ? 1 : 0;
    if (i == t.size()) okNum = false;
    // Avoid treating leading-zero strings like "00123" as integers
    if (okNum && t.size() - i > 1 && t[i] == '0' && t[i + 1] != '.') okNum = false;
    for (; okNum && i < t.size(); ++i) {
        if (t[i] == '.') {
            if (hasDot) okNum = false;
            hasDot = true;
        } else if (t[i] < '0' || t[i] > '9') {
            okNum = false;
        }
    }
    if (okNum) {
        try {
            if (!hasDot) {
                v.kind = ValNode::Int;
                v.intVal = std::stoll(t);
                v.floatVal = static_cast<double>(v.intVal);
                return v;
            } else {
                v.kind = ValNode::Float;
                v.floatVal = std::stod(t);
                return v;
            }
        } catch (...) {}
    }
    v.kind = ValNode::String;
    v.strVal = t;
    return v;
}

inline bool ParseCsvToAst(const std::string& src, ValNode& out) {
    auto splitCsvRows = [](const std::string& s, char delim) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> row;
        std::string cell;
        bool inQuotes = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < s.size() && s[i + 1] == '"') {
                        cell.push_back('"');
                        ++i;
                    } else {
                        inQuotes = false;
                    }
                } else {
                    cell.push_back(c);
                }
            } else {
                if (c == '"') {
                    inQuotes = true;
                } else if (c == delim) {
                    row.push_back(cell);
                    cell.clear();
                } else if (c == '\r' || c == '\n') {
                    if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
                    row.push_back(cell);
                    cell.clear();
                    bool nonEmpty = false;
                    for (const auto& f : row) if (!Trim(f).empty()) nonEmpty = true;
                    if (nonEmpty) rows.push_back(std::move(row));
                    row.clear();
                } else {
                    cell.push_back(c);
                }
            }
        }
        if (!cell.empty() || !row.empty()) {
            row.push_back(cell);
            bool nonEmpty = false;
            for (const auto& f : row) if (!Trim(f).empty()) nonEmpty = true;
            if (nonEmpty) rows.push_back(std::move(row));
        }
        return rows;
    };

    char delim = ',';
    size_t firstNl = src.find('\n');
    std::string headerSample = (firstNl == std::string::npos) ? src : src.substr(0, firstNl);
    if (std::count(headerSample.begin(), headerSample.end(), '\t') >
        std::count(headerSample.begin(), headerSample.end(), ',')) {
        delim = '\t';
    }

    auto rows = splitCsvRows(src, delim);
    if (rows.size() < 2) return false;
    const auto& headers = rows[0];
    out.kind = ValNode::Array;
    out.arrItems.clear();
    for (size_t r = 1; r < rows.size(); ++r) {
        ValNode obj;
        obj.kind = ValNode::Object;
        for (size_t c = 0; c < headers.size(); ++c) {
            std::string key = Trim(headers[c]);
            if (key.empty()) key = "col_" + std::to_string(c + 1);
            std::string rawVal = (c < rows[r].size()) ? rows[r][c] : "";
            obj.objFields.emplace_back(key, ParseScalarHeuristic(rawVal));
        }
        out.arrItems.push_back(std::move(obj));
    }
    return !out.arrItems.empty();
}

// Lightweight indentation-aware YAML subset parser
inline bool ParseYamlToAst(const std::string& src, ValNode& out) {
    struct YLine {
        int indent;
        std::string content;
    };
    std::vector<YLine> ylines;
    for (const auto& raw : SplitLines(src)) {
        std::string trimmed = Trim(raw);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed == "---" || trimmed == "...") continue;
        int ind = 0;
        while (ind < static_cast<int>(raw.size()) && raw[ind] == ' ') ++ind;
        // Strip trailing comment if unquoted
        bool inQ = false;
        std::string clean;
        for (size_t i = static_cast<size_t>(ind); i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '"' || c == '\'') inQ = !inQ;
            if (!inQ && c == '#' && i > static_cast<size_t>(ind) && raw[i - 1] == ' ') break;
            clean.push_back(c);
        }
        clean = Trim(clean);
        if (!clean.empty()) ylines.push_back({ind, clean});
    }
    if (ylines.empty()) return false;

    std::function<ValNode(size_t&, int)> parseBlock = [&](size_t& idx, int baseIndent) -> ValNode {
        if (idx >= ylines.size()) return ValNode{};
        if (StartsWith(ylines[idx].content, "- ")) {
            ValNode arr;
            arr.kind = ValNode::Array;
            while (idx < ylines.size() && ylines[idx].indent == baseIndent && StartsWith(ylines[idx].content, "- ")) {
                std::string afterDash = Trim(ylines[idx].content.substr(2));
                ++idx;
                size_t colon = afterDash.find(':');
                if (colon != std::string::npos && colon > 0 &&
                    (colon + 1 == afterDash.size() || afterDash[colon + 1] == ' ')) {
                    ValNode itemObj;
                    itemObj.kind = ValNode::Object;
                    std::string k = Trim(afterDash.substr(0, colon));
                    std::string vpart = Trim(afterDash.substr(colon + 1));
                    if (!vpart.empty()) {
                        itemObj.objFields.emplace_back(k, ParseScalarHeuristic(vpart));
                    } else if (idx < ylines.size() && ylines[idx].indent > baseIndent) {
                        itemObj.objFields.emplace_back(k, parseBlock(idx, ylines[idx].indent));
                    } else {
                        itemObj.objFields.emplace_back(k, ValNode{});
                    }
                    while (idx < ylines.size() && ylines[idx].indent > baseIndent &&
                           !StartsWith(ylines[idx].content, "- ")) {
                        int childInd = ylines[idx].indent;
                        std::string line = ylines[idx].content;
                        size_t c2 = line.find(':');
                        if (c2 != std::string::npos) {
                            std::string k2 = Trim(line.substr(0, c2));
                            std::string v2 = Trim(line.substr(c2 + 1));
                            ++idx;
                            if (!v2.empty()) {
                                itemObj.objFields.emplace_back(k2, ParseScalarHeuristic(v2));
                            } else if (idx < ylines.size() && ylines[idx].indent > childInd) {
                                itemObj.objFields.emplace_back(k2, parseBlock(idx, ylines[idx].indent));
                            } else {
                                itemObj.objFields.emplace_back(k2, ValNode{});
                            }
                        } else {
                            ++idx;
                        }
                    }
                    arr.arrItems.push_back(std::move(itemObj));
                } else if (!afterDash.empty()) {
                    arr.arrItems.push_back(ParseScalarHeuristic(afterDash));
                } else if (idx < ylines.size() && ylines[idx].indent > baseIndent) {
                    arr.arrItems.push_back(parseBlock(idx, ylines[idx].indent));
                }
            }
            return arr;
        } else {
            ValNode obj;
            obj.kind = ValNode::Object;
            while (idx < ylines.size() && ylines[idx].indent == baseIndent && !StartsWith(ylines[idx].content, "- ")) {
                std::string line = ylines[idx].content;
                size_t colon = line.find(':');
                if (colon == std::string::npos) {
                    ++idx;
                    continue;
                }
                std::string k = Trim(line.substr(0, colon));
                std::string vpart = Trim(line.substr(colon + 1));
                ++idx;
                if (!vpart.empty()) {
                    obj.objFields.emplace_back(k, ParseScalarHeuristic(vpart));
                } else if (idx < ylines.size() && ylines[idx].indent > baseIndent) {
                    obj.objFields.emplace_back(k, parseBlock(idx, ylines[idx].indent));
                } else {
                    obj.objFields.emplace_back(k, ValNode{});
                }
            }
            return obj;
        }
    };

    size_t idx = 0;
    out = parseBlock(idx, ylines[0].indent);
    return (out.kind == ValNode::Object && !out.objFields.empty()) ||
           (out.kind == ValNode::Array && !out.arrItems.empty());
}

inline bool ParseStructuredSeed(const QuantumSeed& seed, ValNode& out) {
    std::string trimmed = Trim(seed.rawText);
    if (trimmed.empty()) return false;

    // If input starts with JSON object/array delimiters, it MUST be valid JSON (never misparse broken JSON as YAML/CSV!)
    if (trimmed.front() == '{' || trimmed.front() == '[') {
        JsonParser jp(trimmed);
        return jp.Parse(out);
    }

    if (seed.structuredSyntax == StructuredSyntax::Csv || LooksLikeCsvPayload(trimmed)) {
        if (ParseCsvToAst(trimmed, out)) return true;
    }
    if (seed.structuredSyntax == StructuredSyntax::Yaml || LooksLikeYamlPayload(trimmed)) {
        if (ParseYamlToAst(trimmed, out)) return true;
    }
    return false;
}

// ============================================================================
// Recursive Schema Inference Engine
// ============================================================================

struct SchemaType {
    bool hasNull = false;
    bool hasBool = false;
    bool hasInt = false;
    bool hasFloat = false;
    bool hasString = false;
    bool hasArray = false;
    bool hasObject = false;
    std::shared_ptr<SchemaType> elemType;
    std::vector<std::string> fieldOrder;
    std::map<std::string, std::shared_ptr<SchemaType>> fields;
    std::map<std::string, size_t> fieldHitCount;
    size_t objectSamples = 0;
};

inline void MergeValIntoSchema(const ValNode& v, SchemaType& st) {
    switch (v.kind) {
        case ValNode::Null: st.hasNull = true; break;
        case ValNode::Bool: st.hasBool = true; break;
        case ValNode::Int: st.hasInt = true; break;
        case ValNode::Float: st.hasFloat = true; break;
        case ValNode::String: st.hasString = true; break;
        case ValNode::Array:
            st.hasArray = true;
            if (!st.elemType) st.elemType = std::make_shared<SchemaType>();
            for (const auto& item : v.arrItems) {
                MergeValIntoSchema(item, *st.elemType);
            }
            break;
        case ValNode::Object:
            st.hasObject = true;
            ++st.objectSamples;
            for (const auto& kv : v.objFields) {
                if (st.fields.find(kv.first) == st.fields.end()) {
                    st.fieldOrder.push_back(kv.first);
                    st.fields[kv.first] = std::make_shared<SchemaType>();
                }
                st.fieldHitCount[kv.first] += 1;
                MergeValIntoSchema(kv.second, *st.fields[kv.first]);
            }
            break;
    }
}

inline std::string ToPascalCase(const std::string& raw) {
    std::string out;
    bool capNext = true;
    for (char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            if (out.empty() && c >= '0' && c <= '9') out += "Item";
            if (capNext && c >= 'a' && c <= 'z') {
                out.push_back(static_cast<char>(c - 'a' + 'A'));
            } else {
                out.push_back(c);
            }
            capNext = false;
        } else {
            capNext = true;
        }
    }
    return out.empty() ? "AutoEntity" : out;
}

inline std::string ToSnakeCase(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') {
            if (!out.empty() && out.back() != '_' &&
                ((i > 0 && raw[i - 1] >= 'a' && raw[i - 1] <= 'z') ||
                 (i + 1 < raw.size() && raw[i + 1] >= 'a' && raw[i + 1] <= 'z'))) {
                out.push_back('_');
            }
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (out.empty() && c >= '0' && c <= '9') out += "f_";
            out.push_back(c);
        } else {
            if (!out.empty() && out.back() != '_') out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "field_val" : out;
}

inline bool IsValidTsIdent(const std::string& k) {
    if (k.empty()) return false;
    auto isStart = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; };
    auto isPart = [&](char c) { return isStart(c) || (c >= '0' && c <= '9'); };
    if (!isStart(k[0])) return false;
    for (char c : k) if (!isPart(c)) return false;
    return true;
}

// 1. Collapse to TypeScript Interfaces
inline std::string CollapseToTypeScript(const ValNode& ast, const std::string& rootName = "QuantumPayload") {
    SchemaType rootSchema;
    MergeValIntoSchema(ast, rootSchema);

    std::vector<std::string> emittedBlocks;
    std::set<std::string> usedNames;

    auto allocName = [&](const std::string& hint) {
        std::string base = ToPascalCase(hint);
        std::string cand = base;
        int n = 2;
        while (usedNames.count(cand)) cand = base + std::to_string(n++);
        usedNames.insert(cand);
        return cand;
    };

    std::function<std::string(const SchemaType&, const std::string&, bool)> tsTypeExpr;
    std::function<std::string(const SchemaType&, const std::string&)> emitObjectInterface =
        [&](const SchemaType& st, const std::string& nameHint) -> std::string {
        std::string ifaceName = allocName(nameHint);
        std::ostringstream oss;
        oss << "export interface " << ifaceName << " {\r\n";
        for (const auto& key : st.fieldOrder) {
            const auto& fst = *st.fields.at(key);
            bool optional = st.fieldHitCount.at(key) < st.objectSamples;
            std::string propKey = IsValidTsIdent(key) ? key : ("\"" + JsonEscape(key) + "\"");
            std::string tExpr = tsTypeExpr(fst, ifaceName + "_" + key, true);
            oss << "  " << propKey << (optional ? "?: " : ": ") << tExpr << ";\r\n";
        }
        oss << "}";
        emittedBlocks.push_back(oss.str());
        return ifaceName;
    };

    tsTypeExpr = [&](const SchemaType& st, const std::string& hint, bool includeNull) -> std::string {
        std::vector<std::string> parts;
        if (st.hasObject) {
            parts.push_back(emitObjectInterface(st, hint));
        }
        if (st.hasArray) {
            std::string inner = st.elemType ? tsTypeExpr(*st.elemType, hint + "Item", true) : "unknown";
            if (inner.find(" | ") != std::string::npos) {
                parts.push_back("(" + inner + ")[]");
            } else {
                parts.push_back(inner + "[]");
            }
        }
        if (st.hasString) parts.push_back("string");
        if (st.hasInt || st.hasFloat) parts.push_back("number");
        if (st.hasBool) parts.push_back("boolean");
        if (includeNull && st.hasNull) parts.push_back("null");
        if (parts.empty()) return "unknown";
        std::string joined = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) joined += " | " + parts[i];
        return joined;
    };

    std::ostringstream finalOut;
    if (rootSchema.hasObject) {
        emitObjectInterface(rootSchema, rootName);
    } else if (rootSchema.hasArray && rootSchema.elemType && rootSchema.elemType->hasObject) {
        std::string itemIface = emitObjectInterface(*rootSchema.elemType, rootName + "Item");
        emittedBlocks.push_back("export type " + allocName(rootName) + " = " + itemIface + "[];");
    } else {
        std::string t = tsTypeExpr(rootSchema, rootName, true);
        emittedBlocks.push_back("export type " + allocName(rootName) + " = " + t + ";");
    }

    for (size_t i = 0; i < emittedBlocks.size(); ++i) {
        if (i > 0) finalOut << "\r\n\r\n";
        finalOut << emittedBlocks[i];
    }
    return finalOut.str();
}

// 2. Collapse to Python @dataclass Models
inline std::string CollapseToPythonDataclass(const ValNode& ast, const std::string& rootName = "QuantumPayload") {
    SchemaType rootSchema;
    MergeValIntoSchema(ast, rootSchema);

    std::vector<std::string> classes; // children emitted first!
    std::set<std::string> usedNames;
    auto allocName = [&](const std::string& hint) {
        std::string base = ToPascalCase(hint);
        std::string cand = base;
        int n = 2;
        while (usedNames.count(cand)) cand = base + std::to_string(n++);
        usedNames.insert(cand);
        return cand;
    };

    static const std::set<std::string> kPyKeywords = {
        "class", "def", "from", "import", "return", "pass", "None", "True", "False",
        "and", "or", "not", "in", "is", "lambda", "global", "yield", "try", "except", "for", "while", "if", "else"
    };

    std::function<std::string(const SchemaType&, const std::string&)> pyTypeExpr;
    std::function<std::string(const SchemaType&, const std::string&)> emitPyClass =
        [&](const SchemaType& st, const std::string& hint) -> std::string {
        std::string clsName = allocName(hint);
        struct PyField {
            std::string name;
            std::string typeStr;
            bool hasDefault;
        };
        std::vector<PyField> reqFields, optFields;
        std::set<std::string> usedFieldNames;
        for (const auto& key : st.fieldOrder) {
            const auto& fst = *st.fields.at(key);
            bool optional = (st.fieldHitCount.at(key) < st.objectSamples) || fst.hasNull;
            std::string pyKey = ToSnakeCase(key);
            if (kPyKeywords.count(pyKey)) pyKey += "_";
            std::string baseKey = pyKey;
            int dupIdx = 2;
            while (usedFieldNames.count(pyKey)) {
                pyKey = baseKey + "_" + std::to_string(dupIdx++);
            }
            usedFieldNames.insert(pyKey);
            std::string baseT = pyTypeExpr(fst, clsName + "_" + pyKey);
            if (optional) {
                optFields.push_back({pyKey, "Optional[" + baseT + "] = None", true});
            } else {
                reqFields.push_back({pyKey, baseT, false});
            }
        }
        std::ostringstream oss;
        oss << "@dataclass\r\nclass " << clsName << ":\r\n";
        if (reqFields.empty() && optFields.empty()) {
            oss << "    pass";
        } else {
            for (const auto& f : reqFields) oss << "    " << f.name << ": " << f.typeStr << "\r\n";
            for (const auto& f : optFields) oss << "    " << f.name << ": " << f.typeStr << "\r\n";
        }
        std::string rendered = oss.str();
        while (!rendered.empty() && (rendered.back() == '\n' || rendered.back() == '\r')) rendered.pop_back();
        classes.push_back(rendered);
        return clsName;
    };

    pyTypeExpr = [&](const SchemaType& st, const std::string& hint) -> std::string {
        if (st.hasObject) return emitPyClass(st, hint);
        if (st.hasArray) {
            std::string inner = st.elemType ? pyTypeExpr(*st.elemType, hint + "Item") : "Any";
            return "List[" + inner + "]";
        }
        if (st.hasString) return "str";
        if (st.hasFloat) return "float";
        if (st.hasInt) return "int";
        if (st.hasBool) return "bool";
        return "Any";
    };

    std::ostringstream out;
    out << "from dataclasses import dataclass\r\n"
        << "from typing import Any, List, Optional\r\n\r\n";

    if (rootSchema.hasObject) {
        emitPyClass(rootSchema, rootName);
    } else if (rootSchema.hasArray && rootSchema.elemType && rootSchema.elemType->hasObject) {
        std::string itemCls = emitPyClass(*rootSchema.elemType, rootName + "Item");
        classes.push_back(allocName(rootName) + " = List[" + itemCls + "]");
    } else {
        classes.push_back(allocName(rootName) + " = " + pyTypeExpr(rootSchema, rootName));
    }

    for (size_t i = 0; i < classes.size(); ++i) {
        if (i > 0) out << "\r\n\r\n";
        out << classes[i];
    }
    return out.str();
}

// 3. Collapse to Go Structs
inline std::string CollapseToGoStruct(const ValNode& ast, const std::string& rootName = "QuantumPayload") {
    SchemaType rootSchema;
    MergeValIntoSchema(ast, rootSchema);

    std::vector<std::string> structs;
    std::set<std::string> usedNames;
    auto allocName = [&](const std::string& hint) {
        std::string base = ToPascalCase(hint);
        std::string cand = base;
        int n = 2;
        while (usedNames.count(cand)) cand = base + std::to_string(n++);
        usedNames.insert(cand);
        return cand;
    };

    std::function<std::string(const SchemaType&, const std::string&)> goTypeExpr;
    std::function<std::string(const SchemaType&, const std::string&)> emitGoStruct =
        [&](const SchemaType& st, const std::string& hint) -> std::string {
        std::string stName = allocName(hint);
        std::ostringstream oss;
        oss << "type " << stName << " struct {\r\n";
        std::set<std::string> usedFieldNames;
        for (const auto& key : st.fieldOrder) {
            const auto& fst = *st.fields.at(key);
            bool optional = (st.fieldHitCount.at(key) < st.objectSamples) || fst.hasNull;
            std::string baseGoField = ToPascalCase(key);
            if (baseGoField == "AutoEntity") baseGoField = "Field";
            std::string goField = baseGoField;
            int dupIdx = 2;
            while (usedFieldNames.count(goField)) {
                goField = baseGoField + std::to_string(dupIdx++);
            }
            usedFieldNames.insert(goField);
            std::string baseT = goTypeExpr(fst, stName + goField);
            if (optional && baseT != "any" && !StartsWith(baseT, "[]")) {
                baseT = "*" + baseT;
            }
            oss << "\t" << goField << " " << baseT
                << " `json:\"" << JsonEscape(key) << (optional ? ",omitempty" : "") << "\"`\r\n";
        }
        oss << "}";
        structs.push_back(oss.str());
        return stName;
    };

    goTypeExpr = [&](const SchemaType& st, const std::string& hint) -> std::string {
        if (st.hasObject) return emitGoStruct(st, hint);
        if (st.hasArray) {
            std::string inner = st.elemType ? goTypeExpr(*st.elemType, hint + "Item") : "any";
            return "[]" + inner;
        }
        if (st.hasString) return "string";
        if (st.hasFloat) return "float64";
        if (st.hasInt) return "int64";
        if (st.hasBool) return "bool";
        return "any";
    };

    if (rootSchema.hasObject) {
        emitGoStruct(rootSchema, rootName);
    } else if (rootSchema.hasArray && rootSchema.elemType && rootSchema.elemType->hasObject) {
        std::string itemName = emitGoStruct(*rootSchema.elemType, rootName + "Item");
        structs.push_back("type " + allocName(rootName) + " []" + itemName);
    } else {
        structs.push_back("type " + allocName(rootName) + " " + goTypeExpr(rootSchema, rootName));
    }

    std::ostringstream out;
    out << "package models\r\n\r\n";
    for (int i = static_cast<int>(structs.size()) - 1; i >= 0; --i) {
        out << structs[static_cast<size_t>(i)];
        if (i > 0) out << "\r\n\r\n";
    }
    return out.str();
}

// 4. Collapse to Rust Serde Structs
inline std::string CollapseToRustStruct(const ValNode& ast, const std::string& rootName = "QuantumPayload") {
    SchemaType rootSchema;
    MergeValIntoSchema(ast, rootSchema);

    std::vector<std::string> structs;
    std::set<std::string> usedNames;
    auto allocName = [&](const std::string& hint) {
        std::string base = ToPascalCase(hint);
        std::string cand = base;
        int n = 2;
        while (usedNames.count(cand)) cand = base + std::to_string(n++);
        usedNames.insert(cand);
        return cand;
    };

    static const std::set<std::string> kRustKeywords = {
        "type", "match", "fn", "let", "mut", "pub", "struct", "enum", "impl", "trait",
        "mod", "use", "crate", "self", "super", "where", "async", "await", "move", "ref", "in", "loop"
    };

    std::function<std::string(const SchemaType&, const std::string&)> rsTypeExpr;
    std::function<std::string(const SchemaType&, const std::string&)> emitRsStruct =
        [&](const SchemaType& st, const std::string& hint) -> std::string {
        std::string stName = allocName(hint);
        std::ostringstream oss;
        oss << "#[derive(Debug, Clone, Serialize, Deserialize)]\r\n"
            << "pub struct " << stName << " {\r\n";
        std::set<std::string> usedFieldNames;
        for (const auto& key : st.fieldOrder) {
            const auto& fst = *st.fields.at(key);
            bool optional = (st.fieldHitCount.at(key) < st.objectSamples) || fst.hasNull;
            std::string baseSnake = ToSnakeCase(key);
            std::string snake = baseSnake;
            int dupIdx = 2;
            while (usedFieldNames.count(snake)) {
                snake = baseSnake + "_" + std::to_string(dupIdx++);
            }
            usedFieldNames.insert(snake);
            bool needRename = (snake != key);
            if (kRustKeywords.count(snake)) {
                snake = "r#" + snake;
                needRename = true;
            }
            if (needRename) {
                oss << "    #[serde(rename = \"" << JsonEscape(key) << "\")]\r\n";
            }
            std::string baseT = rsTypeExpr(fst, stName + ToPascalCase(snake));
            if (optional) baseT = "Option<" + baseT + ">";
            oss << "    pub " << snake << ": " << baseT << ",\r\n";
        }
        oss << "}";
        structs.push_back(oss.str());
        return stName;
    };

    rsTypeExpr = [&](const SchemaType& st, const std::string& hint) -> std::string {
        if (st.hasObject) return emitRsStruct(st, hint);
        if (st.hasArray) {
            std::string inner = st.elemType ? rsTypeExpr(*st.elemType, hint + "Item") : "serde_json::Value";
            return "Vec<" + inner + ">";
        }
        if (st.hasString) return "String";
        if (st.hasFloat) return "f64";
        if (st.hasInt) return "i64";
        if (st.hasBool) return "bool";
        return "serde_json::Value";
    };

    if (rootSchema.hasObject) {
        emitRsStruct(rootSchema, rootName);
    } else if (rootSchema.hasArray && rootSchema.elemType && rootSchema.elemType->hasObject) {
        std::string itemName = emitRsStruct(*rootSchema.elemType, rootName + "Item");
        structs.push_back("pub type " + allocName(rootName) + " = Vec<" + itemName + ">;");
    } else {
        structs.push_back("pub type " + allocName(rootName) + " = " + rsTypeExpr(rootSchema, rootName) + ";");
    }

    std::ostringstream out;
    out << "use serde::{Deserialize, Serialize};\r\n\r\n";
    for (int i = static_cast<int>(structs.size()) - 1; i >= 0; --i) {
        out << structs[static_cast<size_t>(i)];
        if (i > 0) out << "\r\n\r\n";
    }
    return out.str();
}

// ============================================================================
// Flattened Relational Table Representation (powers SQL, CF_HTML, CSV, TSV, .xlsx)
// ============================================================================

struct FlatCell {
    ValNode::Kind kind = ValNode::Null;
    std::string text;
    double numVal = 0.0;
    bool isNumeric = false;
};

struct FlatTable {
    std::string tableName = "quantum_records";
    std::vector<std::string> columns;
    std::vector<SchemaType> colSchemas;
    std::vector<std::vector<FlatCell>> rows;
};

inline std::string SerializeValCompactJson(const ValNode& v) {
    switch (v.kind) {
        case ValNode::Null: return "";
        case ValNode::Bool: return v.boolVal ? "true" : "false";
        case ValNode::Int: return std::to_string(v.intVal);
        case ValNode::Float: {
            std::ostringstream oss;
            oss << v.floatVal;
            return oss.str();
        }
        case ValNode::String: return v.strVal;
        case ValNode::Array: {
            // If all elements are scalars, join cleanly; else compact JSON
            bool allScalars = true;
            for (const auto& item : v.arrItems) {
                if (item.kind == ValNode::Array || item.kind == ValNode::Object) {
                    allScalars = false;
                    break;
                }
            }
            if (allScalars) {
                std::string s;
                for (size_t i = 0; i < v.arrItems.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += SerializeValCompactJson(v.arrItems[i]);
                }
                return s;
            }
            std::string s = "[";
            for (size_t i = 0; i < v.arrItems.size(); ++i) {
                if (i > 0) s += ",";
                if (v.arrItems[i].kind == ValNode::String) s += "\"" + JsonEscape(v.arrItems[i].strVal) + "\"";
                else s += SerializeValCompactJson(v.arrItems[i]);
            }
            return s + "]";
        }
        case ValNode::Object: {
            std::string s = "{";
            for (size_t i = 0; i < v.objFields.size(); ++i) {
                if (i > 0) s += ",";
                s += "\"" + JsonEscape(v.objFields[i].first) + "\":";
                const auto& fv = v.objFields[i].second;
                if (fv.kind == ValNode::String) s += "\"" + JsonEscape(fv.strVal) + "\"";
                else s += SerializeValCompactJson(fv);
            }
            return s + "}";
        }
    }
    return "";
}

inline void FlattenObjectIntoRowMap(const ValNode& obj,
                                    const std::string& prefix,
                                    const std::string& sep,
                                    std::vector<std::string>& colOrder,
                                    std::set<std::string>& colSet,
                                    std::map<std::string, ValNode>& rowMap) {
    if (obj.kind != ValNode::Object) {
        std::string col = prefix.empty() ? "value" : prefix;
        if (!colSet.count(col)) { colSet.insert(col); colOrder.push_back(col); }
        rowMap[col] = obj;
        return;
    }
    for (const auto& kv : obj.objFields) {
        std::string path = prefix.empty() ? kv.first : (prefix + sep + kv.first);
        if (kv.second.kind == ValNode::Object && !kv.second.objFields.empty()) {
            FlattenObjectIntoRowMap(kv.second, path, sep, colOrder, colSet, rowMap);
        } else {
            if (!colSet.count(path)) {
                colSet.insert(path);
                colOrder.push_back(path);
            }
            rowMap[path] = kv.second;
        }
    }
}

inline FlatTable FlattenAstToTable(const ValNode& ast, const std::string& sep = ".") {
    FlatTable tbl;
    std::vector<ValNode> recordNodes;

    if (ast.kind == ValNode::Array) {
        recordNodes = ast.arrItems;
    } else if (ast.kind == ValNode::Object) {
        // Check if root object wraps a primary array of objects (e.g. {"orders": [{...}, {...}]})
        int objArrayIdx = -1;
        for (size_t i = 0; i < ast.objFields.size(); ++i) {
            const auto& fv = ast.objFields[i].second;
            if (fv.kind == ValNode::Array && !fv.arrItems.empty() && fv.arrItems[0].kind == ValNode::Object) {
                objArrayIdx = static_cast<int>(i);
                tbl.tableName = ToSnakeCase(ast.objFields[i].first);
                break;
            }
        }
        if (objArrayIdx >= 0) {
            const auto& arrField = ast.objFields[static_cast<size_t>(objArrayIdx)];
            for (const auto& elem : arrField.second.arrItems) {
                ValNode merged;
                merged.kind = ValNode::Object;
                // Keep top-level scalar context fields alongside unpacked child array rows
                for (size_t i = 0; i < ast.objFields.size(); ++i) {
                    if (static_cast<int>(i) == objArrayIdx) continue;
                    const auto& sv = ast.objFields[i].second;
                    if (sv.kind != ValNode::Array) {
                        merged.objFields.push_back(ast.objFields[i]);
                    }
                }
                if (elem.kind == ValNode::Object) {
                    for (const auto& ekv : elem.objFields) {
                        merged.objFields.push_back(ekv);
                    }
                }
                recordNodes.push_back(std::move(merged));
            }
        } else {
            recordNodes.push_back(ast);
        }
    } else {
        recordNodes.push_back(ast);
    }

    std::set<std::string> colSet;
    std::vector<std::map<std::string, ValNode>> rawRows;
    rawRows.reserve(recordNodes.size());
    for (const auto& rec : recordNodes) {
        std::map<std::string, ValNode> rmap;
        FlattenObjectIntoRowMap(rec, "", sep, tbl.columns, colSet, rmap);
        rawRows.push_back(std::move(rmap));
    }

    tbl.colSchemas.resize(tbl.columns.size());
    tbl.rows.reserve(rawRows.size());
    for (const auto& rmap : rawRows) {
        std::vector<FlatCell> row(tbl.columns.size());
        for (size_t c = 0; c < tbl.columns.size(); ++c) {
            auto it = rmap.find(tbl.columns[c]);
            if (it == rmap.end() || it->second.kind == ValNode::Null) {
                tbl.colSchemas[c].hasNull = true;
                row[c].kind = ValNode::Null;
                row[c].text = "";
            } else {
                const ValNode& v = it->second;
                MergeValIntoSchema(v, tbl.colSchemas[c]);
                row[c].kind = v.kind;
                row[c].text = SerializeValCompactJson(v);
                if (v.kind == ValNode::Int) {
                    row[c].isNumeric = true;
                    row[c].numVal = static_cast<double>(v.intVal);
                } else if (v.kind == ValNode::Float) {
                    row[c].isNumeric = true;
                    row[c].numVal = v.floatVal;
                }
            }
        }
        tbl.rows.push_back(std::move(row));
    }
    return tbl;
}

// 5. Collapse to SQL DDL + Batch INSERT
inline std::string CollapseToSql(const ValNode& ast) {
    FlatTable tbl = FlattenAstToTable(ast, "_");
    if (tbl.columns.empty()) return "-- Empty dataset";

    std::vector<std::string> sqlCols;
    std::set<std::string> usedCols;
    for (const auto& rawCol : tbl.columns) {
        std::string sc = ToSnakeCase(rawCol);
        std::string cand = sc;
        int n = 2;
        while (usedCols.count(cand)) cand = sc + "_" + std::to_string(n++);
        usedCols.insert(cand);
        sqlCols.push_back(cand);
    }

    auto sqlTypeFor = [](const SchemaType& st) -> std::string {
        if (st.hasString || st.hasArray || st.hasObject) return "TEXT";
        if (st.hasFloat) return "DOUBLE PRECISION";
        if (st.hasInt) return "BIGINT";
        if (st.hasBool) return "BOOLEAN";
        return "TEXT";
    };

    std::ostringstream oss;
    oss << "CREATE TABLE IF NOT EXISTS " << tbl.tableName << " (\r\n";
    for (size_t c = 0; c < sqlCols.size(); ++c) {
        oss << "    " << sqlCols[c] << " " << sqlTypeFor(tbl.colSchemas[c]);
        if (!tbl.colSchemas[c].hasNull && c == 0 && (sqlCols[c] == "id" || EndsWith(sqlCols[c], "_id"))) {
            oss << " PRIMARY KEY";
        }
        if (c + 1 < sqlCols.size()) oss << ",";
        oss << "\r\n";
    }
    oss << ");\r\n\r\n";

    if (!tbl.rows.empty()) {
        oss << "INSERT INTO " << tbl.tableName << " (";
        for (size_t c = 0; c < sqlCols.size(); ++c) {
            if (c > 0) oss << ", ";
            oss << sqlCols[c];
        }
        oss << ") VALUES\r\n";

        for (size_t r = 0; r < tbl.rows.size(); ++r) {
            oss << "    (";
            for (size_t c = 0; c < tbl.rows[r].size(); ++c) {
                if (c > 0) oss << ", ";
                const auto& cell = tbl.rows[r][c];
                if (cell.kind == ValNode::Null) {
                    oss << "NULL";
                } else if (cell.kind == ValNode::Int || cell.kind == ValNode::Float) {
                    oss << cell.text;
                } else if (cell.kind == ValNode::Bool) {
                    oss << (cell.text == "true" ? "TRUE" : "FALSE");
                } else {
                    oss << "'" << ReplaceAll(cell.text, "'", "''") << "'";
                }
            }
            oss << ")" << (r + 1 < tbl.rows.size() ? "," : ";") << "\r\n";
        }
    }
    return oss.str();
}

// 6. Collapse to CSV or TSV
inline std::string CollapseToDelimited(const ValNode& ast, char delim = ',') {
    FlatTable tbl = FlattenAstToTable(ast, ".");
    auto quoteField = [&](const std::string& s) -> std::string {
        bool needQ = false;
        for (char c : s) {
            if (c == delim || c == '"' || c == '\r' || c == '\n') {
                needQ = true;
                break;
            }
        }
        if (!needQ) return s;
        return "\"" + ReplaceAll(s, "\"", "\"\"") + "\"";
    };

    std::ostringstream oss;
    for (size_t c = 0; c < tbl.columns.size(); ++c) {
        if (c > 0) oss << delim;
        oss << quoteField(tbl.columns[c]);
    }
    oss << "\r\n";
    for (const auto& row : tbl.rows) {
        for (size_t c = 0; c < row.size(); ++c) {
            if (c > 0) oss << delim;
            oss << quoteField(row[c].text);
        }
        oss << "\r\n";
    }
    return oss.str();
}

// 7. Collapse to Markdown Pipe Table
inline std::string CollapseToMarkdownTable(const ValNode& ast) {
    FlatTable tbl = FlattenAstToTable(ast, ".");
    if (tbl.columns.empty()) return "";
    std::ostringstream oss;
    oss << "|";
    for (const auto& col : tbl.columns) oss << " " << ReplaceAll(col, "|", "\\|") << " |";
    oss << "\r\n|";
    for (size_t c = 0; c < tbl.columns.size(); ++c) {
        bool num = (tbl.colSchemas[c].hasInt || tbl.colSchemas[c].hasFloat) && !tbl.colSchemas[c].hasString;
        oss << (num ? " ---: |" : " --- |");
    }
    oss << "\r\n";
    for (const auto& row : tbl.rows) {
        oss << "|";
        for (const auto& cell : row) {
            std::string clean = ReplaceAll(ReplaceAll(cell.text, "\r", ""), "\n", " ");
            oss << " " << ReplaceAll(clean, "|", "\\|") << " |";
        }
        oss << "\r\n";
    }
    return oss.str();
}

// 8. Collapse to Excel-ready CF_HTML Table
inline std::string CollapseToCfHtmlTable(const ValNode& ast) {
    FlatTable tbl = FlattenAstToTable(ast, ".");
    std::ostringstream frag;
    frag << "<table border=\"1\" cellspacing=\"0\" cellpadding=\"6\" "
         << "style=\"border-collapse:collapse;font-family:'Segoe UI',Calibri,sans-serif;font-size:11pt;\">\r\n"
         << "<thead><tr style=\"background-color:#1e3a8a;color:#ffffff;font-weight:bold;\">";
    for (const auto& col : tbl.columns) {
        frag << "<th style=\"border:1px solid #cbd5e1;padding:6px 10px;\">" << HtmlEscape(col) << "</th>";
    }
    frag << "</tr></thead>\r\n<tbody>\r\n";
    for (size_t r = 0; r < tbl.rows.size(); ++r) {
        const char* bg = (r % 2 == 1) ? "#f8fafc" : "#ffffff";
        frag << "<tr style=\"background-color:" << bg << ";\">";
        for (const auto& cell : tbl.rows[r]) {
            frag << "<td style=\"border:1px solid #cbd5e1;padding:5px 10px;\">" << HtmlEscape(cell.text) << "</td>";
        }
        frag << "</tr>\r\n";
    }
    frag << "</tbody></table>";
    return BuildCfHtmlPayload(frag.str());
}

// 9. Collapse to Authentic Styled OpenXML .xlsx Workbook Bytes!
inline std::vector<uint8_t> CollapseToStyledXlsxBytes(const ValNode& ast) {
    FlatTable tbl = FlattenAstToTable(ast, ".");
    auto colLetters = [](size_t idx) -> std::string {
        std::string res;
        size_t n = idx + 1;
        while (n > 0) {
            size_t rem = (n - 1) % 26;
            res.insert(res.begin(), static_cast<char>('A' + rem));
            n = (n - 1) / 26;
        }
        return res;
    };

    const std::string contentTypesXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>";

    const std::string rootRelsXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";

    const std::string workbookXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"SchrodingerClip\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>";

    const std::string workbookRelsXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>";

    // Style 0: default, Style 1: Bold white on #1E3A8A header with border, Style 2: Data cell with border
    const std::string stylesXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"2\">"
        "<font><sz val=\"11\"/><color rgb=\"FF0F172A\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/></font>"
        "</fonts>"
        "<fills count=\"3\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1E3A8A\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border>"
        "<left style=\"thin\"><color rgb=\"FFCBD5E1\"/></left>"
        "<right style=\"thin\"><color rgb=\"FFCBD5E1\"/></right>"
        "<top style=\"thin\"><color rgb=\"FFCBD5E1\"/></top>"
        "<bottom style=\"thin\"><color rgb=\"FFCBD5E1\"/></bottom>"
        "<diagonal/>"
        "</border>"
        "</borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"3\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyBorder=\"1\"/>"
        "</cellXfs>"
        "</styleSheet>";

    std::ostringstream sheet;
    sheet << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
          << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";

    if (!tbl.columns.empty()) {
        sheet << "<cols>";
        for (size_t c = 0; c < tbl.columns.size(); ++c) {
            size_t maxLen = tbl.columns[c].size();
            for (const auto& row : tbl.rows) {
                if (c < row.size()) maxLen = std::max(maxLen, row[c].text.size());
            }
            double width = std::min<double>(48.0, std::max<double>(12.0, static_cast<double>(maxLen) + 4.0));
            sheet << "<col min=\"" << (c + 1) << "\" max=\"" << (c + 1)
                  << "\" width=\"" << width << "\" customWidth=\"1\"/>";
        }
        sheet << "</cols>";
    }

    auto xmlSafeEscape = [](const std::string& s) -> std::string {
        std::string cleaned;
        cleaned.reserve(s.size());
        for (unsigned char c : s) {
            if (c >= 0x20 || c == '\t' || c == '\n' || c == '\r') {
                cleaned.push_back(static_cast<char>(c));
            }
        }
        return HtmlEscape(cleaned);
    };

    sheet << "<sheetData>";
    if (!tbl.columns.empty()) {
        sheet << "<row r=\"1\" ht=\"22\" customHeight=\"1\">";
        for (size_t c = 0; c < tbl.columns.size(); ++c) {
            std::string ref = colLetters(c) + "1";
            sheet << "<c r=\"" << ref << "\" s=\"1\" t=\"inlineStr\"><is><t>"
                  << xmlSafeEscape(tbl.columns[c]) << "</t></is></c>";
        }
        sheet << "</row>";

        for (size_t r = 0; r < tbl.rows.size(); ++r) {
            size_t rowNum = r + 2;
            sheet << "<row r=\"" << rowNum << "\">";
            for (size_t c = 0; c < tbl.rows[r].size(); ++c) {
                std::string ref = colLetters(c) + std::to_string(rowNum);
                const auto& cell = tbl.rows[r][c];
                if (cell.kind == ValNode::Null) {
                    sheet << "<c r=\"" << ref << "\" s=\"2\"/>";
                } else if (cell.isNumeric && std::isfinite(cell.numVal)) {
                    sheet << "<c r=\"" << ref << "\" s=\"2\" t=\"n\"><v>" << cell.text << "</v></c>";
                } else if (cell.kind == ValNode::Bool) {
                    sheet << "<c r=\"" << ref << "\" s=\"2\" t=\"b\"><v>" << (cell.text == "true" ? "1" : "0") << "</v></c>";
                } else {
                    sheet << "<c r=\"" << ref << "\" s=\"2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">"
                          << xmlSafeEscape(cell.text) << "</t></is></c>";
                }
            }
            sheet << "</row>";
        }
    }
    sheet << "</sheetData></worksheet>";

    StoredZipBuilder zip;
    zip.AddFile("[Content_Types].xml", contentTypesXml);
    zip.AddFile("_rels/.rels", rootRelsXml);
    zip.AddFile("xl/workbook.xml", workbookXml);
    zip.AddFile("xl/_rels/workbook.xml.rels", workbookRelsXml);
    zip.AddFile("xl/styles.xml", stylesXml);
    zip.AddFile("xl/worksheets/sheet1.xml", sheet.str());
    return zip.Build();
}

} // namespace sclip
