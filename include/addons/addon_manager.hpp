#pragma once

#include "addons/lua_engine.hpp"
#include "addons/toc_parser.hpp"
#include <memory>
#include <string_view>
#include <utility>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wowee::addons {

class AddonManager {
public:
    AddonManager();
    ~AddonManager();

    bool initialize(game::GameHandler* gameHandler, const LuaServices& services = {});
    /// Find the addons this session may load.
    ///
    /// @param addonsPath  wowee's own Interface/AddOns, under its data tree
    /// @param extraRoots  further directories to scan - the Interface/AddOns of
    ///        the installation wowee was dropped into, which is where a
    ///        player's own addons live and which nothing derived from wowee's
    ///        data tree reaches.
    ///
    /// The installation's archives are searched too, whenever the byte source
    /// is wired: an installation nobody extracted keeps every Blizzard panel
    /// inside them.
    void scanAddons(const std::string& addonsPath,
                    const std::vector<std::string>& extraRoots = {});
    void loadAllAddons();
    /// Parse an XML file, build what it declares, and follow its includes and
    /// scripts. depth guards against a file that includes itself.
    bool loadXmlFile(const std::string& path, int depth);
    /// Load the original interface from its own manifest, in the order it
    /// states. Opt-in through WOWEE_LOAD_FRAMEXML; see loadAllAddons.
    /// Register this client's own settings as a category in FrameXML's
    /// Interface Options, built from the schema. Runs after FrameXML, because
    /// InterfaceOptions_AddCategory is FrameXML's.
    void registerWoweeOptionsPanel();
    /// Move the coin amounts off the coins they are drawn against.
    void giveCoinAmountsClearance();

    bool loadFrameXml(const std::string& frameXmlDir);
    /// Where FrameXML lives, remembered at scan time so the loader can find it.
    void setFrameXmlDir(const std::string& dir) { frameXmlDir_ = dir; }

    /// Load the original login and character screens from GlueXML's own
    /// manifest, the way loadFrameXml loads the world interface.
    ///
    /// A separate manifest and a separate lifetime: the real client runs glue
    /// and the world interface in different Lua states, and so does this - the
    /// glue screens are torn down before the world's are built, because both
    /// define GameFontNormal, the options templates and a good deal else, and
    /// a state holding two copies is a state where which one answers is an
    /// accident of load order.
    bool loadGlueXml(const std::string& glueXmlDir);
    void setGlueXmlDir(const std::string& dir) { glueXmlDir_ = dir; }
    /// The loose Interface directory beside the original executable, whose
    /// files win over the archives exactly as they do in the original client.
    /// Empty where there is none.
    void setLooseInterfaceRoot(const std::string& dir) { looseInterfaceRoot_ = dir; }
    [[nodiscard]] const std::string& getGlueXmlDir() const { return glueXmlDir_; }
    /// Whether the glue screens are the ones currently built.
    [[nodiscard]] bool glueLoaded() const { return glueLoaded_; }

    /// Take the glue screens down, so the world's interface is built into a
    /// state that is not already holding them.
    ///
    /// The other half of the lifetime loadGlueXml documents, and the half that
    /// was never wired: glue was loaded once at startup and never torn down,
    /// so world entry ran FrameXML over a state that still held the login
    /// screens. Both define GameFontNormal and both create a frame called
    /// VideoOptionsFrame, and the second load is the one the globals end up
    /// naming - the glue widgets it displaced stay in the tree, shown, and
    /// draw over the world.
    ///
    /// A no-op returning true when glue is not up, so the world's loader can
    /// call it unconditionally and a session that never asked for the glue
    /// screens pays nothing.
    bool unloadGlue();

    /// Build the glue screens again, after the world's interface came down.
    ///
    /// A logout closes the Lua state, and with it every frame in it - the
    /// login screen this session started on included. Without this the client
    /// returns to a login screen that no longer exists: glueLoaded() would
    /// still be true, so the widget pass and the glue event route would keep
    /// running against an empty tree while this client's own screens stayed
    /// out of the way for an interface that is no longer there.
    ///
    /// Whether the glue screens are wanted at all was decided once, at
    /// startup; this asks that question of glueWanted() rather than of the
    /// environment, so the caller does not have to know how it was decided.
    /// False when they are not wanted, or when the rebuild did not produce
    /// them.
    bool restoreGlue();

