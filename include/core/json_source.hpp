#pragma once

#include "core/logger.hpp"

#include <fstream>
#include <functional>
#include <iterator>
#include <string>

namespace wowee {
namespace core {

/// Where a JSON file the client needs is read from: disk first, then whatever
/// the caller can resolve it out of.
///
/// Three tables load this way - the opcode table, the update-field table and
/// the DBC layouts - and each is one of the four expansion profiles that are
/// now built into the binary. The order is the same one the shaders and the
/// asset manager use, and for the same reason: a profile edited under Data/ is
/// what a developer means by editing it, and the resolver is what a wowee.exe
/// dropped beside the original game executable, with no Data/ anywhere at all,
/// runs on.
///
/// Written once because it was written twice. The update-field and DBC-layout
/// loaders were the same ten lines differing only in the name they logged, and
/// the tree's own duplicate-function sweep said so.
///
/// @param label what to call the caller in the log, e.g. "DBCLayout"
/// @return whether @p out holds the file's text
[[nodiscard]] inline bool readJsonDiskThenResolver(
    const std::string& path,
    const std::function<bool(const std::string&, std::string&)>& resolver,
    const char* label,
    std::string& out) {
    std::ifstream f(path);
    if (f.is_open()) {
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    if (resolver && resolver(path, out)) return true;
    // Which of the two was missing matters to whoever reads this: a path that
    // is simply absent is a different fault from one absent in a build that
    // was supposed to carry a copy of it.
    LOG_WARNING(label, ": cannot open ", path,
                resolver ? " and no embedded copy of it either" : "");
    return false;
}

}  // namespace core
}  // namespace wowee
