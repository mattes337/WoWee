#pragma once

// An authored M2 scene drawn into an offscreen image: the login screen, the
// racial backdrops the character screens stand a character in, and anything
// else a glue screen names through its markup.
//
// One path for all of them. What a screen says about its scene - which model,
// which of the model's cameras, which animation, the fog, the lights and the
// glow - arrives as a GlueSceneState and nothing here knows which screen said
// it. The state is read out of the markup by the glue API (GlueXML's
// ModelFFX attributes, SetModel, SetBackgroundModel and the fog and light
// calls), or written by this client's own screens to say the same thing; the
// scene is drawn the same way either way.
//
// M2Renderer rather than CharacterRenderer, because these scenes are M2s and
// not figures: the Northrend login model carries forty particle emitters and
// that is what its snow and the burst over the citadel's spire are made of.
// Figures - a character on the select screen - are drawn into the same pass
// by whoever owns them, through the callback record() takes, so they share
// the scene's depth, lighting and fog.

#include "rendering/camera.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering {

class Renderer;

/// One directional light a glue screen adds around its scene.
///
/// The thirteen numbers GlueParent.lua's light tables hold, in the order
/// AddLight takes them after the light-set index: enabled, type, a direction,
/// an ambient intensity and colour, a diffuse intensity and colour. Every row
/// in every one of those tables is type 0 - the comment above them says the
/// current version only supports directional lights - so there is no point
/// carrying the type past the reading of it.
struct GlueSceneLight {
    /// The way the light travels, not the way to the light: the model shader
    /// negates it, and the tables' commonest row is (0, 0, -1), which is a
    /// light shining straight down a Z-up scene.
    float direction[3] = {0.0f, 0.0f, -1.0f};
    float ambientIntensity = 0.0f;
    float ambientColor[3] = {0.0f, 0.0f, 0.0f};
    float diffuseIntensity = 0.0f;
    float diffuseColor[3] = {0.0f, 0.0f, 0.0f};
};

/// Everything a glue screen says about the scene it is showing. The scene
/// descriptor: the markup's word on what is drawn and how.
struct GlueSceneState {
    /// The model's path as the markup names it - .mdx is accepted and means
    /// the .m2 beside it, the way the original client takes it.
    std::string model;
    /// Which of the model's own cameras to look through. Every glue screen
    /// asks for zero; SecurityMatrix is the only frame in the interface that
    /// ever names another.
    int cameraIndex = 0;
    int sequence = 0;
    /// SetSequenceTime's second argument, in milliseconds. Recorded and not
    /// applied - see the note on GlueScene::show.
    float sequenceTimeMs = -1.0f;
    float modelScale = 1.0f;
    /// False until one of SetFogNear, SetFogFar and SetFogColor is called, and
    /// false again after ClearFog. Fog is off by default: PatchDownload and
    /// TrialConvert declare a range in markup and the racial screens set one
    /// from CharModelFogInfo, but a race with no row there reaches ClearFog.
    bool fog = false;
    float fogStart = 0.0f;
    float fogEnd = 0.0f;
    float fogColor[3] = {0.0f, 0.0f, 0.0f};
    /// SetGlow: how strongly the scene's bright parts bloom. AccountLogin
    /// says 0.08. Applied as two passes of its own after the scene is drawn -
    /// see GlueScene::textureId.
    float glow = 0.0f;
    /// The background light set, which is the one the scene itself is lit by.
    /// Empty means "the model's own lights", which is what ResetLights means
    /// and what a screen that never adds any is asking for.
    std::vector<GlueSceneLight> lights;
};

