#include "game/opcode_table.hpp"
#include "game/json_table_scan.hpp"
#include "core/logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <unordered_set>

namespace wowee {
namespace game {

// Global active opcode table pointer
static const OpcodeTable* g_activeOpcodeTable = nullptr;

void setActiveOpcodeTable(const OpcodeTable* table) { g_activeOpcodeTable = table; }
const OpcodeTable* getActiveOpcodeTable() { return g_activeOpcodeTable; }

// Name ↔ LogicalOpcode mapping table (generated from the enum)
struct OpcodeNameEntry {
    const char* name;
    LogicalOpcode op;
};

// Expansion/core naming aliases -> canonical LogicalOpcode names used by implementation.
struct OpcodeAliasEntry {
    const char* alias;
    const char* canonical;
};

// clang-format off
static const OpcodeAliasEntry kOpcodeAliases[] = {
#include "game/opcode_aliases_generated.inc"
};

static const OpcodeNameEntry kOpcodeNames[] = {
#include "game/opcode_names_generated.inc"
};
// clang-format on


static std::string_view canonicalOpcodeName(std::string_view name) {
    for (auto entry : kOpcodeAliases) {
        if (name == entry.alias) return entry.canonical;
    }
    return name;
}

static std::optional<uint16_t> resolveLogicalOpcodeIndex(std::string_view name) {
    const std::string_view canonical = canonicalOpcodeName(name);
    for (auto entry : kOpcodeNames) {
        if (canonical == entry.name) {
            return static_cast<uint16_t>(entry.op);
        }
    }
    return std::nullopt;
}

static std::optional<std::string> parseStringField(const std::string& json, const char* fieldName) {
    const std::string needle = std::string("\"") + fieldName + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;

    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::nullopt;

    size_t valueStart = json.find('"', colon + 1);
    if (valueStart == std::string::npos) return std::nullopt;
    size_t valueEnd = json.find('"', valueStart + 1);
    if (valueEnd == std::string::npos) return std::nullopt;
    return json.substr(valueStart + 1, valueEnd - valueStart - 1);
}

static std::vector<std::string> parseStringArrayField(const std::string& json, const char* fieldName) {
    std::vector<std::string> values;
    const std::string needle = std::string("\"") + fieldName + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return values;

    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return values;

    size_t arrayStart = json.find('[', colon + 1);
    if (arrayStart == std::string::npos) return values;
    size_t arrayEnd = json.find(']', arrayStart + 1);
    if (arrayEnd == std::string::npos) return values;

    size_t pos = arrayStart + 1;
    while (pos < arrayEnd) {
        size_t valueStart = json.find('"', pos);
        if (valueStart == std::string::npos || valueStart >= arrayEnd) break;
        size_t valueEnd = json.find('"', valueStart + 1);
        if (valueEnd == std::string::npos || valueEnd > arrayEnd) break;
        values.push_back(json.substr(valueStart + 1, valueEnd - valueStart - 1));
        pos = valueEnd + 1;
    }
    return values;
}

static bool loadOpcodeJsonRecursive(const std::filesystem::path& path,
                                    const OpcodeTable::JsonResolver& resolver,
                                    std::unordered_map<uint16_t, uint16_t>& logicalToWire,
                                    std::unordered_map<uint16_t, uint16_t>& wireToLogical,
                                    std::unordered_set<std::string>& loadingStack);

/// The body of one opcodes.json, already read. @p sourcePath is where it came
/// from, and is what an "_extends" inside it is resolved against - on disk,
/// or through @p resolver when there is no disk copy to walk.
static bool parseOpcodeJson(const std::string& json,
                            const std::filesystem::path& sourcePath,
                            const OpcodeTable::JsonResolver& resolver,
                            std::unordered_map<uint16_t, uint16_t>& logicalToWire,
                            std::unordered_map<uint16_t, uint16_t>& wireToLogical,
                            std::unordered_set<std::string>& loadingStack) {
    bool ok = true;

    if (auto extends = parseStringField(json, "_extends")) {
        ok = loadOpcodeJsonRecursive(sourcePath.parent_path() / *extends, resolver,
                                     logicalToWire, wireToLogical, loadingStack) && ok;
    }

    for (const std::string& removeName : parseStringArrayField(json, "_remove")) {
        auto logical = resolveLogicalOpcodeIndex(removeName);
        if (!logical) continue;
        auto it = logicalToWire.find(*logical);
        if (it != logicalToWire.end()) {
            const uint16_t oldWire = it->second;
            logicalToWire.erase(it);
            auto wireIt = wireToLogical.find(oldWire);
            if (wireIt != wireToLogical.end() && wireIt->second == *logical) {
                wireToLogical.erase(wireIt);
            }
        }
    }

    forEachJsonKeyValue(json, [&](const std::string& key, const std::string& valStr) {
        uint32_t parsed = 0;
        // A value that is not wholly a number is skipped rather than stored:
        // the "_extends" and "_remove" entries handled above land here too.
        if (!parseTableNumber(valStr, parsed)) return;
        const uint16_t wire = static_cast<uint16_t>(parsed);

        auto logical = resolveLogicalOpcodeIndex(key);
        if (logical) {
            auto oldLogicalIt = logicalToWire.find(*logical);
            if (oldLogicalIt != logicalToWire.end()) {
                const uint16_t oldWire = oldLogicalIt->second;
                auto oldWireIt = wireToLogical.find(oldWire);
                if (oldWireIt != wireToLogical.end() && oldWireIt->second == *logical) {
                    wireToLogical.erase(oldWireIt);
                }
            }
            auto oldWireIt = wireToLogical.find(wire);
            if (oldWireIt != wireToLogical.end() && oldWireIt->second != *logical) {
                logicalToWire.erase(oldWireIt->second);
                wireToLogical.erase(oldWireIt);
            }
            logicalToWire[*logical] = wire;
            wireToLogical[wire] = *logical;
        }
    });

    return ok;
}

static bool loadOpcodeJsonRecursive(const std::filesystem::path& path,
                                    const OpcodeTable::JsonResolver& resolver,
                                    std::unordered_map<uint16_t, uint16_t>& logicalToWire,
                                    std::unordered_map<uint16_t, uint16_t>& wireToLogical,
                                    std::unordered_set<std::string>& loadingStack) {
    // weakly_canonical applies any ".." the _extends chain introduced, and
    // does so without the file having to exist - which it does not, in the
    // build that carries the profiles inside it.
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path);
    const std::string canonicalKey = canonicalPath.string();
    if (!loadingStack.insert(canonicalKey).second) {
        LOG_WARNING("OpcodeTable: inheritance cycle at ", canonicalKey);
        return false;
    }

