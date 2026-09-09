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

    /// Addons this one cannot run without. An addon whose dependency is
    /// missing or disabled is not loaded at all, and one whose dependency is
    /// present is loaded after it - which is the whole reason the order
    /// matters, since a dependency exists to define what the dependant reads
    /// at file scope.
    ///
    /// Spelled "Dependencies" or "RequiredDeps"; both are taken.
    [[nodiscard]] std::vector<std::string> getDependencies() const;

    /// Addons this one uses when they are there. They order the load the same
    /// way and their absence is not an error.
    ///
    /// Spelled "OptionalDeps" or "OptionalDependencies".
    [[nodiscard]] std::vector<std::string> getOptionalDependencies() const;

    /// The interface version the addon states, or 0 when it states none.
    /// 11200 is 1.12.0, 20400 is 2.4.3, 30300 is 3.3.5a.
    [[nodiscard]] int getInterfaceVersion() const;
};

/// Parse a TOC already in hand, named by @p tocPath.
///
/// Bytes rather than a path, because a manifest inside the game's archives has
/// no file to open - and reading it is the caller's business either way, since
/// only the caller knows whether this installation keeps its interface on disk
/// or in an archive. The path is still what names the addon and what its own
/// files are resolved against.
std::optional<TocFile> parseTocText(const std::string& tocPath,
                                    const std::string& text);

} // namespace wowee::addons
