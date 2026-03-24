// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "common/types.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class KeyManager {
public:
    // ------------------- Nested keysets -------------------
    struct TrophyKeySet {
        std::vector<u8> ReleaseTrophyKey;
    };

    struct FakeKeyset {
        std::vector<u8> Exponent1, Exponent2, PublicExponent, Coefficient, Modulus, Prime1, Prime2,
            PrivateExponent;
    };

    struct DebugRifKeyset {
        std::vector<u8> Exponent1, Exponent2, PublicExponent, Coefficient, Modulus, Prime1, Prime2,
            PrivateExponent;
    };

    struct PkgDerivedKey3Keyset {
        std::vector<u8> Exponent1, Exponent2, PublicExponent, Coefficient, Modulus, Prime1, Prime2,
            PrivateExponent;
    };

    struct AllKeys {
        KeyManager::TrophyKeySet TrophyKeySet;
        KeyManager::FakeKeyset FakeKeyset;
        KeyManager::DebugRifKeyset DebugRifKeyset;
        KeyManager::PkgDerivedKey3Keyset PkgDerivedKey3Keyset;
    };

    // ------------------- Construction -------------------
    KeyManager();
    ~KeyManager();

    // ------------------- Singleton -------------------
    static std::shared_ptr<KeyManager> GetInstance();
    static void SetInstance(std::shared_ptr<KeyManager> instance);

    // ------------------- File operations -------------------
    bool LoadFromFile();
    bool SaveToFile();

    // ------------------- Key operations -------------------
    void SetDefaultKeys();
    bool HasKeys() const;

    // ------------------- Getters / Setters -------------------
    const AllKeys& GetAllKeys() const {
        return m_keys;
    }
    void SetAllKeys(const AllKeys& keys) {
        m_keys = keys;
    }
    bool IsFakeKeysetValid() const {
        const auto& FakeKeyset = GetAllKeys().FakeKeyset;
        if (FakeKeyset.Prime1.empty() || FakeKeyset.Prime2.empty() || FakeKeyset.Modulus.empty() ||
            FakeKeyset.PrivateExponent.empty() || FakeKeyset.PublicExponent.empty()) {
            return false;
        }
        return true;
    }
    bool isPkgDerivedKey3KeysetValid() const {
        const auto& PkgDerivedKey3Keyset = GetAllKeys().PkgDerivedKey3Keyset;
        if (PkgDerivedKey3Keyset.Prime1.empty() || PkgDerivedKey3Keyset.Prime2.empty() ||
            PkgDerivedKey3Keyset.Modulus.empty() || PkgDerivedKey3Keyset.PrivateExponent.empty() ||
            PkgDerivedKey3Keyset.PublicExponent.empty()) {
            return false;
        }
        return true;
    }

    static std::vector<u8> HexStringToBytes(const std::string& hexStr);
    static std::string BytesToHexString(const std::vector<u8>& bytes);

private:
    void KeysToJson(json& j) const;
    void JsonToKeys(const json& j);

    AllKeys m_keys{};

    static std::shared_ptr<KeyManager> s_instance;
    static std::mutex s_mutex;
};

// ------------------- NLOHMANN macros -------------------
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(KeyManager::TrophyKeySet, ReleaseTrophyKey)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(KeyManager::FakeKeyset, Exponent1, Exponent2, PublicExponent,
                                   Coefficient, Modulus, Prime1, Prime2, PrivateExponent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(KeyManager::DebugRifKeyset, Exponent1, Exponent2, PublicExponent,
                                   Coefficient, Modulus, Prime1, Prime2, PrivateExponent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(KeyManager::PkgDerivedKey3Keyset, Exponent1, Exponent2,
                                   PublicExponent, Coefficient, Modulus, Prime1, Prime2,
                                   PrivateExponent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(KeyManager::AllKeys, TrophyKeySet, FakeKeyset, DebugRifKeyset,
                                   PkgDerivedKey3Keyset)

namespace nlohmann {
template <>
struct adl_serializer<std::vector<u8>> {
    static void to_json(json& j, const std::vector<u8>& vec) {
        j = KeyManager::BytesToHexString(vec);
    }
    static void from_json(const json& j, std::vector<u8>& vec) {
        vec = KeyManager::HexStringToBytes(j.get<std::string>());
    }
};
} // namespace nlohmann
