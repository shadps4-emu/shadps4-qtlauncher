// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <fstream>
#include <memory>
#include "log_analyzer.h"
#include "logging/log.h"

namespace LogAnalyzer {

std::vector<std::unique_ptr<Entry>> entries;

inline std::string trim(std::string const& str) {
    auto start = std::ranges::find_if_not(str.begin(), str.end(),
                                          [](unsigned char c) { return std::isspace(c); });
    auto end = std::ranges::find_if_not(str.rbegin(), str.rend(), [](unsigned char c) {
                   return std::isspace(c);
               }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

void LoadSuiteFromInput(std::istream& data) {
    entry_id_counter = 1;
    std::string line;

    while (std::getline(data, line)) {
        std::string type, pattern, description = "";
        std::optional<string> label = std::nullopt, match_var_name = std::nullopt;
        std::optional<bool> multiple = std::nullopt, silent_pass = std::nullopt,
                            requirement = std::nullopt;
        if (line.empty() || line[0] != '#')
            continue;

        std::istringstream iss(line.substr(1));
        std::getline(iss, type, ',');
        std::string params;
        while (std::getline(iss, params, ',')) {
            std::string p = trim(params);
            if (p == "multiple")
                multiple = true;
            else if (p == "singular")
                multiple = false;
            else if (p == "silentpass")
                silent_pass = true;
            else if (p == "alwaysprint")
                silent_pass = false;
            else if (p == "requirement")
                requirement = true;
            else if (p == "optional")
                requirement = false;
        }
        if (*type.rbegin() == ',')
            type = type.substr(0, type.size() - 1);

        if (!std::getline(data, line))
            break;

        if (line[0] == '-') {
            description = line.substr(1);
            if (!std::getline(data, line))
                break;
        }
        if (line[0] == ';') {
            label = line.substr(1);
            if (!std::getline(data, line))
                break;
        }
        if (line[0] == ':') {
            match_var_name = line.substr(1);
            if (!std::getline(data, line))
                break;
        }
        pattern = line;

        if (type == "MatchValueEntry") {
            std::vector<std::string> accepted_values;
            if (std::getline(data, line)) {
                std::istringstream values(line);
                std::string value;
                while (std::getline(values, value, ',')) {
                    if (!std::all_of(value.begin(), value.end(), isspace))
                        accepted_values.push_back(value);
                }
            }
            entries.push_back(std::make_unique<MatchValueEntry>(
                pattern, description, label, match_var_name, accepted_values, multiple, requirement,
                silent_pass));
        } else if (type == "ShouldExistEntry") {
            entries.push_back(std::make_unique<ShouldExistEntry>(
                pattern, description, label, match_var_name, multiple, requirement, silent_pass));
        } else if (type == "ShouldntExistEntry") {
            entries.push_back(std::make_unique<ShouldntExistEntry>(
                pattern, description, label, match_var_name, multiple, requirement, silent_pass));
        } else {
            entries.push_back(std::make_unique<Entry>(pattern, description, label, match_var_name,
                                                      multiple, requirement, silent_pass));
        }
    }
}

void ResetEntries() {
    for (auto& e : entries) {
        e->Reset();
    }
}

bool DetectLogTypeAndSetupEntries(std::filesystem::path const& path) {
    entries.clear();
    enum LogType {
        Release,
        Nightly,
        Old,
    };
    LogType type;
    std::ifstream log(path);
    if (!log.is_open()) {
        return false;
    }
    bool is_valid = true;
    std::string first_line;
    std::getline(log, first_line);
    first_line = trim(first_line);
    Entry version_test =
        Entry("[Loader] <Info> emulator.cpp:# Run: Starting shadps4 emulator +", "", "");
    version_test.ProcessLine(first_line);
    if (version_test.occurrence_count == 1) {
        type = version_test.parsed_data[0].contains("WIP") ? Nightly : Release;
    } else {
        version_test = Entry("[Loader] <Info> emulator.cpp:# Starting shadps4 emulator +", "", "");
        version_test.ProcessLine(first_line);
        if (version_test.occurrence_count != 1) {
            // likely not a shadPS4 log file at all, or log filters were used
            is_valid = false;
        }
        type = Old;
    }
    if (!is_valid) {
        return false;
    }

    std::istringstream ds(is_valid_report_suite);
    LoadSuiteFromInput(ds);
    return true;
}

bool ProcessFile(std::filesystem::path const& path) {
    bool is_valid_file = DetectLogTypeAndSetupEntries(path);
    std::ifstream log(path);
    std::string linebuf;
    if (log.is_open()) {
        while (std::getline(log, linebuf)) {
            linebuf = trim(linebuf);
            for (auto& entry : entries) {
                entry->ProcessLine(linebuf);
            }
        }
        log.close();
        return true;
    } else {
        return false;
    }
}

optional<string> CheckResults(std::string const& game_id) {
    if (!(entries[0]->GetParsedData().has_value() && *entries[0]->GetParsedData() == game_id)) {
        return entries[0]->description;
    }
    if (!(entries[1]->GetParsedData().has_value() &&
          !entries[1]->GetParsedData()->contains("WIP"))) {
        return entries[1]->description;
    }

    for (int i = 2; i < entries.size(); ++i) {
        if (!entries[i]->Passed()) {
            return entries[i]->description;
        }
    }
    return nullopt;
}

} // namespace LogAnalyzer