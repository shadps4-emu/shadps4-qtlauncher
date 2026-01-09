// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace LogAnalyzer {

using std::nullopt;
using std::optional;
using std::string;

static inline constexpr char const* const is_valid_report_suite = R"(
#Entry
-The game ID in the log does not match the game ID of the selected game.
[Loader] <Info> ^ emulator.cpp:# Run: Game id: @ Title: *
#Entry
-The emulator version wasn't an official release one.
[Loader] <Info> ^ emulator.cpp:# Run: Starting shadps4 emulator +

#MatchValueEntry
-The emulator version wasn't an official release one.
[Loader] <Info> ^ emulator.cpp:# Run: Remote https://github.com/@(/)*
shadps4-emu
#MatchValueEntry
-The log type was not snyc.
[Config] <Info> ^ emulator.cpp:# Run: General LogType: @
sync
#MatchValueEntry
-PS4 Neo mode was turned on.
[Config] <Info> ^ emulator.cpp:# Run: General isNeo: @
false

#ShouldntExistEntry
-isDevKit was turned on.
[Kernel.Vmm] <Warning> ^ memory.cpp:# SetupMemoryRegions: Config::isDevKitConsole is enabled! *
#ShouldntExistEntry
-System modules were missing.
[Loader] <Info> ^ emulator.cpp:# LoadSystemModules: No HLE available for @{ module}
#ShouldntExistEntry
-System modules were missing.
[Loader] <Info> ^ emulator.cpp:# LoadSystemModules: Can't Load @{ switching to HLE}
#ShouldntExistEntry
-Patches were used.
[Loader] <Info> ^ memory_patcher.cpp:# PatchMemory: Applied patch: *
)";

inline int entry_id_counter = 1;
class Entry {
public:
    string pattern, description, label;
    std::optional<string> variable_name;
    int occurrence_count;
    std::vector<string> parsed_data;
    bool is_multiple_occurrence, counts_to_total, silent_pass;
    int id;
    size_t minimum_line_length;

    Entry(string p, string d, optional<string> l = nullopt, optional<string> v = nullopt,
          optional<bool> m = nullopt, optional<bool> c = nullopt, optional<bool> s = nullopt)
        : pattern(p), description(d.size() != 0 ? d : p), label(l.value_or("")), variable_name(v),
          occurrence_count(0), is_multiple_occurrence(m.value_or(false)),
          counts_to_total(c.value_or(false)), silent_pass(s.value_or(true)), id(entry_id_counter++),
          minimum_line_length(
              p.size() -
              std::min(p.size() < 2
                           ? p.size()
                           : (size_t)std::ranges::count_if(
                                 std::views::iota(size_t{0}, std::max(p.size() - 2, size_t{0})),
                                 [&p](size_t i) { return p[i] == '@' && p[i + 1] == '('; }) *
                                 3,
                       p.size())) {}
    virtual ~Entry() = default;