/// How a scene's view is aimed: where the camera looks and how wide.
struct GlueSceneFraming {
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

/// Work a scene's view out of the camera the artist baked into the model.
/// `eye` and `target` are the M2 camera's base position and the point it
/// looks at, in the model's own space; `diagonalFov` is its field of view as
/// an M2 stores it, which is the diagonal one in radians.
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
/// Free and header-only so the arithmetic can be tested without a device.
inline GlueSceneFraming glueSceneFraming(const float eye[3], const float target[3],
                                         float diagonalFov, float aspect) {
    GlueSceneFraming out;
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

/// The one directional light and the ambient term this client's model shader
/// takes, worked out from the up-to-four lights a glue screen adds.
struct GlueSceneLighting {
    float direction[3] = {0.0f, 0.0f, -1.0f};
    float lightColor[3] = {0.0f, 0.0f, 0.0f};
    float ambientColor[3] = {0.0f, 0.0f, 0.0f};
    /// False when the screen added no lights at all, which is the ordinary
    /// case: the login screen's own AccountLogin.lua never calls SetLighting,
    /// and ResetLights means "use the model's own". The caller then lights the
    /// scene by the model's own light rather than with black.
    bool authored = false;
};

/// Merge a glue screen's lights into the pair the shader has room for.
///
/// The interface adds up to four directional lights per set and GlueParent's
/// own comment says they are merged in the engine. This client's per-frame
/// block has one directional light and one ambient colour, so the merge is:
///
///  * every light's ambient intensity times its ambient colour is summed into
///    the ambient term. Most of these rows are pure ambient - a diffuse colour
///    of black - and are only there for that sum.
///  * every light's diffuse intensity times its diffuse colour is summed into
///    the light colour, and the direction is their sum weighted by how bright
///    each contribution is.
///
/// The direction is the compromise: a surface facing the merged direction gets
/// what all the lights together would give it, and one facing only one of them
/// gets more than it should. Four lights cannot be four lights here.
///
/// Free and header-only so it can be tested without a device, for the same
/// reason glueSceneFraming above is.
inline GlueSceneLighting glueSceneLighting(const GlueSceneLight* lights, size_t count) {
    GlueSceneLighting out;
    if (lights == nullptr || count == 0) return out;
    out.authored = true;

    float dirSum[3] = {0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < count; ++i) {
        const GlueSceneLight& light = lights[i];
        float contribution[3];
        for (int c = 0; c < 3; ++c) {
            out.ambientColor[c] += light.ambientIntensity * light.ambientColor[c];
            contribution[c] = light.diffuseIntensity * light.diffuseColor[c];
            out.lightColor[c] += contribution[c];
        }
        // Rec. 709 luminance: how much of the picture this light is actually
        // responsible for. Weighting by intensity alone would let a bright
        // multiplier on a black colour - which is most of these rows - drag
        // the direction to where no light is coming from.
        const float weight = 0.2126f * contribution[0] + 0.7152f * contribution[1] +
                             0.0722f * contribution[2];
        if (weight <= 0.0f) continue;
        const float len = std::sqrt(light.direction[0] * light.direction[0] +
                                    light.direction[1] * light.direction[1] +
                                    light.direction[2] * light.direction[2]);
        if (!std::isfinite(len) || len < 1e-6f) continue;
        for (int c = 0; c < 3; ++c) dirSum[c] += weight * light.direction[c] / len;
    }

    const float len = std::sqrt(dirSum[0] * dirSum[0] + dirSum[1] * dirSum[1] +
                                dirSum[2] * dirSum[2]);
    // Lights that cancel each other out leave no direction to point at. The
    // default stands - straight down, which is the direction every purely
    // ambient row in these tables carries - and the light colour it is applied
    // with is the sum, which for that case is black anyway.
    if (std::isfinite(len) && len > 1e-6f) {
        for (int c = 0; c < 3; ++c) out.direction[c] = dirSum[c] / len;
    }
    return out;
}

/// The two distances the model shader's fog runs between.
struct GlueSceneFogRange {
    float start = 0.0f;
    float end = 0.0f;
};

/// Where a glue screen's fog starts and ends, in a form the shader can use.
///
/// Fog has no "off" in that shader - it always mixes by
/// (end - dist) / (end - start) - so switching it off is a range nothing in
/// the scene reaches rather than a flag.
///
/// A range whose end is at or before its start is refused for the same reason:
/// it divides by zero or by a negative, and the result is a NaN or an inverted
/// fog over every pixel of the scene rather than an obviously wrong number
/// somewhere. SetFogNear without SetFogFar - which is a legal thing for a
/// screen to say and leaves the far distance at zero - is exactly that case.
inline GlueSceneFogRange glueSceneFogRange(bool enabled, float start, float end) {
    // Far enough that nothing in a glue scene is inside it. The scenes are
    // authored a couple of hundred units across.
    constexpr GlueSceneFogRange kNoFog{9999.0f, 10000.0f};
    if (!enabled) return kNoFog;
    if (!std::isfinite(start) || !std::isfinite(end)) return kNoFog;
    if (end <= start) return kNoFog;
    return GlueSceneFogRange{start, end};
}

/// Which of a model's cameras SetCamera(index) means. A model with none has
/// nothing to offer; an index past the end falls back to the first, which is
/// what the original client does with a bad index too.
inline int glueCameraIndex(int requested, int cameraCount) {
    if (cameraCount <= 0) return -1;
    if (requested < 0 || requested >= cameraCount) return 0;
    return requested;
}

/// Where a scene puts a figure and how its authored camera looks at it.
struct GlueSceneStage {
    /// Attachment 0, the mark the character stands on; the camera's target
    /// when the model carries no attachment.
    glm::vec3 standPosition{0.0f};
    glm::vec3 cameraEye{0.0f};
    glm::vec3 cameraTarget{0.0f};
    bool valid = false;
};

/// The view an authored scene is drawn into, and the drawing of it.
class GlueScene {
public:
    GlueScene();
    ~GlueScene();
    GlueScene(const GlueScene&) = delete;
    GlueScene& operator=(const GlueScene&) = delete;

    /// What a figure drawn into the scene's pass is given: the command buffer
    /// with the pass open, the per-frame set carrying the scene's camera,
    /// lighting and fog, and the camera itself.
    using FigureDraw =
        std::function<void(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera)>;

    /// Build the view, `width` x `height` pixels. True once there is a target
    /// to draw into. Safe to call again at the same size; a different size
    /// rebuilds.
    bool build(int width, int height, Renderer* renderer, pipeline::AssetManager* assets);
    void shutdown();
    [[nodiscard]] bool isBuilt() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    /// The pass and sample count the target was made with - what a
    /// CharacterRenderer drawing figures into this scene is built against.
    [[nodiscard]] VkRenderPass renderPass() const;
    [[nodiscard]] VkSampleCountFlagBits sampleCount() const;

    /// How much of the target the passes draw and the interface samples, in
    /// pixels. Defaults to the whole target. The login backdrop's target is
    /// allocated in multiples of 32 so a window dragged by a few pixels does
    /// not rebuild it, and a 1280x736 image drawn into a 1280x720 rect
    /// resamples one row in forty-six - which over the frost wyrm read as
    /// pale bands across the wing. So the passes draw exactly this much and
    /// the interface samples exactly this much.
    void setDrawSize(int width, int height);
    [[nodiscard]] float drawAspect() const;

    /// What is behind everything: black for a backdrop, the studio grey the
    /// character screens used, or transparent for a portrait masked by the
    /// frame art around it.
    void setClearColor(const glm::vec4& color);

    /// Show `scene`. The model is loaded when it changes and the rest of the
    /// state is re-applied where it differs from what is already on the
    /// model, so this is safe to call every frame. False for a model the
    /// install does not carry, or one that carries no camera - there is no
    /// second way to place one of these scenes, so it stays unplaced rather
    /// than being put somewhere that happens to look right on one screen.
    ///
    /// One thing the scene carries is recorded and not applied, and is named
    /// here rather than left to be discovered: `sequenceTimeMs`. Nothing here
    /// can seek an animation; the renderer plays them and reads the clock
    /// back but does not take one. The only caller in the whole interface is
    /// SecurityMatrix's sparkle, which this client does not draw.
    bool show(const GlueSceneState& scene);
    /// Show nothing: the clear colour and whatever figures are drawn.
    void clear();
    [[nodiscard]] bool placed() const;

    /// The stand mark and the authored camera of the scene on show.
    [[nodiscard]] GlueSceneStage stage() const;

    /// Aim `camera` through the scene's own camera - the one `show` was asked
    /// for - at the draw rect's aspect. That camera is the whole of the
    /// placement: no facing, no scale, no position, and nothing the interface
    /// computes. False when there is no scene or its camera cannot frame
    /// anything, in which case `camera` is left alone.
    bool frame(Camera& camera) const;

    /// Advance the scene's animation and particles by `deltaTime`, seen from
    /// `camera`.
    void update(float deltaTime, const Camera& camera);

    /// Record the scene into `cmd`: the per-frame block for `slot` (camera,
    /// the lighting the scene asked for or the model's own, the fog), the
    /// pass with `figures` drawn first so the scene's depth and blends see
    /// them, then the model, its particles and ribbons, and the glow passes
    /// when the scene asked for a glow. `slot` picks a per-frame buffer:
    /// a caller recording inside a frame passes the frame's index, so a frame
    /// still in flight is not overwritten; a caller submitting on its own
    /// passes zero.
    void record(VkCommandBuffer cmd, uint32_t slot, const Camera& camera,
                const FigureDraw& figures = {});

    /// Record and submit on their own, and wait. For an owner with no frame
    /// to record into - the glue backdrop draws before the interface pass
    /// that will sample it.
    void composite(const Camera& camera);

    /// The finished picture - the scene with its glow added when the screen
    /// asked for one - or zero until the first pass has run. A
    /// VkDescriptorSet carried as an integer, for the same reason
    /// UnitPortrait::textureId is: the widget tree must not include Vulkan.
    [[nodiscard]] uint64_t textureId() const;

    /// How much of that texture the passes wrote, as a 0..1 fraction of its
    /// width and height. See setDrawSize.
    [[nodiscard]] float textureU1() const;
    [[nodiscard]] float textureV1() const;

    /// The number of per-frame slots record() can be given.
    static constexpr uint32_t kSlots = 2;

private:
    struct View;
    std::unique_ptr<View> view_;
};

} // namespace rendering
} // namespace wowee