    // Disk first, embedded second. A profile edited under Data/ is what a
    // developer means by editing it; the resolver is what a wowee.exe dropped
    // beside the original game executable, with no Data/ anywhere, runs on.
    std::string json;
    std::ifstream f(canonicalPath);
    if (f.is_open()) {
        json.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    } else if (!resolver || !resolver(canonicalKey, json)) {
        LOG_WARNING("OpcodeTable: cannot open ", canonicalKey,
                    resolver ? " and no embedded copy of it either" : "");
        loadingStack.erase(canonicalKey);
        return false;
    }

    const bool ok = parseOpcodeJson(json, canonicalPath, resolver,
                                    logicalToWire, wireToLogical, loadingStack);
    loadingStack.erase(canonicalKey);
    return ok;
}

const char* OpcodeTable::logicalToName(LogicalOpcode op) {
    uint16_t val = static_cast<uint16_t>(op);
    for (auto entry : kOpcodeNames) {
        if (static_cast<uint16_t>(entry.op) == val) return entry.name;
    }
    return "UNKNOWN";
}

bool OpcodeTable::loadFromJson(const std::string& path, const JsonResolver& resolver) {
    // Resolved JSON inheritance is the single source of truth for opcode mappings.
    // Load into a scratch map (the recursive loader supports add/remove via
    // _extends/_remove), then bake it into the flat vector for fast toWire().
    // Load into temporaries so a failed reload doesn't wipe the working table.
    std::unordered_map<uint16_t, uint16_t> scratch;
    std::unordered_map<uint16_t, uint16_t> newWireToLogical;
    std::unordered_set<std::string> loadingStack;
    if (!loadOpcodeJsonRecursive(std::filesystem::path(path), resolver,
                                 scratch, newWireToLogical, loadingStack) ||
        scratch.empty()) {
        LOG_WARNING("OpcodeTable: no opcodes loaded from ", path);
        return false;
    }

    return bake(scratch, newWireToLogical, path);
}

bool OpcodeTable::loadFromMemory(const std::string& json, const std::string& sourceName,
                                 const JsonResolver& resolver) {
    std::unordered_map<uint16_t, uint16_t> scratch;
    std::unordered_map<uint16_t, uint16_t> newWireToLogical;
    std::unordered_set<std::string> loadingStack;

    // Named in the cycle set before parsing, so a file that inherits from
    // itself is caught here too and not only down the recursive path.
    const std::filesystem::path source = std::filesystem::weakly_canonical(sourceName);
    loadingStack.insert(source.string());

    if (!parseOpcodeJson(json, source, resolver, scratch, newWireToLogical, loadingStack) ||
        scratch.empty()) {
        LOG_WARNING("OpcodeTable: no opcodes loaded from ", sourceName);
        return false;
    }

    return bake(scratch, newWireToLogical, sourceName);
}

bool OpcodeTable::bake(std::unordered_map<uint16_t, uint16_t>& scratch,
                       std::unordered_map<uint16_t, uint16_t>& newWireToLogical,
                       const std::string& sourceName) {
    // Bake into the flat lookup table. Sized to cover the highest logical id we saw;
    // unmapped slots stay 0xFFFF (the same sentinel toWire used to return on miss).
    uint16_t maxIdx = 0;
    for (const auto& [logical, _wire] : scratch) {
        if (logical > maxIdx) maxIdx = logical;
    }
    std::vector<uint16_t> newLogicalToWire(static_cast<size_t>(maxIdx) + 1, 0xFFFF);
    for (const auto& [logical, wire] : scratch) {
        newLogicalToWire[logical] = wire;
    }

    logicalToWire_ = std::move(newLogicalToWire);
    wireToLogical_ = std::move(newWireToLogical);
    logicalToWireSize_ = scratch.size();

    LOG_INFO("OpcodeTable: loaded ", logicalToWireSize_, " opcodes from ", sourceName);
    return true;
}

uint16_t OpcodeTable::toWire(LogicalOpcode op) const {
    const size_t idx = static_cast<size_t>(op);
    return (idx < logicalToWire_.size()) ? logicalToWire_[idx] : 0xFFFF;
}

std::optional<LogicalOpcode> OpcodeTable::fromWire(uint16_t wireValue) const {
    auto it = wireToLogical_.find(wireValue);
    if (it != wireToLogical_.end()) {
        return static_cast<LogicalOpcode>(it->second);
    }
    return std::nullopt;
}

bool OpcodeTable::hasOpcode(LogicalOpcode op) const {
    return toWire(op) != 0xFFFF;
}

} // namespace game
} // namespace wowee
