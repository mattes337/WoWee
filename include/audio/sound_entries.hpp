#pragma once

/// The sound names the interface uses, and the files behind them.
///
/// WoW's interface almost never names a sound file. It names a row in
/// SoundEntries.dbc - PlaySound("igQuestFailed"), PlayGlueMusic("GS_LichKing"),
/// PlayGlueAmbience("GlueScreenIntro") - and the row names a directory and up
/// to ten files in it. So anything that answers one of those calls has to read
/// that table first, and there is exactly one right reading of it.
///
/// Header-only and free-standing because the two callers are built at
/// different moments from different asset pointers: the UI sound bank exists
/// only when the audio device came up, and the glue screens ask for their
/// music whether it did or not - a client with no sound card still has to be
/// able to say which track it would have played.

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/logger.hpp"
#include "pipeline/asset_manager.hpp"

namespace wowee::audio {

/// SoundEntries.dbc, read once and asked by name.
class SoundEntryTable {
public:
    /// The files row `name` lists, in the order it lists them, or an empty
    /// list when the table has no such row - or no table at all.
    ///
    /// Case-insensitive, because the interface is: GlueParent.lua upper-cases
    /// a race name to key its own tables and hands the result straight on.
    [[nodiscard]] const std::vector<std::string>& files(pipeline::AssetManager* assets,
                                                        const std::string& name) {
        static const std::vector<std::string> none;
        if (name.empty()) return none;
        ensureLoaded(assets);
        const auto it = byName_.find(upper(name));
        return it == byName_.end() ? none : it->second;
    }

    /// How many rows were read. Zero after a build that found no table, which
    /// is also the state a build is not attempted twice from.
    [[nodiscard]] size_t size() const { return byName_.size(); }

private:
    static std::string upper(const std::string& in) {
        std::string out = in;
        for (char& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return out;
    }

    void ensureLoaded(pipeline::AssetManager* assets) {
        if (built_) return;
        built_ = true;              // once, whether or not it works
        if (!assets) return;

        auto dbc = assets->loadDBC("SoundEntries.dbc");
        if (!dbc || !dbc->isLoaded()) {
            LOG_WARNING("SoundEntries.dbc is not available; sounds named by row "
                        "cannot be resolved");
            return;
        }
        // 3.3.5a layout: 0 ID, 1 SoundType, 2 Name, 3..12 File[0..9],
        // 13..22 Freq[0..9], 23 DirectoryBase.
        if (dbc->getFieldCount() < 24) {
            LOG_WARNING("SoundEntries.dbc has ", dbc->getFieldCount(),
                        " fields, expected at least 24");
            return;
        }
        for (uint32_t row = 0; row < dbc->getRecordCount(); ++row) {
            const std::string name = dbc->getString(row, 2);
            if (name.empty()) continue;
            const std::string dir = dbc->getString(row, 23);
            std::vector<std::string> paths;
            for (uint32_t f = 3; f <= 12; ++f) {
                const std::string file = dbc->getString(row, f);
                if (file.empty()) continue;
                paths.push_back(dir.empty() ? file : dir + "\\" + file);
            }
            // The first row wins. Names repeat in this table and the later rows
            // are variants; a caller asking by name wants one sound, not a
            // different one each time.
            if (!paths.empty()) byName_.emplace(upper(name), std::move(paths));
        }
        LOG_INFO("SoundEntries.dbc: ", byName_.size(), " sound names");
    }

    bool built_ = false;
    std::unordered_map<std::string, std::vector<std::string>> byName_;
};

}  // namespace wowee::audio