    /// Whether the glue screens were ever built this session - which is how
    /// this asks "did the client ask for them", the decision being made once
    /// at startup and never revisited.
    [[nodiscard]] bool glueWanted() const { return glueWanted_; }

    bool runScript(const std::string& code);
    /// Run one line of interface Lua, for a keybinding whose window FrameXML
    /// now owns. Errors are logged rather than thrown: a bad line here should
    /// cost the keypress, not the frame.
    void runInterfaceCommand(const std::string& lua);
    /// The same, for a command whose answer decides what happens next.
    bool interfaceCommandBoolean(const std::string& expression);


    void fireEvent(const std::string& event, const std::vector<std::string>& args = {});
    void update(float deltaTime);
    void shutdown();

    [[nodiscard]] const std::vector<TocFile>& getAddons() const { return addons_; }
    /// The addons that wait to be asked for. Half the interface's own panels
    /// are here - the talent tree, the achievements, the macro editor - and
    /// each is loaded whole or not at all, so one of them raising during load
    /// costs its entire panel rather than degrading it.
    [[nodiscard]] const std::vector<TocFile>& getLoadOnDemandAddons() const { return lodAddons_; }

    /// Load a load-on-demand addon by name. Names are matched without regard to
    /// case, because the interface asks for "Blizzard_TalentUI" and the
    /// directory on a case-sensitive filesystem is blizzard_talentui.
    bool loadAddOnByName(const std::string& name, std::string& reason);
    [[nodiscard]] bool isAddOnLoadedByName(const std::string& name) const;
    LuaEngine* getLuaEngine() { return &luaEngine_; }
    [[nodiscard]] bool isInitialized() const { return luaEngine_.isInitialized(); }

    // Per-addon enable/disable (persisted). Disabled addons are skipped by
    // loadAllAddons; changes take effect on the next load (world enter or /reload).
    [[nodiscard]] bool isAddonEnabled(const std::string& addonName) const;
    void setAddonEnabled(const std::string& addonName, bool enabled);
    // True once any addon has been loaded this session (so the UI can note that a
    // toggle only applies after the next reload).
    [[nodiscard]] bool addonsLoaded() const { return addonsLoaded_; }

    void saveAllSavedVariables();
    void setCharacterName(const std::string& name) { characterName_ = name; }

    /// Take the interface down, leaving a live but empty Lua state.
    ///
    /// Whichever interface it is: closing the state disposes of the widget
    /// tree, so the glue screens go with it exactly as the world's frames do,
    /// and glueLoaded() is cleared here for that reason. It used to survive
    /// the state that held it, which left every caller asking "are the glue
    /// screens up" answered yes about frames that no longer existed.
    ///
    /// The interface belongs to the character that was playing: it is built by
    /// running FrameXML's files, and running them again over a state that
    /// already holds them builds every frame a second time - the widget tree
    /// appends rather than replaces, so both copies draw. That is what a
    /// logout followed by another login without quitting did. Whoever clears
    /// the "addons are loaded" flag has to call this, or the next world entry
    /// loads a second interface on top of the first.
    ///
    /// Saved variables are written on the way out, as they are on a reload.
    bool unloadAll();

    /// Re-initialize the Lua VM and build again whichever interface was up
    /// (used by /reload).
    ///
    /// Which one that is has to be read before the teardown, because the
    /// teardown is what forgets it. /reload on a login screen used to answer
    /// with the world's interface, on a screen that has no world behind it.
    bool reload();

private:
    LuaEngine luaEngine_;
    std::vector<TocFile> addons_;
    /// Declared LoadOnDemand: known, listed, and not run until asked for.
    std::vector<TocFile> lodAddons_;
    std::set<std::string> lodLoaded_;
    game::GameHandler* gameHandler_ = nullptr;
    LuaServices luaServices_;
    std::string addonsPath_;
    /// The directories scanAddons was given beside addonsPath_ - the
    /// installation's own Interface/AddOns and the one beside this executable.
    ///
    /// Kept because unloadAll scans again, and scanning again with only
    /// addonsPath_ is how the player's own addons disappeared from a session
    /// that had already loaded them once: they live in the extra roots and
    /// nothing else names them.
    std::vector<std::string> extraAddonRoots_;

