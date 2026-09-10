#include "rendering/shader_features.hpp"

#include "core/logger.hpp"

namespace wowee {
namespace rendering {

namespace {
ShaderFeatures& mutableActive() {
    // Default-constructed is the pre-phase-01 client. Nothing has to set this
    // for the client to look the way it always did, which is the point.
    static ShaderFeatures active;
    return active;
}
}  // namespace

const ShaderFeatures& activeShaderFeatures() { return mutableActive(); }

bool setActiveShaderFeatures(const ShaderFeatures& features) {
    if (mutableActive() == features) return false;
    mutableActive() = features;
    LOG_INFO("Shader variant: bits 0x", std::hex, features.bits, std::dec,
             ", cascades ", features.shadowCascades,
             ", shadow filter ", features.shadowFilter,
             ", fog model ", features.fogModel);
    return true;
}

}  // namespace rendering
}  // namespace wowee
