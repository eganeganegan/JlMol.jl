#include "core/MmcifMetadata.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>

namespace jlmol {
namespace {

struct Token {
    std::string value;
    bool quoted{};
};

std::string lower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

class CifTokens {
public:
    explicit CifTokens(const std::filesystem::path& path) : input_(path) {}

    std::optional<Token> next() {
        while (true) {
            if (column_ == line_.size() || line_[column_] == '#') {
                if (!std::getline(input_, line_)) return std::nullopt;
                column_ = 0;
                if (!line_.empty() && line_[0] == ';') {
                    // Semicolon text fields cannot be residue identifiers.
                    while (std::getline(input_, line_)) {
                        if (!line_.empty() && line_[0] == ';') break;
                    }
                    column_ = line_.size();
                    return Token{"", true};
                }
                continue;
            }
            if (space(line_[column_])) {
                ++column_;
                continue;
            }
            const char delimiter = line_[column_];
            if (delimiter == '\'' || delimiter == '"') {
                const auto start = ++column_;
                while (column_ < line_.size()) {
                    if (line_[column_] == delimiter &&
                        (column_ + 1 == line_.size() || space(line_[column_ + 1]))) {
                        return Token{line_.substr(start, column_++ - start), true};
                    }
                    ++column_;
                }
                return Token{"", true};
            }
            const auto start = column_;
            while (column_ < line_.size() && !space(line_[column_])) ++column_;
            return Token{line_.substr(start, column_ - start), false};
        }
    }

private:
    std::ifstream input_;
    std::string line_;
    std::size_t column_{};
};

bool tag(const Token& token) { return !token.quoted && token.value.starts_with('_'); }

bool control(const Token& token) {
    if (token.quoted) return false;
    if (tag(token)) return true;
    const auto value = lower(token.value);
    return value == "loop_" || value == "stop_" || value == "global_" ||
           value.starts_with("data_") || value.starts_with("save_");
}

using Label = std::pair<std::string, int>;
using Author = std::tuple<std::string, int, std::string>;
using Fields = std::map<std::string, std::string, std::less<>>;

std::optional<int> integer(std::string_view text) {
    int result{};
    if (text.empty()) return std::nullopt;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return result;
}

std::string_view field(const Fields& fields, std::string_view name) {
    const auto it = fields.find(name);
    if (it == fields.end() || it->second == "." || it->second == "?") return {};
    return it->second;
}

std::optional<Label> label(std::string_view chain, std::string_view sequence) {
    const auto id = integer(sequence);
    if (chain.empty() || !id) return std::nullopt;
    return Label{chain, *id};
}

std::optional<Author> author(std::string_view chain, std::string_view sequence, std::string_view insertion) {
    const auto id = integer(sequence);
    if (chain.empty() || !id) return std::nullopt;
    return Author{chain, *id, insertion};
}

struct PendingRange {
    Fields fields;
    std::string category;
    SecondaryStructure structure;
};

} // namespace

MmcifMetadata readMmcifMetadata(const std::filesystem::path& path) {
    MmcifMetadata result;
    CifTokens tokens(path);
    std::map<Author, std::optional<Label>> authorLabels;
    std::vector<PendingRange> pending;
    Fields scalars;

    const auto addRange = [&](Fields fields, const std::string& category) {
        if (fields.empty()) return;
        const bool helix = category == "_struct_conf.";
        if (helix && !lower(std::string(field(fields, "_struct_conf.conf_type_id"))).starts_with("helx")) return;
        pending.push_back({std::move(fields), category, helix ? SecondaryStructure::Helix : SecondaryStructure::Sheet});
    };
    const auto addScalars = [&] {
        for (const std::string prefix : {"_struct_conf.", "_struct_sheet_range."}) {
            Fields fields;
            for (const auto& [key, value] : scalars) {
                if (key.starts_with(prefix)) fields.emplace(key, value);
            }
            addRange(std::move(fields), prefix);
        }
        scalars.clear();
    };
    auto token = tokens.next();
    while (token) {
        const auto keyword = token->quoted ? std::string{} : lower(token->value);
        if (keyword.starts_with("data_")) {
            if (result.hasMacromolecularAtoms) break;
            pending.clear();
            scalars.clear();
            authorLabels.clear();
        } else if (keyword == "loop_") {
            std::vector<std::string> columns;
            token = tokens.next();
            while (token && tag(*token)) {
                columns.push_back(lower(std::move(token->value)));
                token = tokens.next();
            }
            if (columns.empty()) continue;
            const auto index = [&](std::string_view name) {
                return static_cast<std::size_t>(std::find(columns.begin(), columns.end(), name) - columns.begin());
            };
            const auto labelChain = index("_atom_site.label_asym_id");
            const auto labelSeq = index("_atom_site.label_seq_id");
            const bool atoms = labelChain < columns.size() && labelSeq < columns.size() &&
                               index("_atom_site.cartn_x") < columns.size();
            const auto authorChain = index("_atom_site.auth_asym_id");
            const auto authorSeq = index("_atom_site.auth_seq_id");
            const auto insertion = index("_atom_site.pdbx_pdb_ins_code");
            const std::string rangeCategory = columns.front().starts_with("_struct_conf.") ? "_struct_conf." :
                columns.front().starts_with("_struct_sheet_range.") ? "_struct_sheet_range." : "";
            std::vector<std::string> row(columns.size());
            std::size_t column = 0;
            while (token && !control(*token)) {
                row[column++] = std::move(token->value);
                if (column == columns.size()) {
                    column = 0;
                    if (atoms) {
                        result.hasMacromolecularAtoms = true;
                        const auto value = [&](std::size_t i) -> std::string_view {
                            if (i >= row.size() || row[i] == "." || row[i] == "?") return {};
                            return row[i];
                        };
                        const auto from = author(value(authorChain), value(authorSeq), value(insertion));
                        const auto to = label(value(labelChain), value(labelSeq));
                        if (from && to) {
                            const auto [it, inserted] = authorLabels.emplace(*from, *to);
                            if (!inserted && it->second != to) it->second.reset(); // Ambiguous author ID.
                        }
                    } else if (!rangeCategory.empty()) {
                        Fields fields;
                        for (std::size_t i = 0; i < columns.size(); ++i) fields.emplace(columns[i], row[i]);
                        addRange(std::move(fields), rangeCategory);
                    }
                }
                token = tokens.next();
            }
            continue;
        } else if (tag(*token)) {
            token = tokens.next();
            if (!token || control(*token)) continue;
            if (keyword.starts_with("_struct_conf.") || keyword.starts_with("_struct_sheet_range.")) {
                scalars.emplace(keyword, std::move(token->value));
            }
        }
        token = tokens.next();
    }
    addScalars();

    for (const auto& range : pending) {
        const auto endpoint = [&](const std::string& end, std::string_view insertionTag) -> std::optional<Label> {
            const auto prefix = range.category + end;
            const auto get = [&](const std::string& suffix) { return field(range.fields, prefix + suffix); };
            if (auto value = label(get("_label_asym_id"), get("_label_seq_id"))) return value;
            const auto key = author(get("_auth_asym_id"), get("_auth_seq_id"), field(range.fields, insertionTag));
            const auto found = key ? authorLabels.find(*key) : authorLabels.end();
            return found == authorLabels.end() ? std::nullopt : found->second;
        };
        const auto first = endpoint("beg", range.category + "pdbx_beg_pdb_ins_code");
        const auto last = endpoint("end", range.category + "pdbx_end_pdb_ins_code");
        if (first && last && first->first == last->first && first->second <= last->second) {
            result.ranges.push_back({first->first, first->second, last->second, range.structure});
        }
    }
    return result;
}

void MmcifMetadata::annotate(std::vector<Atom>& atoms) const {
    if (ranges.empty()) return;
    for (auto& atom : atoms) {
        for (const auto& range : ranges) {
            if (atom.chainId == range.chain && atom.residueId >= range.first && atom.residueId <= range.last) {
                atom.secondaryStructure = range.structure;
                break;
            }
        }
    }
}

} // namespace jlmol