    virtual void ProcessLine(std::string const& line) {
        if ((!is_multiple_occurrence && occurrence_count != 0) ||
            line.size() < minimum_line_length) {
            return;
        }
        auto line_it = line.begin();
        auto pattern_it = pattern.begin();
        std::stringstream in("");
        int chars_to_discard = 0;
        while (pattern_it != pattern.end()) {
            if (*pattern_it == '#') {
                while (line_it != line.end() && *line_it != ' ') {
                    line_it++;
                }
            } else if (*pattern_it == '@') {
                if (in.str().size() != 0) {
                    in << " ";
                }
                if (pattern_it + 1 != pattern.end() && *(pattern_it + 1) == '{') {
                    while (*pattern_it != '}') {
                        chars_to_discard++;
                        pattern_it++;
                    }
                    chars_to_discard -= 2;
                    while (line_it != line.end() && (*line_it != ' ' || chars_to_discard != 0)) {
                        in << *line_it++;
                    }
                } else if (pattern_it + 1 != pattern.end() && *(pattern_it + 1) == '(') {
                    char discard_symbol = *(pattern_it + 2);
                    while (line_it != line.end() && *line_it != discard_symbol) {
                        in << *line_it++;
                    }
                    pattern_it += 3;
                } else {
                    while (line_it != line.end() && (*line_it != ' ' || chars_to_discard != 0)) {
                        in << *line_it++;
                    }
                }
            } else if (*pattern_it == '*') {
                // break early as hitting this means that everything before was
                // correct and everything after we don't care about
                occurrence_count++;
                if (in.str().size() > 0)
                    parsed_data.push_back(in.str().substr(0, in.str().size() - chars_to_discard));
                return;
            } else if (*pattern_it == '+') {
                if (in.str().size() != 0) {
                    in << " ";
                }
                occurrence_count++;
                in << string(line_it, line.end());
                parsed_data.push_back(in.str().substr(0, in.str().size() - chars_to_discard));
                return;
            } else if (*pattern_it == '^') {
                if (*line_it == '(') { // thread logging present
                    while (line_it + 1 != line.end() && *line_it != ')') {
                        ++line_it;
                    }
                    ++line_it;
                } else { // skip space after
                    ++pattern_it;
                }
            } else {
                if (line_it == line.end() || *pattern_it != *line_it++) {
                    return;
                }
            }
            pattern_it++;
        }
        if (line_it == line.end()) {
            if (in.str().size() > 0) {
                if (chars_to_discard > 0) {
                    // check if the discarded part actually fits
                    auto rev_line_it = line.rbegin();
                    auto rev_pattern_it = pattern.rbegin() + 1;
                    while (*rev_pattern_it != '{') {
                        if (*rev_pattern_it++ != *rev_line_it++) {
                            return;
                        }
                    }
                }
                parsed_data.push_back(in.str().substr(0, in.str().size() - chars_to_discard));
            }
            occurrence_count++;
        }
    }
    virtual void Reset() {
        parsed_data.clear();
        occurrence_count = 0;
    }
    virtual bool Passed() {
        return true;
    }
    virtual std::optional<string> GetParsedData() {
        if (parsed_data.size() > 0) {
            return parsed_data[0];
        }
        return nullopt;
    }
};

class MatchValueEntry : public Entry {
public:
    std::vector<string> accepted_data;
    MatchValueEntry(string p, string d, optional<string> l, optional<string> v,
                    std::vector<string> a, optional<bool> m = nullopt, optional<bool> c = nullopt,
                    optional<bool> s = nullopt)
        : Entry(p, d, l, v, m, c.value_or(true), s.value_or(true)), accepted_data(std::move(a)) {}
    bool Passed() {
        return occurrence_count != 0 &&
               std::ranges::find(accepted_data.begin(), accepted_data.end(), parsed_data[0]) !=
                   accepted_data.end();
    }
    optional<string> GetFirstAcceptedValue() {
        if (accepted_data.size() == 0) {
            return nullopt;
        }
        return accepted_data[0];
    }
};

class ShouldExistEntry : public Entry {
public:
    ShouldExistEntry(string p, string d, optional<string> l = nullopt, optional<string> v = nullopt,
                     optional<bool> m = nullopt, optional<bool> c = nullopt,
                     optional<bool> s = nullopt)
        : Entry(p, d, l, v, m, c.value_or(true), s.value_or(true)) {}
    bool Passed() {
        return occurrence_count != 0;
    }
};
class ShouldntExistEntry : public Entry {
public:
    ShouldntExistEntry(string p, string d, optional<string> l = nullopt,
                       optional<string> v = nullopt, optional<bool> m = nullopt,
                       optional<bool> c = nullopt, optional<bool> s = nullopt)
        : Entry(p, d, l, v, m, c.value_or(true), s.value_or(true)) {}
    bool Passed() {
        return occurrence_count == 0;
    }
};

extern std::vector<std::unique_ptr<Entry>> entries;

bool ProcessFile(std::filesystem::path const& path);
optional<string> CheckResults(std::string const& game_id);

} // namespace LogAnalyzer
