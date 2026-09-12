#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wowee {
namespace pipeline {

/**
 * M2 Model Format (WoW Character/Creature Models)
 *
 * M2 files contain:
 * - Skeletal animated meshes
 * - Multiple texture units and materials
 * - Animation sequences
 * - Bone hierarchy
 * - Particle emitters, ribbon emitters, etc.
 *
 * Reference: https://wowdev.wiki/M2
 */

// Animation sequence data
struct M2Sequence {
    uint32_t id;                    // Animation ID
    uint32_t variationIndex;        // Sub-animation index
    uint32_t duration;              // Length in milliseconds
    float movingSpeed;              // Speed during animation
    uint32_t flags;                 // Animation flags
    int16_t frequency;              // Probability weight
    uint32_t replayMin;             // Minimum replay delay
    uint32_t replayMax;             // Maximum replay delay
    uint32_t blendTime;             // Blend time in ms
    glm::vec3 boundMin;             // Bounding box
    glm::vec3 boundMax;
    float boundRadius;              // Bounding sphere radius
    int16_t nextAnimation;          // Next animation in chain
    uint16_t aliasNext;             // Alias for next animation
};

// Animation track with per-sequence keyframe data
struct M2AnimationTrack {
    uint16_t interpolationType = 0; // 0=none, 1=linear, 2=hermite, 3=bezier
    int16_t globalSequence = -1;    // -1 if not a global sequence

    struct SequenceKeys {
        std::vector<uint32_t> timestamps;   // Milliseconds
        std::vector<glm::vec3> vec3Values;  // For translation/scale tracks
        std::vector<glm::quat> quatValues;  // For rotation tracks
        std::vector<float> floatValues;     // For float tracks (particle emitters)
    };
    std::vector<SequenceKeys> sequences;    // One per animation sequence

    [[nodiscard]] bool hasData() const { return !sequences.empty(); }
};

// Bone data for skeletal animation
struct M2Bone {
    int32_t keyBoneId;              // Bone ID (-1 = not key bone)
    uint32_t flags;                 // Bone flags
    int16_t parentBone;             // Parent bone index (-1 = root)
    uint16_t submeshId;             // Submesh ID
    glm::vec3 pivot;                // Pivot point

    M2AnimationTrack translation;   // Position keyframes per sequence
    M2AnimationTrack rotation;      // Rotation keyframes per sequence
    M2AnimationTrack scale;         // Scale keyframes per sequence
};

// Vertex with skinning data
struct M2Vertex {
    glm::vec3 position;
    uint8_t boneWeights[4];         // Bone weights (0-255)
    uint8_t boneIndices[4];         // Bone indices
    glm::vec3 normal;
    glm::vec2 texCoords[2];         // Two UV sets
};

// Texture unit
struct M2Texture {
    uint32_t type;                  // Texture type
    uint32_t flags;                 // Texture flags
    std::string filename;           // Texture filename (from FileData or embedded)
};

// Render batch (submesh)
struct M2Batch {
    uint8_t flags;
    int8_t priorityPlane;
    uint16_t shader;                // Shader ID
    uint16_t skinSectionIndex;      // Submesh index
    uint16_t colorIndex;            // Color animation index
    uint16_t materialIndex;         // Material index
    uint16_t materialLayer;         // Material layer
    uint16_t textureCount;          // Number of textures
    uint16_t textureIndex;          // First texture lookup index
    uint16_t textureUnit;           // Texture unit
    uint16_t transparencyIndex;     // Transparency animation index
    uint16_t textureAnimIndex;      // Texture animation index

    // Render data
    uint32_t indexStart;            // First index
    uint32_t indexCount;            // Number of indices
    uint32_t vertexStart;           // First vertex
    uint32_t vertexCount;           // Number of vertices

