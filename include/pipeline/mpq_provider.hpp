#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

/// Read-only access to a game installation's own MPQ archives.
///
/// The archives are opened where they lie and never written to, so an
/// installation is left exactly as it was found - including one on a
/// read-only volume.
///
/// Lookups are by known path, not by listfile: a name is hashed into each
/// archive's table, so an archive whose (listfile) is absent or incomplete
/// still answers for every file wowee asks for by name.
///
/// Archives are given lowest priority first, the order the game itself loads
/// them in, and searched highest priority first so a patch archive wins over
/// the base it patches. A file a patch marks deleted stops the search there
/// and reads as absent, which is what the marker means.
class MpqProvider {
public:
    MpqProvider();
    ~MpqProvider();

    MpqProvider(const MpqProvider&) = delete;
    MpqProvider& operator=(const MpqProvider&) = delete;

    /// Whether this build can read archives at all. False when built without
    /// StormLib, in which case open() always fails.
    [[nodiscard]] static bool isSupported();

    /// Open @p archives, lowest priority first. Archives that fail to open are
    /// reported and skipped; the rest still serve. Replaces any open set.
    /// @return whether at least one archive is open.
    bool open(const std::vector<std::string>& archives);

    void close();

    [[nodiscard]] bool isOpen() const { return !archives_.empty(); }
    [[nodiscard]] size_t archiveCount() const { return archives_.size(); }

    /// @param path a normalized WoW path (lowercase, backslash-separated).
    [[nodiscard]] bool exists(const std::string& path) const;

    /// File contents, or empty when the file is absent, deleted by a patch, or
    /// unreadable.
    [[nodiscard]] std::vector<uint8_t> read(const std::string& path) const;

    /// The archive a lookup is answered from, for diagnostics. Empty when the
    /// file is not there.
    [[nodiscard]] std::string sourceOf(const std::string& path) const;

private:
    struct Archive {
        std::string path;
        void* handle = nullptr;
        /// StormLib serializes nothing of its own, and several threads stream
        /// assets through here at once.
        mutable std::mutex mutex;
    };

    /// Highest priority first - the reverse of what open() is given.
    std::vector<std::unique_ptr<Archive>> archives_;

    /// Index of the archive answering for @p path, or -1. Skips an archive
    /// that marks the file deleted by returning -1 at that point.
    [[nodiscard]] int findArchive(const std::string& path) const;
};

}  // namespace pipeline
}  // namespace wowee
