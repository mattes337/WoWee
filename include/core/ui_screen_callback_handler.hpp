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
    ///
    /// Also what GlueDialog's StatusDialogClick reaches: every one of its
    /// progress and failure types calls that from its button, and for a login
    /// in progress abandoning it is precisely what the button says. With
    /// nothing in progress the same call is inert - the handler is already
    /// disconnected and the client is already on the login screen - and still
    /// closes the dialog, which is the other half of what the button is for.
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
    /// What the login is doing, said as the status dialog GlueDialog.lua
    /// listens for.
    ///
    /// GlueDialog_OnLoad registers OPEN_STATUS_DIALOG, UPDATE_STATUS_DIALOG
    /// and CLOSE_STATUS_DIALOG and nothing else, and every screen of the
    /// original login flow that is not a form is one of those three: the
    /// "Connecting" that appears when the button is pressed, the
    /// "Authenticating" that replaces it in place, and the line that says why
    /// it stopped. OPEN_STATUS_DIALOG carries GlueDialog's own type name -
    /// "CANCEL" while something can still be abandoned, "OKAY" when it is
    /// over - and the text to put in it.
    ///
    /// Only for a login the original screen started. This client's own login
    /// screen says all of this itself, from the same handler state, and
    /// following that one too would say everything twice.
    void updateGlueStatusDialog();

    /// Put a line in the status dialog, opening it if it is not up.
    ///
    /// Deduplicated, because the caller runs every frame: the same line again
    /// is not said again, and a new line in a dialog that is already the right
    /// type goes through UPDATE_STATUS_DIALOG, which re-texts it where it
    /// stands. Re-opening it instead would re-run OnShow every frame.
    void showGlueStatus(const char* dialogType, const std::string& text);
    /// Take it away, if this put it up.
    void closeGlueStatus();

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

    /// Whether the status dialog is following that login. Set when one
    /// starts, cleared once the login has reached somewhere it cannot leave
    /// on its own - a realm list, a failure, a disconnect - so the dialog that
    /// says so stays up until the player dismisses it.
    bool glueStatusFollow_ = false;

    /// Why the last glue login failed, in the auth handler's own words.
    ///
    /// AuthHandler holds one failure callback and this client's own login
    /// screen owns it, so the reason is taken by installing ours for the
    /// duration of a glue login. The native screen re-installs its own at the
    /// start of every attempt it makes, and it reads the reason only for an
    /// attempt it started, so neither can be left reading the other's.
    std::string glueFailureReason_;

    /// What updateGlueScreens has already announced. The character count
    /// starts at a value no list can have, so the first list of any size -
    /// including an account with no characters on it - is news.
    static constexpr size_t kNoCharacterListYet = static_cast<size_t>(-1);
    GlueEventFn glueEvent_;
    size_t   announcedRealmCount_     = 0;
    size_t   announcedCharacterCount_ = kNoCharacterListYet;
    uint64_t announcedCharacterGuid_  = 0;
    std::string announcedGlueScreen_;
    /// The status dialog that is up, by GlueDialog type name, and the line in
    /// it. Empty type means none.
    std::string announcedStatusDialog_;
    std::string announcedStatusText_;
    /// The disconnect notice already passed on. AuthScreen counts them; this
    /// is the last count seen, so the one notice is announced once.
    uint64_t announcedDisconnectSerial_ = 0;
};

} // namespace core
} // namespace wowee
