#pragma once

// A live head-and-shoulders view of a unit, for the interface's portraits.
//
// WoW's portraits are not pictures on disk: they are the character itself,
// rendered small. The same offscreen pass the character-select screen uses
// already frames a face when zoomed all the way in, so this is that pass kept
// running while in the world, at a size a portrait needs.
//
// Four units: the player, the target, the pet and the focus. They differ only
// in whose appearance is loaded - a player from race, appearance bytes and
// what they are visibly wearing, anything else from the model its display id
// names. The party frames want the same thing and are left out on cost: each
// of these is a 640x800 offscreen target and a character pass every frame.
//
// GlueBackdrop, at the bottom, is the other thing this client renders into a
// widget rather than reading off disk: the M2 scene behind the login and
// character screens. It shares the shape of the problem and none of the
// answer - see the note above it.

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wowee {
namespace game { class GameHandler; struct EquipmentItem; }
namespace pipeline { class AssetManager; }
namespace rendering { class CharacterPreview; class Renderer; }

namespace ui {

class UnitPortrait {
public:
    /// How much of the character to show. A portrait is the face in a circle;
    /// the paperdoll wants the whole figure in a tall rectangle. The offscreen
    /// pass is the same either way - only the framing differs, which is why
    /// this is one class and not two.
    enum class Framing { Face, FullBody };

    UnitPortrait();
    ~UnitPortrait();

    /// Builds the offscreen view on first use and keeps it in step with the
    /// player's appearance and equipment afterwards. Safe to call every frame;
    /// it reloads the model only when something about it actually changed.
    void update(game::GameHandler& gameHandler, pipeline::AssetManager* assets,
                rendering::Renderer* renderer, float deltaTime);

    /// Answers whether there is actually a model to show. A load can fail -
    /// a race this client has no model path for, an M2 the install is missing
    /// - and the view then keeps whatever it had, which for a frame that has
    /// just changed unit means the previous one's face. The caller blanks the
    /// texture instead.
    ///
    /// Show another player, from the appearance the world already reads for
    /// them: race, gender, the packed appearance bytes and facial features.
    ///
    /// Dressed in what they are visibly wearing, which for a portrait framed
    /// on the head is the helm and the shoulders - the two pieces that change
    /// a face most. An empty list leaves the model as it is rather than
    /// stripping it, because "nothing known yet" and "wearing nothing" arrive
    /// looking the same and only one of them should undress anybody.
    bool updatePlayer(uint8_t race, uint8_t gender, uint32_t appearanceBytes,
                      uint8_t facialFeatures,
                      const std::vector<game::EquipmentItem>& equipment,
                      pipeline::AssetManager* assets,
                      rendering::Renderer* renderer, float deltaTime);

    /// Show a creature instead, by the model its display id names.
    ///
    /// Kept apart from update() rather than folded into it: a player is built
    /// from race, appearance bytes and equipment, and a creature is a path and
    /// nothing else. The two share the offscreen view and the framing and
    /// agree on nothing else, so one function taking both would be two
    /// functions sharing a name.
    ///
    /// Reloads only when the path changes, so this is safe every frame.
    bool updateCreature(const std::string& m2Path,
                        const std::vector<std::pair<uint32_t, std::string>>& skins,
                        pipeline::AssetManager* assets,
                        rendering::Renderer* renderer, float deltaTime);

    /// A pre-composited skin to put on the character once it is built,
    /// replacing the one made from CharSections.
    ///
    /// CreateDisplayInfoExtra carries one for nearly every humanoid NPC, and
    /// it is the whole appearance already baked - skin, face, hair and the
    /// armour they wear. Set before the update that should use it; it is part
    /// of what decides a rebuild, so changing it is enough to apply it.
    void setBakedSkin(const std::string& path) { pendingBake_ = path; }

    /// Set before the first update, since framing is applied when the model
    /// loads and the model loads once.
    void setFraming(Framing framing) { framing_ = framing; }

    /// How big the offscreen image is, in pixels. Set before the first update,
    /// because the view is built once and keeps the size it was built at.
    ///
    /// Worth setting: this is a full character pass every frame, and the
    /// paperdoll's 640x800 is enormously oversized for a face drawn into a
    /// circle fifty pixels across.
    void setTargetSize(int width, int height) {
        targetWidth_ = width;
        targetHeight_ = height;
    }

    /// Turn the figure by this much, in radians. What the paperdoll's rotate
    /// buttons drive.
    void rotate(float yawDelta);

    /// The rendered portrait, or zero until the first composite has run. The
    /// value is a VkDescriptorSet, carried as an integer so this header does
    /// not drag Vulkan into the widget tree.
    [[nodiscard]] uint64_t textureId() const;

    void shutdown(rendering::Renderer* renderer);

private:
    std::unique_ptr<rendering::CharacterPreview> preview_;
    bool initialized_ = false;
    Framing framing_ = Framing::Face;
    int targetWidth_ = 640;
    int targetHeight_ = 800;
    bool registered_ = false;

    // What the loaded model was built from, so a reload happens only on a real
    // change rather than every frame.
    /// The creature model currently loaded, empty while a player is loaded.
    /// Also the guard against reloading: a portrait rebuilt every frame looks
    /// like one that flickers, and the two are indistinguishable from outside.
    std::string loadedCreaturePath_;
    /// The bake asked for, and the one already on the model.
    std::string pendingBake_;
    std::string loadedBake_;

    uint64_t loadedGuid_ = 0;
    /// Race and gender as well, because another player is identified by these
    /// rather than by a guid that this only ever sees one of at a time.
    uint8_t  loadedRace_ = 0xFF;
    uint8_t  loadedGender_ = 0xFF;
    uint32_t loadedAppearance_ = 0;
    uint8_t  loadedFacialFeatures_ = 0;
    // uint64_t, not size_t: the hash is sixty-four bits and storing it in a
    // pointer-sized field would truncate on a 32-bit build, where two outfits
    // sharing the low half would stop the portrait redrawing.
    uint64_t loadedEquipHash_ = 0;
};

/// How a glue backdrop's view is aimed: where the camera looks and how wide.
struct GlueBackdropFraming {
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    /// Vertical, in degrees, which is what a projection takes.
    float fovYDegrees = 0.0f;
    /// False when the model's camera cannot frame anything - it looks nowhere,
    /// or its field of view is not a field of view. Nothing else in a glue
    /// screen says where to stand, so the answer is "not drawn" rather than a
    /// substitute.
    bool usable = false;
};

/// Work a glue backdrop's view out of the camera the artist baked into the
/// model. `eye` and `target` are the M2 camera's base position and the point
/// it looks at, in the model's own space; `diagonalFov` is its field of view
/// as an M2 stores it, which is the diagonal one in radians.
///
/// Two conversions live here, both easy to get wrong and neither visible as an
/// error when it is:
///
///  * A look-at becomes the yaw and pitch this client's Camera takes. Z is up,
///    so the pitch is the asin of the direction's z and the yaw the atan2 of
///    its y and x.
///  * The diagonal field of view becomes the vertical one, which is the
///    diagonal over sqrt(1 + aspect^2). Handing the diagonal straight to a
///    projection widens the view by roughly the aspect ratio, and a scene seen
///    too wide reads as one placed too far away rather than as a wrong field
///    of view - which sends the search somewhere else entirely.
///
/// Free and header-only so the arithmetic can be tested without a device: the
/// rest of GlueBackdrop needs Vulkan and this is the half that decides whether
/// the picture is framed the way the artist framed it.
inline GlueBackdropFraming glueBackdropFraming(const float eye[3], const float target[3],
                                               float diagonalFov, float aspect) {
    GlueBackdropFraming out;
    const float dx = target[0] - eye[0];
    const float dy = target[1] - eye[1];
    const float dz = target[2] - eye[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(len) || len < 1e-4f) return out;
    if (!std::isfinite(aspect) || aspect <= 0.0f) return out;
    // A field of view outside this is not one: zero draws nothing and a whole
    // turn is not a camera. The bound is the same one the wider client uses
    // when it decides whether an M2 camera's angle can be believed.
    if (!std::isfinite(diagonalFov) || diagonalFov <= 0.01f || diagonalFov >= 3.5f) return out;

    const float nz = dz / len;
    constexpr float kRadToDeg = 57.29577951308232f;
    out.yawDegrees = std::atan2(dy, dx) * kRadToDeg;
    out.pitchDegrees = std::asin(nz < -1.0f ? -1.0f : (nz > 1.0f ? 1.0f : nz)) * kRadToDeg;
    out.fovYDegrees = diagonalFov / std::sqrt(1.0f + aspect * aspect) * kRadToDeg;
    out.usable = true;
    return out;
}

/// The scene behind the login and character screens.
///
/// WoW's glue screens are not painted backdrops: each is an M2 scene - the
/// Dark Portal at login, the race's own home on character select - drawn
/// behind the interface. GlueParent.lua names one per screen, and the original
/// client places it with the camera the artist baked into the model. That
/// camera is the whole of the placement: no facing, no scale, no position, and
/// nothing the interface computes. A model drawn through any other camera
/// renders perfectly well and is framed wrongly, which reads as a different
/// fault entirely.
///
/// Beside UnitPortrait rather than inside it. A portrait is a character built
/// out of appearance bytes and worn gear and framed by a rig this client
/// chooses; a backdrop is a whole authored scene that carries its own framing
/// and has no appearance to build. The two share only "an M2 rendered into a
/// widget's rectangle", which is not enough to share a class over.
class GlueBackdrop {
public:
    GlueBackdrop();
    ~GlueBackdrop();
    GlueBackdrop(const GlueBackdrop&) = delete;
    GlueBackdrop& operator=(const GlueBackdrop&) = delete;

    /// Show `m2Path`, drawn into an image `width` x `height` pixels and framed
    /// by the model's own camera. True once there is something to show.
    ///
    /// Safe to call every frame: the view is rebuilt only when the model or
    /// the size actually changes. False is the honest answer for a model the
    /// install does not carry, and for one that carries no camera - there is
    /// no second way to place one of these scenes, so it stays unplaced rather
    /// than being put somewhere that happens to look right on one screen.
    bool update(const std::string& m2Path, int width, int height,
                pipeline::AssetManager* assets,
                rendering::Renderer* renderer, float deltaTime);

    /// The rendered scene, or zero until the first pass has run. A
    /// VkDescriptorSet carried as an integer, for the same reason
    /// UnitPortrait::textureId is.
    [[nodiscard]] uint64_t textureId() const;

    /// Give the GPU resources back. Must run while the device is still alive,
    /// which is why it is a call and not the destructor's business.
    void shutdown();

private:
    struct View;
    std::unique_ptr<View> view_;
};

} // namespace ui
} // namespace wowee