    // Geoset info (from submesh)
    uint16_t submeshId = 0;         // Submesh/geoset ID (determines body part group)
    uint16_t submeshLevel = 0;      // Submesh level (0=base, 1+=LOD/alternate mesh)
};

// Material / render flags (per-batch blend mode)
struct M2Material {
    uint16_t flags;       // Render flags (unlit, unfogged, two-sided, etc.)
    uint16_t blendMode;   // 0=Opaque, 1=AlphaKey, 2=Alpha, 3=Add, 4=Mod, 5=Mod2x, 6=BlendAdd, 7=Screen
};

// Texture transform (UV animation) data
struct M2TextureTransform {
    M2AnimationTrack translation;   // UV translation keyframes
    M2AnimationTrack rotation;      // UV rotation keyframes (quat)
    M2AnimationTrack scale;         // UV scale keyframes
};

// Attachment point (bone-anchored position for weapons, effects, etc.)
struct M2Attachment {
    uint32_t id;        // 0=Head, 1=RightHand, 2=LeftHand, etc.
    uint16_t bone;      // Bone index
    glm::vec3 position; // Offset from bone pivot
};

// Camera baked into the model. Scene models (the character-select glue screens,
// cinematics) carry the framing the artist authored them for; without it, a scene
// whose geometry sits hundreds of units from its origin cannot be placed sensibly.
struct M2Camera {
    /// 0 is the portrait camera, 1 the character-sheet one, -1 anything else.
    /// Kept because the portrait framing is chosen by it: a model that carries
    /// its own portrait camera knows where its face is far better than a
    /// fraction of the bind-pose height can guess.
    int32_t type = -1;
    float fov = 0.0f;      // radians
    float farClip = 0.0f;
    float nearClip = 0.0f;
    glm::vec3 positionBase{0.0f};
    glm::vec3 targetBase{0.0f};  // the point the camera looks at
};

// FBlock: particle lifetime curve (color/alpha/scale over particle life)
struct M2FBlock {
    std::vector<float> timestamps;      // Normalized 0..1
    std::vector<float> floatValues;     // For alpha/scale
    std::vector<glm::vec3> vec3Values;  // For color RGB
};

// Particle emitter definition parsed from M2
struct M2ParticleEmitter {
    int32_t particleId;
    uint32_t flags;
    glm::vec3 position;
    uint16_t bone;
    uint16_t texture;
    uint8_t blendingType;   // 0=opaque,1=alphakey,2=alpha,4=add
    uint8_t emitterType;    // 1=plane,2=sphere,3=spline
    int16_t textureTileRotation = 0;
    uint16_t textureRows = 1;
    uint16_t textureCols = 1;
    M2AnimationTrack emissionSpeed;
    M2AnimationTrack speedVariation;
    M2AnimationTrack verticalRange;
    M2AnimationTrack horizontalRange;
    M2AnimationTrack gravity;
    M2AnimationTrack lifespan;
    M2AnimationTrack emissionRate;
    M2AnimationTrack emissionAreaLength;  // plane: extent along the bone's X; sphere: inner radius
    M2AnimationTrack emissionAreaWidth;   // plane: extent along the bone's Y; sphere: outer radius
    /// When above zero the birth velocity points from (0, 0, zSource) in the
    /// bone's space to the birth point instead of into the authored cone.
    M2AnimationTrack zSource;
    M2FBlock particleColor;   // vec3 RGB at 3 timestamps
    M2FBlock particleAlpha;   // float (from uint16/32767) at 3 timestamps
    M2FBlock particleScale;   // float (x component of vec2) at 3 timestamps
    /// Flipbook cell over the particle's life, for a tiled texture: uint16
    /// cell numbers carried as floats. Empty when the model authors none.
    M2FBlock headCellTrack;
    /// FollowPosition (flag 0x4000): how much of the emitter's travel since the
    /// last update its live particles are carried by. The fraction is a line
    /// through (followSpeed1, followScale1) and (followSpeed2, followScale2)
    /// in emitter speed, clamped at one. Zero when the pair spans no speed.
    float followSpeed1 = 0.0f;
    float followScale1 = 0.0f;
    float followSpeed2 = 0.0f;
    float followScale2 = 0.0f;
    bool enabled = true;
};

// Ribbon emitter definition parsed from M2 (WotLK format)
struct M2RibbonEmitter {
    int32_t  ribbonId   = 0;
    uint32_t bone       = 0;        // Bone that drives the ribbon spine
    glm::vec3 position{0.0f};       // Offset from bone pivot

    uint16_t textureIndex  = 0;     // First texture lookup index
    uint16_t materialIndex = 0;     // First material lookup index (blend mode)

