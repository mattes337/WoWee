#pragma once

#include "rendering/normal_map_cache.hpp"
#include <cmath>

#include "rendering/vk_shader.hpp"
#include "rendering/shadow_params.hpp"

#include "pipeline/terrain_mesh.hpp"
#include "pipeline/blp_loader.hpp"
#include "rendering/camera.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>

namespace wowee {

// Forward declarations
namespace pipeline { class AssetManager; }

namespace rendering {

class VkContext;
class VkTexture;
class Frustum;

/**
 * GPU-side terrain chunk data (Vulkan)
 */
struct TerrainChunkGPU {
    ::VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAlloc = VK_NULL_HANDLE;
    ::VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAlloc = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    // Material descriptor set (set 1: 7 samplers + params UBO)
    VkDescriptorSet materialSet = VK_NULL_HANDLE;

    // Per-chunk params UBO (hasLayer1/2/3)
    ::VkBuffer paramsUBO = VK_NULL_HANDLE;
    VmaAllocation paramsAlloc = VK_NULL_HANDLE;

    // Texture handles (owned by cache, NOT destroyed per-chunk)
    VkTexture* baseTexture = nullptr;
    VkTexture* layerTextures[3] = {nullptr, nullptr, nullptr};
    VkTexture* alphaTextures[3] = {nullptr, nullptr, nullptr};
    int layerCount = 0;

    /// The cache keys of the four blended textures, so the chunk can be told
    /// which generated normal maps are its own once they exist. Empty for a
    /// layer the chunk does not have.
    std::string normalMapKeys[4];
    /// True until every one of those keys has been answered - with a map or
    /// with "this texture has no height in it". While it is set the chunk is
    /// revisited each frame; once clear it is never looked at again.
    bool normalMapsPending = false;
    /// One bit per layer whose map is bound, mirroring TerrainParams.
    int normalMapMask = 0;

    // Per-chunk alpha textures (owned by this chunk, destroyed on removal)
    std::vector<std::unique_ptr<VkTexture>> ownedAlphaTextures;

    // World position for culling
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;

    // Owning tile coordinates (for per-tile removal)
    int tileX = -1, tileY = -1;

    // Bounding sphere for frustum culling
    float boundingSphereRadius = 0.0f;
    glm::vec3 boundingSphereCenter = glm::vec3(0.0f);

    // Offsets into mega buffers for indirect drawing (-1 = not in mega buffer)
    int32_t megaBaseVertex = -1;
    uint32_t megaFirstIndex = 0;
    uint32_t vertexCount = 0;

    [[nodiscard]] bool isValid() const { return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE; }
};

/**
 * Terrain renderer (Vulkan)
 */
class TerrainRenderer {
public:
    TerrainRenderer();
    ~TerrainRenderer();

    /**
     * Initialize terrain renderer
     * @param ctx Vulkan context
     * @param perFrameLayout Descriptor set layout for set 0 (per-frame UBO)
     * @param assetManager Asset manager for loading textures
     */
    bool initialize(VkContext* ctx, VkDescriptorSetLayout perFrameLayout,
                    pipeline::AssetManager* assetManager);

    void shutdown();

    bool loadTerrain(const pipeline::TerrainMesh& mesh,
                     const std::vector<std::string>& texturePaths,
                     int tileX = -1, int tileY = -1);


    /// Upload a batch of terrain chunks incrementally. Returns true when all chunks done.
    /// chunkIndex is updated to the next chunk to process (0-255 row-major).
    bool loadTerrainIncremental(const pipeline::TerrainMesh& mesh,
                                const std::vector<std::string>& texturePaths,
                                int tileX, int tileY,
                                int& chunkIndex, int maxChunksPerCall = 16);

    void removeTile(int tileX, int tileY);

    void uploadPreloadedTextures(const std::unordered_map<std::string, pipeline::BLPImage>& textures);

    /**
     * Render terrain
     * @param cmd Command buffer to record into
     * @param perFrameSet Per-frame descriptor set (set 0)
     * @param camera Camera for frustum culling
     */
    void render(VkCommandBuffer cmd, VkDescriptorSet perFrameSet, const Camera& camera);

    /**
     * Initialize terrain shadow pipeline (must be called after initialize()).
     * @param shadowRenderPass  Depth-only render pass used for the shadow map.
     */
    bool initializeShadow(VkRenderPass shadowRenderPass);

    /**
     * Render terrain into the shadow depth map.
     * @param cmd               Command buffer (inside shadow render pass).
     * @param lightSpaceMatrix  Orthographic light-space transform.
     * @param shadowCenter      World-space centre of shadow coverage.
     * @param shadowRadius      Cull radius around shadowCenter.
     */
    void renderShadow(VkCommandBuffer cmd, const glm::mat4& lightSpaceMatrix,
                      const glm::vec3& shadowCenter, float shadowRadius);

    [[nodiscard]] bool hasShadowPipeline() const { return shadowPipeline_ != VK_NULL_HANDLE; }

    void clear();

