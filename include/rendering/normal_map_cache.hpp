#pragma once

/**
 * normal_map_cache.hpp - one generated normal/height map per source texture,
 * made off the render thread and kept on disk between runs.
 *
 * The WMO and character renderers each generate their normal maps inline, on
 * whichever thread asked for the texture, and throw them away when the process
 * exits. That is affordable for a few hundred wall textures. It is not
 * affordable for the doodads: Elwynn alone loads several thousand M2 skins, and
 * a Sobel pass over each of them on the thread that is also recording the frame
 * is a stall per tree.
 *
 * So this: ask for a map by the source texture's key, get nothing back the
 * first time, and get the map on some later frame once a worker has made it.
 * Until then the caller binds the flat-normal fallback it already has, which is
 * the surface unbumped - the first frames of a newly seen model are flat and
 * refine, and the settings tooltip says so rather than leaving a player to
 * wonder.
 *
 * The generated pixels are also written next to the client's data, so the
 * second run of the same zone reads them instead of deriving them again.
 *
 * RESERVED(phase-07, M1-texture-cache): the directory layout and the hash that
 * names a file here are what the generated-asset manifest will index. It is
 * manifest-free until then - a file is found by hashing the source bytes, not
 * by being listed anywhere - and phase 07 replaces the directory scan in
 * initialize() with a read of that manifest.
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "pipeline/blp_loader.hpp"

namespace wowee {
namespace rendering {

class VkContext;
class VkTexture;

class NormalMapCache {
public:
    NormalMapCache();
    ~NormalMapCache();

    NormalMapCache(const NormalMapCache&) = delete;
    NormalMapCache& operator=(const NormalMapCache&) = delete;

    /// @param cacheDir  where the .rgba sidecars live, normally
    ///                  `<data>/generated/<expansion>/normals`. An empty or
    ///                  unwritable directory only costs the disk half: maps are
    ///                  still generated, they are just made again next run.
    void initialize(VkContext* ctx, const std::string& cacheDir);
    void shutdown();

    /// A map that has been generated and uploaded.
    struct Ready {
        VkTexture* texture = nullptr;
        /// How much height the source had, as generateNormalHeightMap() reports
        /// it. Below kMinVariance the map is dropped rather than bound - the
        /// same threshold the WMO path's shaders apply to `heightMapVariance`,
        /// so a flat texture costs no fetch on any surface.
        float variance = 0.0f;
    };

    /// The map for `key`, or nullptr while it is pending, rejected or unknown.
    [[nodiscard]] const Ready* lookup(const std::string& key) const;

    /// Ask for a map, once. Later calls with the same key do nothing, whatever
    /// they pass, so a caller may call it unconditionally.
    ///
    /// `source` is copied because the work happens on a worker: the caller's
    /// copy goes to the GPU and is freed on the render thread's own schedule.
    /// The worker decodes the base level, downsamples it to at most
    /// kMaxSourceSide on each axis, and runs the Sobel at `strength`.
    void request(const std::string& key, const pipeline::BLPImage& source, float strength);

    /// Upload what the workers finished, on the render thread, bounded.
    ///
    /// The keys that became readable are appended to @p outReadyKeys, because
    /// the descriptors that named them have to be written again and only the
    /// caller knows which sets those are. Returns how many.
    uint32_t pump(std::vector<std::string>* outReadyKeys, uint32_t maxUploads = 8);

    /// Bumped by every upload, so a renderer can hold one number rather than a
    /// set of keys.
    [[nodiscard]] uint64_t generation() const { return generation_; }

    /// Bytes of GPU memory the uploaded maps occupy, for the memory monitor.
    [[nodiscard]] uint64_t uploadedBytes() const { return uploadedBytes_; }
    [[nodiscard]] uint32_t uploadedCount() const { return uploadedCount_; }
    [[nodiscard]] uint32_t diskHits() const { return diskHits_; }
    [[nodiscard]] uint32_t generatedCount() const { return generatedCount_; }

    /// A source texture no bigger than this on either axis is used as it is;
    /// anything larger is box-downsampled to it first. A normal map is a
    /// low-frequency statement about a surface, and 512 of them across a wall
    /// is already more than a screen shows.
    static constexpr uint32_t kMaxSourceSide = 512;
    /// Height variance under which a map is not worth binding. The WMO shaders
    /// gate POM on `heightMapVariance > 0.001`; the same number decides here
    /// whether the map exists at all.
    static constexpr float kMinVariance = 0.001f;
    /// The disk cache's ceiling. Oldest files go first when it is over.
    static constexpr uint64_t kMaxCacheBytes = 2ull * 1024 * 1024 * 1024;

    /// The name a source's bytes get on disk, without the directory or the
    /// extension.
    ///
    /// FNV-1a over the source bytes with the Sobel strength folded in: the same
    /// texture at strength 2 and at strength 3 are two different maps and must
    /// not share a file. Not a cryptographic hash and does not need to be - a
    /// collision costs one doodad the wrong bumps, and the alternative is a
    /// dependency for a file name.
    ///
    /// Inline, and so is downsampleToCap below, because the whole point of both
    /// is that they are pure: a test reaches them by including this header,
    /// without a Vulkan context or a thread pool.
    ///
    /// RESERVED(phase-07, M1-texture-cache): this is the name the
    /// generated-asset manifest will index a normal map by, so it is stated
    /// once and tested rather than spelled out at each call site.
    [[nodiscard]] static std::string hashName(const uint8_t* bytes, size_t byteCount,
                                              float strength) {
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < byteCount; ++i) {
            h ^= static_cast<uint64_t>(bytes[i]);
            h *= 1099511628211ull;
        }
        uint32_t strengthBits = 0;
        std::memcpy(&strengthBits, &strength, sizeof(strengthBits));
        h ^= static_cast<uint64_t>(strengthBits);
        h *= 1099511628211ull;

        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
        return std::string(buf);
    }

    /// The sidecar's 8-byte header, then width*height*4 bytes of RGBA.
    struct SidecarHeader {
        uint16_t width = 0;
        uint16_t height = 0;
        float variance = 0.0f;
    };
    static_assert(sizeof(SidecarHeader) == 8, "the sidecar's header is eight bytes");

    /// Box-downsample RGBA8 to at most kMaxSourceSide on each axis; a no-op when
    /// the source already fits, and empty when the input does not describe an
    /// image of the size it claims.
    ///
    /// Halving rather than resampling to an arbitrary size keeps it a box filter
    /// over whole texels, which is what a height field wants: an interpolating
    /// filter invents gradients the source does not have, and a gradient is the
    /// entire content of the result.
    static std::vector<uint8_t> downsampleToCap(const std::vector<uint8_t>& rgba,
                                                uint32_t width, uint32_t height,
                                                uint32_t& outWidth, uint32_t& outHeight) {
        outWidth = width;
        outHeight = height;
        if (width == 0 || height == 0) return {};
        if (rgba.size() < static_cast<size_t>(width) * height * 4) return {};
        if (width <= kMaxSourceSide && height <= kMaxSourceSide) return rgba;

        std::vector<uint8_t> src = rgba;
        uint32_t w = width, h = height;
        while (w > kMaxSourceSide || h > kMaxSourceSide) {
            const uint32_t nw = std::max(1u, w / 2);
            const uint32_t nh = std::max(1u, h / 2);
            std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
            for (uint32_t y = 0; y < nh; ++y) {
                for (uint32_t x = 0; x < nw; ++x) {
                    const uint32_t x0 = std::min(x * 2, w - 1);
                    const uint32_t x1 = std::min(x * 2 + 1, w - 1);
                    const uint32_t y0 = std::min(y * 2, h - 1);
                    const uint32_t y1 = std::min(y * 2 + 1, h - 1);
                    for (uint32_t c = 0; c < 4; ++c) {
                        const uint32_t sum =
                            src[(static_cast<size_t>(y0) * w + x0) * 4 + c] +
                            src[(static_cast<size_t>(y0) * w + x1) * 4 + c] +
                            src[(static_cast<size_t>(y1) * w + x0) * 4 + c] +
                            src[(static_cast<size_t>(y1) * w + x1) * 4 + c];
                        dst[(static_cast<size_t>(y) * nw + x) * 4 + c] =
                            static_cast<uint8_t>((sum + 2) / 4);
                    }
                }
            }
            src = std::move(dst);
            w = nw;
            h = nh;
        }
        outWidth = w;
        outHeight = h;
        return src;
    }

private:
    struct Pending {
        std::string key;
        std::future<void> job;
        // Filled by the worker, read by pump() once the future is ready.
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        float variance = 0.0f;
        bool fromDisk = false;
    };

    void enforceDiskBudget();

    VkContext* vkCtx_ = nullptr;
    std::string cacheDir_;
    bool diskUsable_ = false;

    std::unordered_map<std::string, Ready> ready_;
    std::unordered_map<std::string, std::unique_ptr<VkTexture>> owned_;
    std::deque<std::unique_ptr<Pending>> pending_;
    std::unordered_map<std::string, bool> requested_;

    std::atomic<uint64_t> generation_{0};
    uint64_t uploadedBytes_ = 0;
    uint32_t uploadedCount_ = 0;
    uint32_t diskHits_ = 0;
    uint32_t generatedCount_ = 0;
};

}  // namespace rendering
}  // namespace wowee
