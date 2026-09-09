#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wowee {

namespace ui { class UIManager; }
namespace game { class GameHandler; class ExpansionRegistry; }
namespace auth { class AuthHandler; }
namespace pipeline { class AssetManager; }

namespace core {

// Forward-declared in application.hpp
enum class AppState;

/// Handles authentication, realm selection, character selection/creation UI callbacks.
/// Owns pendingCreatedCharacterName_.
class UIScreenCallbackHandler {
public:
    using SetStateFn = std::function<void(AppState)>;

    UIScreenCallbackHandler(ui::UIManager& uiManager,
                            game::GameHandler& gameHandler,
                            auth::AuthHandler& authHandler,
                            game::ExpansionRegistry* expansionRegistry,
                            pipeline::AssetManager* assetManager,
                            SetStateFn setState);

    void setupCallbacks();

    /// Connect to a realm and go on to character selection, as picking one on
    /// the realm screen does.
    ///
    /// Public because the original realm list picks realms too, and its
    /// ChangeRealm has to arrive at the same place: the host override, the
    /// session-key fallback, the realm id and the build the realm reports are
    /// all decided here, and a second copy of that decision would drift from
    /// this one the first time any of it changed.
    void selectRealm(const std::string& realmName, const std::string& realmAddress);

    /// The realm that was picked, for GetServerName. False before one has
    /// been, which is every moment before the realm list is answered.
    [[nodiscard]] bool selectedRealmInfo(std::string& name, bool& isPvp,
                                         bool& isRp, bool& isDown) const;

    /// Log in on the original login screen's behalf.
    ///
    /// The server is the one this client's own login screen last used - the
    /// glue screen's DefaultServerLogin names only an account and a password,
    /// exactly as the original client's does, because the original takes the
    /// address from its realmlist file. False when no server has ever been
    /// used, when the account or password is empty, or when the connection is
    /// refused.
    [[nodiscard]] bool beginGlueLogin(const std::string& account,
                                      const std::string& password);

    /// Abandon one and go back to the login screen, as CancelLogin does.
    void cancelGlueLogin();

    using GlueEventFn = std::function<void(const std::string&,
                                           const std::vector<std::string>&)>;

    /// Where the original screens hear about the client. Unset unless they are
    /// the screens that were built, which is not the shipping default.
    void setGlueEventSink(GlueEventFn sink) { glueEvent_ = std::move(sink); }

    /// Tell them what has changed since the last frame.
    ///
    /// The original screens are event-driven where this client's own are not:
    /// AuthScreen and CharacterScreen re-read the handlers every frame and so
    /// never had to be told anything, and nothing on either handler announces
    /// a realm list or a character list arriving. This is that announcement,
    /// worked out by comparing what the handlers hold now against what was
    /// last said - without it the original realm list never opens and the
    /// original character list is never filled in.
    void updateGlueScreens();

    /// Say which screen the client is on, in GlueParent's own vocabulary.
    ///
    /// The other half of SetCurrentScreen. The client changes screens for
    /// reasons the interface did not ask for - a realm connected, a world
    /// connection dropped - and GlueParent hears about those through
    /// SET_GLUE_SCREEN. Announced only when it differs from the last one said,
    /// so a screen the interface itself moved to does not come straight back
    /// at it.
    void noteClientScreen(const std::string& glueScreenName);

private:
    ui::UIManager& uiManager_;
    game::GameHandler& gameHandler_;
    auth::AuthHandler& authHandler_;
    game::ExpansionRegistry* expansionRegistry_;
    pipeline::AssetManager* assetManager_;
    SetStateFn setState_;

    std::string pendingCreatedCharacterName_;  // Auto-select after character creation
    std::vector<uint8_t> authenticatedSessionKey_;

    /// The realm selectRealm was last given, kept as plain fields rather than
    /// an auth::Realm so this header does not need that type complete.
    std::string selectedRealmName_;
    bool haveSelectedRealm_ = false;
    bool selectedRealmPvp_  = false;
    bool selectedRealmRp_   = false;
    bool selectedRealmDown_ = false;

    /// A login the original screen started, so its success can move the client
    /// on. This client's own login screen does that from its render loop, off
    /// a flag it only sets for its own button; a login begun anywhere else
    /// authenticates and then sits there.
    bool glueLoginActive_ = false;

    /// What updateGlueScreens has already announced. The character count
    /// starts at a value no list can have, so the first list of any size -
    /// including an account with no characters on it - is news.
    static constexpr size_t kNoCharacterListYet = static_cast<size_t>(-1);
    GlueEventFn glueEvent_;
    size_t   announcedRealmCount_     = 0;
    size_t   announcedCharacterCount_ = kNoCharacterListYet;
    uint64_t announcedCharacterGuid_  = 0;
    std::string announcedGlueScreen_;
};

} // namespace core
} // namespace wowee