    void recreatePipelines();
    /// The fill pipeline and its wireframe derivative, which
    /// initialize() and recreatePipelines() both need.
    bool buildMainPassPipelines(VkDevice device,
                                wowee::rendering::VkShaderModule& vertShader,
                                wowee::rendering::VkShaderModule& fragShader);

    void setWireframe(bool enabled) { wireframe = enabled; }
    void setFrustumCulling(bool enabled) { frustumCullingEnabled = enabled; }
    void setFogEnabled(bool enabled) { fogEnabled = enabled; }
    [[nodiscard]] bool isFogEnabled() const { return fogEnabled; }
    void setViewDistance(float distance) { maxViewDistance_ = std::clamp(distance, 400.0f, 2400.0f); }

    /// How far the ground is allowed to drop in detail: 0 draws every chunk at
    /// the 145-vertex mesh the client always drew, 3 lets the furthest ones
    /// fall to nine.
    ///
    /// A chunk's level is decided per frame from its distance, so nothing is
    /// re-uploaded when this changes - the shared index sets are all resident
    /// and the draw picks one. Two chunks that touch can never be more than one
    /// level apart, because the thresholds are 12, 30 and 60 percent of the
    /// view distance and the smallest gap between two of them is far wider than
    /// the 47 yards that separate two neighbours' distances at the worst angle.
    void setTerrainLodLevel(int level) { terrainLodLevel_ = std::clamp(level, 0, 3); }

    // ---- M3a: the ground's generated normal maps ----
    /// Where the sidecars live, and whether the ground asks for maps at all.
    /// Called once at start-up, before any tile streams in.
    void initializeNormalMapCache(const std::string& cacheDir);
    /// `normalmapscope`: off is what the client did, and off means no map is
    /// ever asked for, so no worker and no disk is touched.
    void setNormalMapsEnabled(bool enabled) { normalMapsEnabled_ = enabled; }
    void setNormalMapStrength(float strength);
    [[nodiscard]] const NormalMapCache& normalMapCache() const { return normalMapCache_; }
    /// Upload what the workers finished and rewrite the chunks that were
    /// waiting on it. Main thread, once a frame, before anything is recorded.
    void applyReadyNormalMaps();
    [[nodiscard]] int getTerrainLodLevel() const { return terrainLodLevel_; }

    void setShadowMap(VkDescriptorImageInfo /*depthInfo*/, const glm::mat4& /*lightSpaceMat*/) {}
    void clearShadowMap() {}

    [[nodiscard]] int getChunkCount() const { return static_cast<int>(chunks.size()); }
    [[nodiscard]] int getRenderedChunkCount() const { return renderedChunks; }
    /// How far the furthest chunk drawn this frame was, for the diagnostic that
    /// compares it against the doodads. See Renderer::logViewDistanceDiag.
    [[nodiscard]] float getFurthestDrawnDistance() const { return std::sqrt(furthestDrawnSq_); }
    [[nodiscard]] int getCulledChunkCount() const { return culledChunks; }
    [[nodiscard]] int getTriangleCount() const;
    [[nodiscard]] VkContext* getVkContext() const { return vkCtx; }

private:
    TerrainChunkGPU uploadChunk(const pipeline::ChunkMesh& chunk);
    VkTexture* loadTexture(const std::string& path);
    VkTexture* createAlphaTexture(const std::vector<uint8_t>& alphaData);
    bool isChunkVisible(const TerrainChunkGPU& chunk, const Frustum& frustum);
    void calculateBoundingSphere(TerrainChunkGPU& chunk, const pipeline::ChunkMesh& meshChunk);
    VkDescriptorSet allocateMaterialSet();
    /// False when there is nothing sampleable to write, in which case the
    /// caller must drop the chunk: a descriptor set that was allocated and
    /// never written is as undefined to bind as one holding a null view.
    bool writeMaterialDescriptors(VkDescriptorSet set, const TerrainChunkGPU& chunk);
    /// Whether every one of a chunk's four keys has been answered.
    [[nodiscard]] bool chunkNormalMapsResolved(const TerrainChunkGPU& chunk) const;
    /// Refresh a chunk's params UBO from its current normalMapMask.
    void writeChunkNormalMapParams(TerrainChunkGPU& chunk);
    void destroyChunkGPU(TerrainChunkGPU& chunk);

    /// Point a chunk's base, layer and alpha textures at the loaded ones.
    ///
    /// Both load paths do this identically; chunkX and chunkY only name the
    /// chunk in the warnings. A layer whose textureId is past the end of the
    /// tile's texture list falls back to white rather than going unbound.
    void bindChunkTextures(TerrainChunkGPU& gpuChunk,
                           const pipeline::ChunkMesh& chunk,
                           const std::vector<std::string>& texturePaths,
                           int tileX, int tileY, int chunkX, int chunkY);

    /// Allocate and fill a chunk's params UBO. False means the allocation
    /// failed, and the two callers answer that differently: the one-shot load
    /// skips the chunk, the incremental one hands it back to be retried.
    bool createChunkParamsUBO(TerrainChunkGPU& gpuChunk);

    VkContext* vkCtx = nullptr;
    pipeline::AssetManager* assetManager = nullptr;

