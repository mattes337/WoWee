#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::addons {

struct TocFile {
    std::string addonName;
    std::string basePath;

    std::unordered_map<std::string, std::string> directives;
    std::vector<std::string> files;

    [[nodiscard]] std::string getTitle() const;
    [[nodiscard]] bool isLoadOnDemand() const;
    [[nodiscard]] std::vector<std::string> getSavedVariables() const;
    [[nodiscard]] std::vector<std::string> getSavedVariablesPerCharacter() const;
};

/// Read and parse a TOC from the filesystem.
std::optional<TocFile> parseTocFile(const std::string& tocPath);

/// Parse a TOC already in hand, named by @p tocPath.
///
/// The original interface's manifests live inside the game's archives, where
/// there is no file to open; the path is still what names the addon and what
/// its own files are resolved against.
std::optional<TocFile> parseTocText(const std::string& tocPath,
                                    const std::string& text);

} // namespace wowee::addons