    // Animated tracks
    M2AnimationTrack colorTrack;       // RGB 0..1
    M2AnimationTrack alphaTrack;       // float 0..1 (stored as fixed16 on disk)
    M2AnimationTrack heightAboveTrack; // Half-width above bone
    M2AnimationTrack heightBelowTrack; // Half-width below bone
    M2AnimationTrack visibilityTrack;  // 0=hidden, 1=visible

    float edgesPerSecond = 15.0f;   // How many edge points are generated per second
    float edgeLifetime   = 0.5f;    // Seconds before edges expire
    float gravity        = 0.0f;    // Downward pull on edges per s²
    uint16_t textureRows = 1;
    uint16_t textureCols = 1;
};

/// The shader id a skin batch would carry if it were spelled the way
/// m2TexCombiner reads it, for a model that stores combiners the older way.
///
/// A model with global flag 0x08 does not put a shader id in its batches. It
/// keeps one array of texture combiners in its header - Blizzard's
/// texture_combiner_combos - and each batch's `shader` field is an offset into
/// it: `textureCount` entries starting there, one op per layer, in the same
/// numbering the low bits of a shader id use (0 Opaque, 1 Mod, 3 Add, 4 Mod2x,
/// 6 Mod2xNA, 7 AddNA). Read as a shader id the offset is nonsense: the
/// Northrend login scene stores [1, 4, 1, 1] and its light shafts say 0, which
/// is Mod_Mod2x by the table and Opaque_Opaque by the number, and its glow
/// says 2, which is Mod_Mod by the table and Opaque_AddAlpha by the number.
/// Roughly a third of the client's models carry the flag, most of them with
/// [1, 4] or [1, 6] - an environment-mapped specular sheet over a Mod base.
///
/// Returns `shader` unchanged for a model without the array, a batch of one
/// layer, or an offset the array does not cover; otherwise the two ops packed
/// as m2TexCombiner reads them, layer 0's in bits 0x70 and layer 1's in the
/// low three.
inline uint16_t m2ShaderFromCombinerCombos(uint16_t shader, uint16_t textureCount,
                                           const std::vector<uint16_t>& combos) {
    if (combos.empty() || textureCount != 2) return shader;
    const size_t at = shader;
    if (at + 1 >= combos.size()) return shader;
    return static_cast<uint16_t>(((combos[at] & 7) << 4) | (combos[at + 1] & 7));
}

/// Which two-layer combine a skin batch's `shader` asks for, as the index the
/// model shaders implement. Zero means "layer 0 alone".
///
/// The field is not a combiner index. It is the client's own shader selector,
/// and Blizzard resolves it through a table whose shape is nothing like the
/// raw number: bit 0x8000 means the low bits are already a combiner, bits 0x70
/// choose the Mod_* family over the Opaque_* one, and only the low three bits
/// pick within a family. A model that stores combiners as an array instead
/// has its batches rewritten into this shape at load - see
/// m2ShaderFromCombinerCombos.
///
/// The names read <layer0 op>_<layer1 op>. "Opaque" on the first means layer
/// 0's alpha is not used; "NA" on the second means layer 1's is not used.
inline int32_t m2TexCombiner(uint16_t textureCount, uint16_t shaderId,
                             uint16_t blendMode) {
    if (textureCount < 2) return 0;
    // An explicit combiner in the low bits. Nothing in 3.3.5a's art sets it,
    // and mapping it would be a guess, so such a batch draws layer 0 alone.
    if (shaderId & 0x8000) return 0;
    const uint16_t lower = static_cast<uint16_t>(shaderId & 7);
    if (shaderId & 0x70) {
        switch (lower) {
            case 0:  return 10;  // Mod_Opaque
            case 3:  return 7;   // Mod_Add
            case 4:  return 6;   // Mod_Mod2x
            case 6:  return 8;   // Mod_Mod2xNA
            case 7:  return 9;   // Mod_AddNA
            default: return 5;   // Mod_Mod
        }
    }
    switch (lower) {
        // Blizzard's table calls this one Opaque_Opaque, whose alpha is the
        // material's rather than either layer's. Only a model without a
        // combiner array reaches it with a zero - every model with one has
        // been rewritten into a Mod_* id above - and for those, which is the
        // older art, the raw zero is not a statement of intent. An additive
        // batch keeps the material's alpha, where alpha is an intensity and
        // the second layer has already shaped the colour; a blended one takes
        // the second layer's, where a sheet with no colour slot would
        // otherwise draw as an opaque slab.
        //
        // Additive is blend mode 3 and 4 - the same two rendering's
        // m2BlendIsAdditive names. Modes 5 and 6, Modulate and Modulate2x,
        // sit above them in the enumeration and are not additive; an
        // open-ended test here was treating them as if they were.
        case 0:  return (blendMode == 3 || blendMode == 4) ? 4 : 1;   // Opaque_Opaque / Opaque_Mod
        case 3:  return 11;  // Opaque_AddAlpha
        case 4:  return 11;  // Opaque_AddAlpha
        case 6:  return 12;  // Opaque_Mod2xNA_Alpha
        case 7:  return 11;  // Opaque_AddAlpha
        default: return 1;   // Opaque_Mod
    }
}

/// A light the model carries.
///
/// A local light: attenuated between two distances, attached to a bone, and
/// switched on and off by its own visibility track. It is a contribution the
/// model adds to whatever lights the scene, not the scene's lighting - the
/// Northrend login scene's one light is ambient-only at 1.3 with the diffuse
/// term authored zero for the whole sequence, and installed as the scene's
/// global light it lit the frost wyrm flat and pale.
///
/// Only the at-rest values: every one of these is an M2Track and nothing in
/// 3.3.5a's scene models animates them. All seven tracks are read, so the
/// question "is this light on, and how far does it reach" is answerable from
/// the model rather than from a default.
struct M2Light {
    uint16_t type = 0;            // 0 = directional, 1 = point
    int16_t bone = -1;
    glm::vec3 position{0.0f};
    glm::vec3 ambientColor{1.0f};
    float ambientIntensity = 1.0f;
    glm::vec3 diffuseColor{1.0f};
    float diffuseIntensity = 1.0f;
    float attenuationStart = 0.0f;
    float attenuationEnd = 0.0f;
    /// Off when the visibility track's first key is zero. An absent track
    /// means on, which is the 3.3.5a convention.
    bool visible = true;
};

// Complete M2 model structure
struct M2Model {
    // Model metadata
    std::string name;
    uint32_t version;
    glm::vec3 boundMin;             // Model bounding box
    glm::vec3 boundMax;
    float boundRadius;              // Bounding sphere