    // Main pipelines
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;

    // Shadow pipeline
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    /// The set the shadow pass binds. Five separate members before,
    /// built and torn down here and in three other renderers.
    ShadowParamsSet shadowParams_;

    // Descriptor pool for material sets. One set per terrain chunk, 256 chunks
    // to a tile, so this is a tile budget: 65536 covered 256 tiles, and tiles
    // are held to the *unload* radius, not the load radius - 8 loading and 11
    // unloading is 23×23 = 529 resident. Every tile past the 256th arrived
    // with no descriptor set and, before the retry in loadTerrainIncremental,
    // was dropped for good: terrain that simply was not there, in whichever
    // direction the player had travelled furthest.
    VkDescriptorPool materialDescPool = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_MATERIAL_SETS = 160 * 1024;  // 640 tiles

    // Loaded terrain chunks
    std::vector<TerrainChunkGPU> chunks;

    // Texture cache (path -> VkTexture)
    struct TextureCacheEntry {
        std::unique_ptr<VkTexture> texture;
        size_t approxBytes = 0;
        uint64_t lastUse = 0;
    };
    std::unordered_map<std::string, TextureCacheEntry> textureCache;
    size_t textureCacheBytes_ = 0;
    uint64_t textureCacheCounter_ = 0;
    size_t textureCacheBudgetBytes_ = 4096ull * 1024 * 1024;
    std::unordered_set<std::string> failedTextureCache_;
    std::unordered_set<std::string> loggedTextureLoadFails_;
    uint32_t textureBudgetRejectWarnings_ = 0;

    // Fallback textures
    std::unique_ptr<VkTexture> whiteTexture;
    std::unique_ptr<VkTexture> opaqueAlphaTexture;

    // Rendering state
    // ---- M3a ----
    NormalMapCache normalMapCache_;
    bool normalMapsEnabled_ = false;
    float normalMapStrength_ = 0.8f;
    /// 128,128,255,128 - the ground unperturbed - on bindings 8 to 11 of every
    /// chunk until that chunk's own layers have maps.
    std::unique_ptr<VkTexture> flatNormalTexture;
    std::vector<std::string> normalMapReadyScratch_;
    /// How many chunk material sets have been rewritten to name their generated
    /// maps. Counted inside the deferred callback for the reason the M2
    /// renderer's twin is: it is the only evidence that the deferral lands.
    uint32_t normalMapDescriptorWrites_ = 0;
    static std::string normalizeTextureKey(const std::string& path);

    bool wireframe = false;
    bool frustumCullingEnabled = true;
    bool fogEnabled = true;
    float maxViewDistance_ = 1200.0f;
    /// 0 = off, which is what the client always did.
    int terrainLodLevel_ = 0;
    /// The three reduced index sets, concatenated into one buffer that every
    /// chunk draws out of. They are chunk-local indices, so the draw shifts
    /// them with a vertex offset the way the mega buffer already does.
    VkBuffer lodIB_ = VK_NULL_HANDLE;
    VmaAllocation lodIBAlloc_ = VK_NULL_HANDLE;
    uint32_t lodFirstIndex_[4] = {0, 0, 0, 0};
    uint32_t lodIndexCount_[4] = {0, 0, 0, 0};
    /// Built on the first frame that asks for a reduced level, and kept.
    bool ensureLodIndexBuffer();
    int renderedChunks = 0;
    float furthestDrawnSq_ = 0.0f;
    int culledChunks = 0;

    // Mega vertex/index buffers for indirect drawing
    // All terrain chunks share a single VB + IB, eliminating per-chunk rebinds.
    // Indirect draw commands are built CPU-side each frame for visible chunks.
    VkBuffer megaVB_ = VK_NULL_HANDLE;
    VmaAllocation megaVBAlloc_ = VK_NULL_HANDLE;
    void* megaVBMapped_ = nullptr;
    VkBuffer megaIB_ = VK_NULL_HANDLE;
    VmaAllocation megaIBAlloc_ = VK_NULL_HANDLE;
    void* megaIBMapped_ = nullptr;
    uint32_t megaVBUsed_ = 0;  // vertices used
    uint32_t megaIBUsed_ = 0;  // indices used
    // ~1.5M verts x 44B ~ 64MB. A chunk is 177 vertices since the LOD skirt
    // ring was appended to it, not 145, so this holds about 8,800 chunks -
    // some 34 tiles - rather than 10,800. Past that a chunk binds its own
    // buffers and draws on its own, which is the fallback that was always
    // there; the cap is left where it is rather than grown by a fifth, because
    // it is a flat 64MB allocation on every machine and the tiles beyond it are
    // the far ones.
    static constexpr uint32_t MEGA_VB_MAX_VERTS   = 1536 * 1024;
    static constexpr uint32_t MEGA_IB_MAX_INDICES  = 6 * 1024 * 1024; // 6M indices × 4B = 24MB

    VkBuffer indirectBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indirectAlloc_ = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_INDIRECT_DRAWS = 8192;
};

} // namespace rendering
} // namespace wowee
