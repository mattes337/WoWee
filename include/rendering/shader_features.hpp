#pragma once

/**
 * shader_features.hpp - which variant of a shader a pipeline was built with.
 *
 * Every renderer toggle used to be an `int` in a uniform block and an `if` in
 * the fragment shader. The variant a player ran with normal mapping off still
 * contained the normal-mapping branch, still fetched the flag every pixel, and
 * still paid for the divergence - and the next toggle after it would have been
 * the thirteenth of those in `wmo.frag` alone.
 *
 * A `VkSpecializationInfo` fixes the value when the pipeline is created, so the
 * driver folds the branch away and compiles only what the settings asked for.
 * This is the C++ half; `assets/shaders/shader_features.glsl` is the other, and
 * the two have to agree about every id and default value. They are checked
 * against each other by `tools/shader_feature_check.py`.
 *
 * **Every default is what the client did before phase 01.** A pipeline built
 * from `ShaderFeatures{}` is the shader that shipped - which is the promise
 * `tests/shader_offpath_identity` measures, and the reason nothing in this
 * header may change a default to make a new feature "on by default". A feature
 * is turned on by the setting that owns it, at the point the pipeline is built.
 */

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace wowee {
namespace rendering {

/// The boolean features, one per bit. Bit N is `constant_id = N` in GLSL.
///
/// The order is the file format of the pipeline cache key, so a bit is added
/// at the end and never inserted - see the note on `SettingDesc::choices` for
/// the same rule and the same reason.
enum class ShaderFeatureBit : uint32_t {
    NormalMap = 0,  ///< perturb the normal by the material's normal map
    Parallax = 1,   ///< march the height map for relief
    Shadows = 2,    ///< read the sun shadow map at all
    Count = 3,
};

/// The specialization constant ids that are not bits: small integers whose
/// value is a mode rather than an on/off.
///
/// They start at 16 so the boolean bits have room to grow to sixteen without
/// renumbering anything a shader has already been compiled against.
enum class ShaderFeatureConstant : uint32_t {
    /// RESERVED(phase-01b, L1-csm): 1..4; 1 is the single map the client always
    /// drew, and no shader reads this yet. Here so the ids are settled and this
    /// enum is written once rather than renumbered when cascades land.
    ShadowCascades = 16,
    /// RESERVED(phase-01b, L2-soft-shadows): 0 = 3x3 PCF, 1 = Poisson 16,
    /// 2 = PCSS. As above.
    ShadowFilter = 17,
    FogModel = 18,        ///< 0 = linear start/end, 1 = exponential height fog
};

/// What one pipeline's shaders were specialized with.
///
/// Default-constructed is the pre-phase-01 client, exactly. Anything that
/// builds a pipeline and does not care passes `{}` and gets the shader that
/// always shipped.
struct ShaderFeatures {
    uint32_t bits = defaultBits();
    int32_t shadowCascades = 1;
    int32_t shadowFilter = 0;
    int32_t fogModel = 0;

    /// The bits that are on in the shipped client. `NormalMap` and `Parallax`
    /// are on because the material's own uniform flag already gates them: the
    /// constant is what lets a pipeline drop the branch, not what enables the
    /// feature. `Shadows` is on because every pipeline sampled the map.
    static constexpr uint32_t defaultBits() {
        return (1u << static_cast<uint32_t>(ShaderFeatureBit::NormalMap)) |
               (1u << static_cast<uint32_t>(ShaderFeatureBit::Parallax)) |
               (1u << static_cast<uint32_t>(ShaderFeatureBit::Shadows));
    }

    constexpr bool has(ShaderFeatureBit bit) const {
        return (bits & (1u << static_cast<uint32_t>(bit))) != 0;
    }
    constexpr void set(ShaderFeatureBit bit, bool on) {
        const uint32_t mask = 1u << static_cast<uint32_t>(bit);
        bits = on ? (bits | mask) : (bits & ~mask);
    }

    constexpr bool operator==(const ShaderFeatures& o) const {
        return bits == o.bits && shadowCascades == o.shadowCascades &&
               shadowFilter == o.shadowFilter && fogModel == o.fogModel;
    }
    constexpr bool operator!=(const ShaderFeatures& o) const { return !(*this == o); }

    /// One number that names the variant, for a pipeline cache key and for a
    /// material sort that has to keep variants apart.
    constexpr uint64_t key() const {
        return (static_cast<uint64_t>(bits) << 32) |
               (static_cast<uint64_t>(static_cast<uint32_t>(shadowCascades) & 0xFFu) << 16) |
               (static_cast<uint64_t>(static_cast<uint32_t>(shadowFilter) & 0xFFu) << 8) |
               (static_cast<uint64_t>(static_cast<uint32_t>(fogModel) & 0xFFu));
    }
};

/// The specialization data for one `ShaderFeatures`, laid out for Vulkan.
///
/// Owns its own storage, because `VkSpecializationInfo` points at the caller's
/// buffer and a pipeline is created after the builder call that set it up
/// returns. Keep one of these alive across the `build()`.
///
/// A `VkBool32` and an `int32_t` are both four bytes, so the whole thing is one
/// array of uint32 and one entry per constant.
class ShaderSpecialization {
public:
    explicit ShaderSpecialization(const ShaderFeatures& features) {
        auto push = [this](uint32_t constantId, uint32_t value) {
            const uint32_t offset = static_cast<uint32_t>(data_.size() * sizeof(uint32_t));
            data_.push_back(value);
            entries_.push_back(VkSpecializationMapEntry{
                .constantID = constantId, .offset = offset, .size = sizeof(uint32_t)});
        };
        for (uint32_t bit = 0; bit < static_cast<uint32_t>(ShaderFeatureBit::Count); ++bit) {
            push(bit, features.has(static_cast<ShaderFeatureBit>(bit)) ? VK_TRUE : VK_FALSE);
        }
        push(static_cast<uint32_t>(ShaderFeatureConstant::ShadowCascades),
             static_cast<uint32_t>(features.shadowCascades));
        push(static_cast<uint32_t>(ShaderFeatureConstant::ShadowFilter),
             static_cast<uint32_t>(features.shadowFilter));
        push(static_cast<uint32_t>(ShaderFeatureConstant::FogModel),
             static_cast<uint32_t>(features.fogModel));

        info_.mapEntryCount = static_cast<uint32_t>(entries_.size());
        info_.pMapEntries = entries_.data();
        info_.dataSize = data_.size() * sizeof(uint32_t);
        info_.pData = data_.data();
    }

    ShaderSpecialization(const ShaderSpecialization&) = delete;
    ShaderSpecialization& operator=(const ShaderSpecialization&) = delete;

    const VkSpecializationInfo* info() const { return &info_; }

private:
    std::vector<uint32_t> data_;
    std::vector<VkSpecializationMapEntry> entries_;
    VkSpecializationInfo info_{};
};

/// The variant every lit pipeline in this process is built with.
///
/// One value rather than one per renderer, because the features here are the
/// player's settings and not a property of any one material: the fog model is
/// the same for terrain, buildings, doodads and characters or the horizon does
/// not agree with itself. The four lit renderers read this when they build
/// their pipelines, and rebuild them when it changes - `Renderer::setFogModel`
/// queues that for between frames, the way an MSAA change already is.
///
/// A process-wide value with a setter, the way
/// `pipeline::setBlockCompressionSupported` already is, and for the same
/// reason: it is decided once, by something that is not on the path that reads
/// it.
const ShaderFeatures& activeShaderFeatures();

/// Change it. Does not rebuild anything - the caller does that, between frames.
/// Returns whether the value actually moved, so a caller can skip the rebuild.
bool setActiveShaderFeatures(const ShaderFeatures& features);

}  // namespace rendering
}  // namespace wowee

namespace std {
template <>
struct hash<wowee::rendering::ShaderFeatures> {
    size_t operator()(const wowee::rendering::ShaderFeatures& f) const noexcept {
        return std::hash<uint64_t>{}(f.key());
    }
};
}  // namespace std
