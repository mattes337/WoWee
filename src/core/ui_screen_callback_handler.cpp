#include "core/ui_screen_callback_handler.hpp"
#include "core/application.hpp"  // AppState
#include "core/config_paths.hpp"
#include "core/logger.hpp"
#include "ui/ui_manager.hpp"
#include "auth/auth_handler.hpp"
#include "game/game_handler.hpp"
#include "game/expansion_profile.hpp"
#include "game/world_packets.hpp"
#include "pipeline/asset_manager.hpp"
#include <cstdlib>
#include <fstream>
#include <string>

namespace wowee { namespace core {

UIScreenCallbackHandler::UIScreenCallbackHandler(
    ui::UIManager& uiManager,
    game::GameHandler& gameHandler,
    auth::AuthHandler& authHandler,
    game::ExpansionRegistry* expansionRegistry,
    pipeline::AssetManager* assetManager,
    SetStateFn setState)
    : uiManager_(uiManager)
    , gameHandler_(gameHandler)
    , authHandler_(authHandler)
    , expansionRegistry_(expansionRegistry)
    , assetManager_(assetManager)
    , setState_(std::move(setState))
{
}

void UIScreenCallbackHandler::setupCallbacks() {
    authHandler_.setOnSuccess([this](const std::vector<uint8_t>& sessionKey) {
        authenticatedSessionKey_ = sessionKey;
        LOG_INFO("Cached auth session key for world handoff (", authenticatedSessionKey_.size(), " bytes)");
        // A login this client's own screen started is moved on by that screen,
        // from the state it polls each frame. One the original login screen
        // started is not - AuthScreen only polls while its own button armed
        // it - so the same step happens here, and only for that case.
        if (glueLoginActive_) {
            glueLoginActive_ = false;
            LOG_INFO("Glue login authenticated, transitioning to realm selection");
            setState_(AppState::REALM_SELECTION);
        }
    });

    // Authentication screen callback
    uiManager_.getAuthScreen().setOnSuccess([this]() {
        LOG_INFO("Authentication successful, transitioning to realm selection");
        setState_(AppState::REALM_SELECTION);
    });

    // Realm selection callback
    uiManager_.getRealmScreen().setOnRealmSelected(
        [this](const std::string& realmName, const std::string& realmAddress) {
            selectRealm(realmName, realmAddress);
        });

    // Realm screen back button - return to login
    uiManager_.getRealmScreen().setOnBack([this]() {
        authHandler_.disconnect();
        uiManager_.getRealmScreen().reset();
        setState_(AppState::AUTHENTICATION);
    });

    // Character selection callback
    uiManager_.getCharacterScreen().setOnCharacterSelected([this](uint64_t characterGuid) {
        LOG_INFO("Character selected: GUID=0x", std::hex, characterGuid, std::dec);
        // Always set the active character GUID
        gameHandler_.setActiveCharacterGuid(characterGuid);
        // Keep CHARACTER_SELECTION active until world entry is fully loaded.
        // This avoids exposing pre-load hitching before the loading screen/intro.
    });

    // Character create screen callbacks
    uiManager_.getCharacterCreateScreen().setOnCreate([this](const game::CharCreateData& data) {
        pendingCreatedCharacterName_ = data.name;  // Store name for auto-selection
        gameHandler_.createCharacter(data);
    });

    uiManager_.getCharacterCreateScreen().setOnCancel([this]() {
        setState_(AppState::CHARACTER_SELECTION);
    });

    // Character create result callback
    gameHandler_.setCharCreateCallback([this](bool success, const std::string& msg) {
        if (success) {
            // Auto-select the newly created character
            if (!pendingCreatedCharacterName_.empty()) {
                uiManager_.getCharacterScreen().selectCharacterByName(pendingCreatedCharacterName_);
                pendingCreatedCharacterName_.clear();
            }
            setState_(AppState::CHARACTER_SELECTION);
        } else {
            uiManager_.getCharacterCreateScreen().setStatus(msg, true);
            pendingCreatedCharacterName_.clear();
        }
    });

    // Character login failure callback
    gameHandler_.setCharLoginFailCallback([this](const std::string& reason) {
        LOG_WARNING("Character login failed: ", reason);
        setState_(AppState::CHARACTER_SELECTION);
        uiManager_.getCharacterScreen().setStatus("Login failed: " + reason, true);
    });

    // "New Hero" button on character screen
    uiManager_.getCharacterScreen().setOnCreateCharacter([this]() {
        uiManager_.getCharacterCreateScreen().reset();
        // Apply expansion race/class constraints before showing the screen
        if (expansionRegistry_ && expansionRegistry_->getActive()) {
            auto* profile = expansionRegistry_->getActive();
            uiManager_.getCharacterCreateScreen().setExpansionConstraints(
                profile->races, profile->classes);
        }
        uiManager_.getCharacterCreateScreen().initializePreview(assetManager_);
        setState_(AppState::CHARACTER_CREATION);
    });

    // "Back" button on character screen
    uiManager_.getCharacterScreen().setOnBack([this]() {
        // Disconnect from world server and reset UI state for fresh realm selection
        gameHandler_.disconnect();
        uiManager_.getRealmScreen().resetForBack();
        uiManager_.getCharacterScreen().reset();
        setState_(AppState::REALM_SELECTION);
    });

    // "Delete Character" button on character screen
    uiManager_.getCharacterScreen().setOnDeleteCharacter([this](uint64_t guid) {
        gameHandler_.deleteCharacter(guid);
    });

    // Character delete result callback
    gameHandler_.setCharDeleteCallback([this](bool success, const std::string& message) {
        uiManager_.getCharacterScreen().setStatus(message, !success);
        if (success) {
            gameHandler_.requestCharacterList();
        }
    });
}

void UIScreenCallbackHandler::selectRealm(const std::string& realmName,
                                          const std::string& realmAddress) {
    LOG_INFO("Realm selected: ", realmName, " (", realmAddress, ")");

    // Parse realm address (format: "hostname:port")
    std::string host = realmAddress;
    uint16_t port = 8085;  // Default world server port

    size_t colonPos = realmAddress.find(':');
    if (colonPos != std::string::npos) {
        host = realmAddress.substr(0, colonPos);
        try { port = static_cast<uint16_t>(std::stoi(realmAddress.substr(colonPos + 1))); }
        catch (...) { LOG_WARNING("Invalid port in realm address: ", realmAddress); }
    }

    // LAN clients are often handed a public realm address by MaNGOS. On
    // routers without NAT reflection that address cannot hairpin back to
    // the local world server. Allow a client-side host override while
    // retaining the realm-advertised port and authentication identity.
    if (const char* overrideHost = std::getenv("WOWEE_REALM_HOST_OVERRIDE");
        overrideHost && *overrideHost) {
        LOG_WARNING("Overriding realm host '", host, "' with '", overrideHost,
                    "' via WOWEE_REALM_HOST_OVERRIDE");
        host = overrideHost;
    }

    // Connect to world server
    auto sessionKey = authHandler_.getSessionKey();
    if (sessionKey.empty() && !authenticatedSessionKey_.empty()) {
        LOG_WARNING("Auth handler session key was empty at realm selection; using cached key");
        sessionKey = authenticatedSessionKey_;
    }
    if (sessionKey.size() != 40) {
        LOG_ERROR("Cannot connect to realm: auth session key has ", sessionKey.size(),
                  " bytes; expected 40. Re-authenticate and try again.");
    }
    std::string accountName = authHandler_.getUsername();
    if (accountName.empty()) {
        LOG_WARNING("Auth username missing; falling back to TESTACCOUNT");
        accountName = "TESTACCOUNT";
    }

    uint32_t realmId = 0;
    uint16_t realmBuild = 0;
    {
        // WotLK AUTH_SESSION includes a RealmID field; some servers reject if it's wrong/zero.
        const auto& realms = authHandler_.getRealms();
        for (const auto& r : realms) {
            if (r.name == realmName && r.address == realmAddress) {
                realmId = r.id;
                realmBuild = r.build;
                // Kept for GetServerName, which the original character screen
                // reads to label the realm it is showing characters from.
                // Read off the realm list rather than from the name alone:
                // whether a realm is PvP, RP or down is in its row.
                selectedRealmName_ = r.name;
                selectedRealmPvp_  = (r.icon == 1 || r.icon == 8);
                selectedRealmRp_   = (r.icon == 6 || r.icon == 8);
                selectedRealmDown_ = (r.flags & 0x02) != 0;   // REALM_FLAG_OFFLINE
                haveSelectedRealm_ = true;
                break;
            }
        }
        LOG_INFO("Selected realmId=", realmId, " realmBuild=", realmBuild);
    }

    uint32_t clientBuild = 12340; // default WotLK
    if (expansionRegistry_) {
        auto* profile = expansionRegistry_->getActive();
        if (profile) clientBuild = profile->worldBuild;
    }
    // Prefer realm-reported build when available (e.g. vanilla servers
    // that report build 5875 in the realm list)
    if (realmBuild != 0) {
        clientBuild = realmBuild;
        LOG_INFO("Using realm-reported build: ", clientBuild);
    }
    if (gameHandler_.connect(host, port, sessionKey, accountName, clientBuild, realmId)) {
        LOG_INFO("Connected to world server, transitioning to character selection");
        setState_(AppState::CHARACTER_SELECTION);
    } else {
        LOG_ERROR("Failed to connect to world server");
    }
}

bool UIScreenCallbackHandler::selectedRealmInfo(std::string& name, bool& isPvp,
                                                bool& isRp, bool& isDown) const {
    if (!haveSelectedRealm_) return false;
    name   = selectedRealmName_;
    isPvp  = selectedRealmPvp_;
    isRp   = selectedRealmRp_;
    isDown = selectedRealmDown_;
    return true;
}

namespace {

/// The lines the status dialog shows while a login runs.
///
/// The first three are GlueStrings.lua's own values - LOGIN_STATE_CONNECTING,
/// LOGIN_STATE_AUTHENTICATING and REALM_LIST_IN_PROGRESS - repeated here
/// rather than read from it because OPEN_STATUS_DIALOG carries the finished
/// text and there is no path from this side of the client into the glue Lua
/// state to resolve a global. That makes them English where the installation's
/// own strings are not; they are the only strings here that could have been.
/// The rest are this client's own sentences, about situations the original
/// client does not have.
constexpr const char* kGlueConnecting     = "Connecting";
constexpr const char* kGlueAuthenticating = "Authenticating";
constexpr const char* kGlueRealmList      = "Retrieving realm list";
constexpr const char* kGlueEnterName      = "Please enter your account name.";
constexpr const char* kGlueEnterPassword  = "Please enter your password.";
constexpr const char* kGlueNoServer =
    "No server to log in to. This screen's login button carries no address - "
    "use this client's own login screen once, and it will reuse that server.";
constexpr const char* kGlueUnreachable = "Could not reach";
/// A code this screen has no box for. The original asks for one through its
/// token dialog, whose OK button calls a TokenEntered this client does not
/// bind - so saying what happened and letting the login be dismissed is the
/// honest answer, where a dialog that accepted a code and dropped it is not.
constexpr const char* kGlueSecurityCode =
    "This account needs an authenticator or PIN code. Use this client's own "
    "login screen to enter it.";
constexpr const char* kGlueFailed = "Authentication failed";

/// The auth server this client's own login screen last used, as "host:port".
///
/// login.cfg's `active` line, which the login screen writes on every attempt.
/// Read here rather than asked of AuthScreen because the screen keeps it in a
/// text box it owns; the file is the part both can name.
bool activeAuthServer(std::string& host, uint16_t& port) {
    std::ifstream in(core::getConfigRoot() + "/login.cfg");
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const std::string prefix = "active=";
        if (line.rfind(prefix, 0) != 0) continue;
        const std::string value = line.substr(prefix.size());
        const size_t colon = value.rfind(':');
        if (colon == std::string::npos || colon == 0) return false;
        host = value.substr(0, colon);
        try { port = static_cast<uint16_t>(std::stoi(value.substr(colon + 1))); }
        catch (...) { return false; }
        return !host.empty() && port != 0;
    }
    return false;
}

}  // namespace

bool UIScreenCallbackHandler::beginGlueLogin(const std::string& account,
                                             const std::string& password) {
    // A fresh attempt: whatever the last one left on the screen is not about
    // this one.
    glueStatusFollow_ = false;
    glueFailureReason_.clear();

    if (account.empty() || password.empty()) {
        LOG_WARNING("Glue login: account or password was empty");
        showGlueStatus("OKAY", account.empty() ? kGlueEnterName : kGlueEnterPassword);
        return false;
    }

    std::string host;
    uint16_t port = 3724;
    if (!activeAuthServer(host, port)) {
        // Deliberately not a guess at localhost. DefaultServerLogin carries no
        // address, so the only honest source is a server this client has
        // actually used, and on a fresh install there is none.
        LOG_WARNING("Glue login: no server in login.cfg - use this client's own "
                    "login screen once so it has one to reuse");
        showGlueStatus("OKAY", kGlueNoServer);
        return false;
    }

    // The same version this client's own login screen sends. Set here rather
    // than left to whatever the last attempt configured, because a glue login
    // may be the first attempt of the run.
    if (expansionRegistry_) {
        if (auto* profile = expansionRegistry_->getActive()) {
            auth::ClientInfo info;
            info.majorVersion = profile->majorVersion;
            info.minorVersion = profile->minorVersion;
            info.patchVersion = profile->patchVersion;
            info.build = profile->build;
            info.protocolVersion = profile->protocolVersion;
            info.game = profile->game;
            info.platform = profile->platform;
            info.os = profile->os;
            info.locale = profile->locale;
            info.timezone = profile->timezone;
            info.legacyVanillaRealmList = (profile->id == "classic" ||
                                           profile->id == "turtle" ||
                                           profile->protocolVersion <= 3);
            authHandler_.setClientInfo(info);
        }
    }

    // Take the failure reason for the duration of this login. AuthHandler
    // holds one of these and this client's own login screen installs its own
    // at the start of every attempt it makes, so the two never overlap - and
    // without this the reason a glue login was refused exists nowhere the
    // dialog could read it.
    authHandler_.setOnFailure([this](const std::string& reason) {
        glueFailureReason_ = reason;
    });

    if (!authHandler_.connect(host, port)) {
        LOG_ERROR("Glue login: could not reach ", host, ":", port);
        showGlueStatus("OKAY", std::string(kGlueUnreachable) + " " + host + ":" +
                                   std::to_string(port));
        return false;
    }
    glueLoginActive_ = true;
    glueStatusFollow_ = true;
    authHandler_.authenticate(account, password);
    LOG_INFO("Glue login: authenticating ", account, " against ", host, ":", port);
    // Said now rather than waiting for the next frame's poll: this call comes
    // from the login button, and a button that takes a frame to acknowledge
    // being pressed is the thing this whole path exists to stop.
    showGlueStatus("CANCEL", kGlueConnecting);
    return true;
}

void UIScreenCallbackHandler::forgetAnnouncedGlueState() {
    // Every one of them, not only the screen. A rebuilt glue interface has
    // been told nothing, so a realm list, a character list, a selected
    // character, a status dialog and a disconnect all have to be sayable
    // again - and each is held back by its own record of what was last said.
    announcedRealmCount_ = 0;
    announcedCharacterCount_ = kNoCharacterListYet;
    announcedCharacterGuid_ = 0;
    announcedGlueScreen_.clear();
    announcedStatusDialog_.clear();
    announcedStatusText_.clear();
    announcedDisconnectSerial_ = 0;
}

void UIScreenCallbackHandler::updateGlueScreens() {
    if (!glueEvent_) return;

    // What the login is doing, first: it closes its dialog when the realm list
    // arrives, and the realm list opens over that same spot on the next lines.
    // The other order leaves a dialog closing on top of the list it made way
    // for.
    updateGlueStatusDialog();

    // The world dropped. The client's own way of saying so is the notice
    // across the top of its login screen, and it is set from one place on the
    // way back there; the original screens have a dialog for it and hear about
    // it as DISCONNECTED_FROM_SERVER, whose argument distinguishes the one
    // case that is not a disconnect at all - 4, a parental-control block, is a
    // dialog of its own. Nothing here produces that case, so it is always 0.
    const uint64_t disconnectSerial =
        uiManager_.getAuthScreen().prominentStatusSerial();
    if (disconnectSerial != announcedDisconnectSerial_) {
        announcedDisconnectSerial_ = disconnectSerial;
        if (disconnectSerial != 0) {
            // GlueParent's handler goes back to the login screen itself, so
            // nothing said here about the screen would be news to it.
            closeGlueStatus();
            LOG_INFO("Glue: announcing DISCONNECTED_FROM_SERVER - ",
                     uiManager_.getAuthScreen().prominentStatusMessage());
            glueEvent_("DISCONNECTED_FROM_SERVER", {"0"});
        }
    }

    // The realm list, once per arrival. RealmList.lua shows its own frame on
    // this event and refreshes it when the frame is already up, and that is
    // the whole of how the original realm list ever appears - it is not opened
    // by anything the player presses.
    const size_t realmCount = authHandler_.getRealms().size();
    if (realmCount != announcedRealmCount_) {
        announcedRealmCount_ = realmCount;
        if (realmCount > 0) glueEvent_("OPEN_REALM_LIST", {});
    }

    // The character list. A count rather than a callback because there is no
    // callback to take: the world handler holds the vector and never says when
    // it changed, which cost nothing while only a screen that re-read it every
    // frame was looking.
    if (!gameHandler_.isConnected()) {
        announcedCharacterCount_ = kNoCharacterListYet;
        announcedCharacterGuid_  = 0;
    } else {
        const size_t characterCount = gameHandler_.getCharacters().size();
        if (characterCount != announcedCharacterCount_) {
            announcedCharacterCount_ = characterCount;
            glueEvent_("CHARACTER_LIST_UPDATE", {});
        }
    }

    // Which of them is highlighted. The original screen keeps the highlight
    // and the name under the models from this event rather than from the click
    // that caused it, so without it a second character can be clicked and the
    // first is still the one Enter World would use.
    const uint64_t activeGuid = gameHandler_.getActiveCharacterGuid();
    if (activeGuid != announcedCharacterGuid_) {
        announcedCharacterGuid_ = activeGuid;
        int row = 0;
        const auto& characters = gameHandler_.getCharacters();
        for (size_t i = 0; i < characters.size(); ++i) {
            if (characters[i].guid == activeGuid) { row = static_cast<int>(i) + 1; break; }
        }
        glueEvent_("UPDATE_SELECTED_CHARACTER", {std::to_string(row)});
    }
}

void UIScreenCallbackHandler::showGlueStatus(const char* dialogType,
                                             const std::string& text) {
    if (!glueEvent_ || !dialogType) return;
    if (announcedStatusDialog_ == dialogType && announcedStatusText_ == text) return;

    if (announcedStatusDialog_ == dialogType) {
        // Same dialog, next line. UPDATE_STATUS_DIALOG re-texts the one that
        // is up and re-sizes it, which is how "Connecting" becomes
        // "Authenticating" in the original without the dialog blinking.
        announcedStatusText_ = text;
        glueEvent_("UPDATE_STATUS_DIALOG", {text});
        return;
    }

    announcedStatusDialog_ = dialogType;
    announcedStatusText_ = text;
    glueEvent_("OPEN_STATUS_DIALOG", {dialogType, text});
}

void UIScreenCallbackHandler::closeGlueStatus() {
    if (!glueEvent_ || announcedStatusDialog_.empty()) return;
    announcedStatusDialog_.clear();
    announcedStatusText_.clear();
    glueEvent_("CLOSE_STATUS_DIALOG", {});
}

void UIScreenCallbackHandler::updateGlueStatusDialog() {
    if (!glueStatusFollow_) return;

    // "CANCEL" while the login can still be abandoned - its one button is
    // CANCEL and calls StatusDialogClick, which is bound to abandoning it -
    // and "OKAY" once it cannot, whose button is OKAY and calls the same thing
    // to dismiss what is left. Both are GlueDialog's own types, out of
    // GlueDialogTypes in GlueDialog.lua.
    switch (authHandler_.getState()) {
        case auth::AuthState::CONNECTED:
        case auth::AuthState::CHALLENGE_SENT:
            showGlueStatus("CANCEL", kGlueConnecting);
            break;

        case auth::AuthState::CHALLENGE_RECEIVED:
        case auth::AuthState::PROOF_SENT:
            showGlueStatus("CANCEL", kGlueAuthenticating);
            break;

        case auth::AuthState::PIN_REQUIRED:
        case auth::AuthState::AUTHENTICATOR_REQUIRED:
            showGlueStatus("OKAY", kGlueSecurityCode);
            glueStatusFollow_ = false;
            break;

        case auth::AuthState::AUTHENTICATED:
        case auth::AuthState::REALM_LIST_REQUESTED:
            showGlueStatus("CANCEL", kGlueRealmList);
            break;

        case auth::AuthState::REALM_LIST_RECEIVED:
            // The realm list is the next thing the player looks at, and it is
            // a dialog in the same place. Nothing left to say.
            closeGlueStatus();
            glueStatusFollow_ = false;
            break;

        case auth::AuthState::FAILED:
            // Whatever AuthHandler decided, in its own words: a wrong
            // password, a banned or suspended account, a build the server
            // refuses, a server that answered something unparseable. This is
            // the case the login screen had no way of showing at all.
            showGlueStatus("OKAY", glueFailureReason_.empty() ? kGlueFailed
                                                              : glueFailureReason_);
            glueStatusFollow_ = false;
            break;

        case auth::AuthState::DISCONNECTED:
            // Cancelled, or dropped before it got anywhere. cancelGlueLogin
            // has already taken the dialog down in the first case; this is the
            // second.
            closeGlueStatus();
            glueStatusFollow_ = false;
            break;
    }
}

void UIScreenCallbackHandler::noteClientScreen(const std::string& glueScreenName) {
    if (!glueEvent_ || glueScreenName.empty()) return;
    if (glueScreenName == announcedGlueScreen_) return;
    announcedGlueScreen_ = glueScreenName;
    glueEvent_("SET_GLUE_SCREEN", {glueScreenName});
}

void UIScreenCallbackHandler::cancelGlueLogin() {
    glueLoginActive_ = false;
    glueStatusFollow_ = false;
    // GlueDialog_OnClick hides the dialog before it calls this, so for the
    // button this is a formality - but CancelLogin is also the cancel button
    // beside the login button and the token dialog's escape, and neither of
    // those hides anything.
    closeGlueStatus();
    authHandler_.disconnect();
    uiManager_.getRealmScreen().reset();
    setState_(AppState::AUTHENTICATION);
}

}} // namespace wowee::core
