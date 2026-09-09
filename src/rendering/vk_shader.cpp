#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"
#include "core/logger.hpp"
#include "rendering/embedded_shaders.hpp"  // generated; see cmake/EmbedResources.cmake
#include <fstream>
#include <filesystem>
#include <cstring>
#include <cerrno>

namespace wowee {
namespace rendering {

namespace {

/// A shader path as the embedded table spells it: forward slashes, no leading
/// "./". Every call site already writes the key verbatim, so for them this is
/// the identity - it exists so a caller that built the path some other way
/// still finds the entry.
std::string normalizeShaderPath(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    while (path.rfind("./", 0) == 0) {
        path.erase(0, 2);
    }
    return path;
}

std::string pathLeaf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// The embedded copy of a shader, or nullptr if it was not built in.
const generated::EmbeddedResource* findEmbeddedShader(const std::string& path) {
    size_t count = 0;
    const generated::EmbeddedResource* table = generated::embeddedShaders(count);
    if (table == nullptr) return nullptr;

    const std::string key = normalizeShaderPath(path);
    for (size_t i = 0; i < count; ++i) {
        if (key == table[i].path) return &table[i];
    }
    // Second pass on the filename alone, so an absolute path - which is what
    // an installed build hands us when it resolves assets/ against its own
    // prefix - still reaches the entry. Shader names are unique within the
    // one flat directory, so there is nothing here to be ambiguous about.
    const std::string leaf = pathLeaf(key);
    for (size_t i = 0; i < count; ++i) {
        if (leaf == pathLeaf(table[i].path)) return &table[i];
    }
    return nullptr;
}

} // namespace

VkShaderModule::~VkShaderModule() {
    destroy();
}

VkShaderModule::VkShaderModule(VkShaderModule&& other) noexcept
    : device_(other.device_), module_(other.module_) {
    other.module_ = VK_NULL_HANDLE;
}

VkShaderModule& VkShaderModule::operator=(VkShaderModule&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        module_ = other.module_;
        other.module_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VkShaderModule::loadFromFile(VkDevice device, const std::string& path) {
    // Disk first, embedded second, and that order is deliberate: a developer
    // iterating on a shader edits the .glsl under assets/shaders, rebuilds the
    // .spv next to it, and expects that copy to win. The embedded table is the
    // fallback for a shipped binary - wowee.exe dropped beside the original
    // game executable with no assets/ directory copied along with it.
    std::vector<uint32_t> code;
    const uint32_t* words = nullptr;
    size_t sizeBytes = 0;

    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (file.is_open()) {
        const size_t fileSize = static_cast<size_t>(file.tellg());
        // SPIR-V is a stream of 32-bit words - file size must be a multiple of 4.
        // No fallback to the embedded copy here: a file that is present and
        // broken is a thing to fix, not to paper over.
        if (fileSize == 0 || fileSize % 4 != 0) {
            LOG_ERROR("Invalid SPIR-V file size (", fileSize, "): ", path);
            return false;
        }

        code.resize(fileSize / sizeof(uint32_t));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), fileSize);
        file.close();
        words = code.data();
        sizeBytes = fileSize;
    } else if (const generated::EmbeddedResource* res = findEmbeddedShader(path)) {
        words = res->words;
        sizeBytes = res->sizeBytes;
    } else {
        // A relative shader path resolves against the working directory, which
        // is the process's and not this file's to assume. Naming it, and what
        // the system said, turns "failed to open" into something actionable.
        std::error_code ec;
        const std::string cwd = std::filesystem::current_path(ec).string();
        LOG_ERROR("Failed to open shader file: ", path, " (", std::strerror(errno),
                  "; working directory ", ec ? "unknown" : cwd,
                  "; no embedded copy either)");
        return false;
    }

    if (!loadFromMemory(device, words, sizeBytes)) return false;

    // Named with the file it came from, so a shader module in the
    // vkDestroyDevice leak report identifies its subsystem rather than being
    // a bare handle. Nothing without validation: the naming call is a no-op
    // when VK_EXT_debug_utils is absent.
    {
        const std::string leaf = pathLeaf(path);
        setObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE,
                      reinterpret_cast<uint64_t>(module_), leaf.c_str());
        // And said in our own log, because the validation layer's leak report
        // prints bare handles whether or not the object carries a name - which
        // is what two rounds of naming just established. Matching a handle from
        // that report against these lines is what actually identifies it.
        if (isObjectNamingActive()) {
            LOG_INFO("shader module ", reinterpret_cast<const void*>(module_), " = ", leaf);
        }
    }
    return true;
}

bool VkShaderModule::loadFromMemory(VkDevice device, const uint32_t* code, size_t sizeBytes) {
    destroy();
    device_ = device;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = sizeBytes;
    createInfo.pCode = code;

    if (vkCreateShaderModule(device_, &createInfo, nullptr, &module_) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return false;
    }

    return true;
}

void VkShaderModule::destroy() {
    if (module_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
}

VkPipelineShaderStageCreateInfo VkShaderModule::stageInfo(
    VkShaderStageFlagBits stage, const char* entryPoint) const
{
    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = stage;
    info.module = module_;
    info.pName = entryPoint;
    return info;
}



ShaderPair loadShaderPair(VkDevice device, const std::string& vertPath,
                          const std::string& fragPath, const char* what) {
    ShaderPair pair;
    if (!pair.vert.loadFromFile(device, vertPath)) {
        LOG_ERROR("Failed to load ", what, " vertex shader: ", vertPath);
        return pair;
    }
    if (!pair.frag.loadFromFile(device, fragPath)) {
        LOG_ERROR("Failed to load ", what, " fragment shader: ", fragPath);
        return pair;
    }
    pair.vertStage = pair.vert.stageInfo(VK_SHADER_STAGE_VERTEX_BIT);
    pair.fragStage = pair.frag.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT);
    return pair;
}

} // namespace rendering
} // namespace wowee