    // Geometry data
    std::vector<M2Vertex> vertices;
    std::vector<uint16_t> indices;

    // Skeletal animation
    std::vector<M2Bone> bones;
    std::vector<M2Sequence> sequences;
    std::vector<uint32_t> globalSequenceDurations;  // Per-global-sequence loop durations (ms)

    // Footfall ($FSD) animation event times per sequence index, in ms from the
    // start of the sequence, sorted ascending. These are the authored keyframes
    // the game client uses to sync footstep sounds to feet hitting the ground.
    // Empty inner list = no footfall events for that sequence.
    std::vector<std::vector<uint32_t>> footstepEventTimes;

    // Bone lookup table (vertex bone indices reference this to get global bone index)
    std::vector<uint16_t> boneLookupTable;

    // Rendering
    std::vector<M2Batch> batches;
    std::vector<M2Texture> textures;
    std::vector<uint16_t> textureLookup;  // Batch texture index lookup
    std::vector<M2Material> materials;    // Render flags / blend modes

    // Texture transforms (UV animation)
    std::vector<M2TextureTransform> textureTransforms;
    std::vector<uint16_t> textureTransformLookup;

    /// Which UV set each texture unit of a skin batch reads. A batch's
    /// `textureUnit` is an index into this, one entry per layer; 0 and 1 are
    /// the vertex's two sets and 0xFFFF means the coordinates are computed -
    /// a spherical environment map.
    std::vector<uint16_t> textureCoordCombos;

    // Texture weights (per-batch opacity, from M2Track<fixed16>)
    // Each entry is the "at-rest" opacity value (0=transparent, 1=opaque).
    // batch.transparencyIndex → textureWeightLookup[idx] → textureWeights[trackIdx]
    std::vector<float> textureWeights;
    std::vector<M2AnimationTrack> textureWeightTracks;
    std::vector<uint16_t> textureWeightLookup;