    /// Every global the load-on-demand addons on disk define, read out of their
    /// own files. This is the source of truth for which names must answer as
    /// absent before their addon loads; the literal list in the Lua bootstrap
    /// is a floor for an install with no addons extracted.
    [[nodiscard]] std::vector<std::string> deferredAddonGlobals() const;

    /// An interface directory: on disk when it is there, in the archives
    /// otherwise. @p defaultVirtualDir is where the archives keep it.
    [[nodiscard]] std::string resolveInterfaceDir(
        const std::string& hint, const std::string& defaultVirtualDir) const;

    /// What one walk of a manifest did.
    struct ManifestRun {
        int lua = 0;
        int xml = 0;
        int failed = 0;
        long long milliseconds = 0;
        std::vector<std::pair<std::string, std::string>> failures;
    };
    /// Walk a manifest's files in the order it lists them, loading each by
    /// what it is. @p label names the manifest in the log.
    ManifestRun runManifestFiles(const std::string& dir, const TocFile& toc,
                                 const char* label);

    bool loadAddon(const TocFile& addon);

    /// The enabled addons in the order they may be loaded, dependencies first.
    /// @param skipped counts the ones refused - disabled, or wanting an addon
    ///        that is missing, disabled, cyclic or itself refused.
    [[nodiscard]] std::vector<const TocFile*> loadOrder(int& skipped) const;

    /// Read a file the interface named, from disk if it is there and from the
    /// installation's archives otherwise.
    ///
    /// One seam for both, because which of the two answers is not a decision
    /// any caller here should have to make: a development checkout has the
    /// interface extracted beside the executable and an untouched installation
    /// keeps it inside its archives, and the loader is the same either way.
    /// Disk wins, so an extracted tree or a working copy still shadows the
    /// archive it came from.
    [[nodiscard]] bool readUiFile(const std::string& path, std::string& out) const;
    [[nodiscard]] bool uiFileExists(const std::string& path) const;

    /// @p relative resolved against @p baseDir - on disk without regard to
    /// case, then as an archive path. Empty when neither has it.
    [[nodiscard]] std::string looseInterfacePath(const std::string& virtualPath) const;
    [[nodiscard]] std::string resolveUiPath(const std::string& baseDir,
                                            const std::string& relative) const;

    /// Run a Lua file named by a path that may be an archive path, under that
    /// same name so an error names the file the interface knows.
    bool runUiLuaFile(const std::string& path);
    /// Where saved variables are written: wowee's own config root, never the
    /// addon's folder and never the original client's WTF.
    static std::string savedVariablesDir();
    [[nodiscard]] std::string getSavedVariablesPath(const TocFile& addon) const;
    [[nodiscard]] std::string getSavedVariablesPerCharacterPath(const TocFile& addon) const;
    std::string characterName_;

    // addonName -> enabled. Absent means enabled (default on).
    std::unordered_map<std::string, bool> addonEnabled_;
    std::string frameXmlDir_;
    std::string glueXmlDir_;
    std::string looseInterfaceRoot_;
    bool glueLoaded_ = false;
    /// Whether the glue screens were ever built. Set by the first load that
    /// produced them and never cleared: the client decides once, at startup,
    /// whether this session has glue screens at all, and a logout rebuilding
    /// them must not have to ask that question a second time.
    bool glueWanted_ = false;
    /// The same directory as it is actually spelled on disk.
    ///
    /// The caller says ".../interface/FrameXML" and this install has
    /// ".../interface/framexml". loadFrameXml resolves that to open it, and
    /// the resolved spelling was thrown away with the local it was kept in -
    /// so every later use of the member was a path that does not exist, on any
    /// filesystem that cares about case.
    std::string frameXmlResolvedDir_;
    /// Why the last loadXmlFile returned false, so a caller loading many files
    /// can report the reasons together instead of leaving them scattered.
    std::string lastXmlError_;
    bool addonsLoaded_ = false;
    static std::string enabledStatePath();
    void loadEnabledState();
    void saveEnabledState() const;
};

} // namespace wowee::addons