    // Color animation alpha values (from M2Color.alpha M2Track<fixed16>)
    // One entry per color animation slot; batch.colorIndex indexes directly into this.
    // Value 0=transparent, 1=opaque. Independent from textureWeights.
    std::vector<float> colorAlphas;
    /// At-rest RGB of each colour track, parallel to colorAlphas.
    ///
    /// An M2Color is a vec3 colour track followed by an alpha track, and only
    /// the alpha was read. The colour is what tints a batch: Orgrimmar's
    /// bonfire glow is authored white and carries (1.0, 0.329, 0.0) here, so
    /// without it the fire renders as a white blob.
    std::vector<glm::vec3> colorRGB;

    // Full per-sequence alpha keyframes for the same color slots. Evaluated at
    // render time to hide batches whose alpha animates to 0 in the current
    // animation (e.g. the lumberjack carry model's alternate wood bundle).
    std::vector<M2AnimationTrack> colorAlphaTracks;

    // Attachment points (for weapon/effect anchoring)
    std::vector<M2Attachment> attachments;
    std::vector<M2Camera> cameras;
    /// The lights the model carries, at rest. See M2Light.
    std::vector<M2Light> lights;
    std::vector<uint16_t> attachmentLookup; // attachment ID → index

    // Particle emitters
    std::vector<M2ParticleEmitter> particleEmitters;

    // Ribbon emitters
    std::vector<M2RibbonEmitter> ribbonEmitters;

    // Collision mesh (simplified geometry for physics)
    std::vector<glm::vec3> collisionVertices;
    std::vector<uint16_t> collisionIndices;      // 3 per triangle
    std::vector<glm::vec4> collisionNormals;     // xyz=normal, w=distance; one per triangle

    // Flags
    uint32_t globalFlags;

    /// Texture combiner combos, when global flag 0x08 says the batches store
    /// combiners this way; empty otherwise. Already applied to every batch's
    /// `shader` by the loader - see m2ShaderFromCombinerCombos - and kept so
    /// a reader of the model can see what it was applied from.
    std::vector<uint16_t> textureCombinerCombos;

    [[nodiscard]] bool isValid() const {
        return !vertices.empty() && !indices.empty();
    }
};

class M2Loader {
public:
    /**
     * Load M2 model from raw file data
     *
     * @param m2Data Raw M2 file bytes
     * @return Parsed M2 model
     */
    static M2Model load(const std::vector<uint8_t>& m2Data);

    /**
     * Load M2 skin file (contains submesh/batch data)
     *
     * @param skinData Raw M2 skin file bytes
     * @param model Model to populate with skin data
     * @return True if successful
     */
    static bool loadSkin(const std::vector<uint8_t>& skinData, M2Model& model);

    /**
     * Load external .anim file data into model bone tracks
     *
     * @param m2Data Original M2 file bytes (contains track headers)
     * @param animData Raw .anim file bytes
     * @param sequenceIndex Which sequence index this .anim file provides data for
     * @param model Model to patch with animation data
     */
    static void loadAnimFile(const std::vector<uint8_t>& m2Data,
                             const std::vector<uint8_t>& animData,
                             uint32_t sequenceIndex,
                             M2Model& model);
};

std::string skinPathForM2(const std::string& m2Path);

/// The .m2 a model reference means, whatever it was written as.
///
/// WMO doodad lists, ADT doodad lists and GameObjectDisplayInfo all name models
/// with the extension the art was authored with - .mdx, and occasionally .mdl -
/// while what ships is .m2. Every reader has to rewrite it, and nine of them
/// did, in their own words:
///
///   * four rewrote .mdx and .mdl
///   * three rewrote only .mdx, so a .mdl reference reached the asset manager
///     as ".mdl", found nothing, and the doodad silently did not appear
///
/// Both are handled here. A path with any other extension, or none, comes back
/// unchanged - this rewrites a known alias and does not guess.
std::string modelPathToM2(const std::string& modelPath);

} // namespace pipeline
} // namespace wowee
