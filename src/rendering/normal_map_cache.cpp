#include "rendering/normal_map_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <exception>
#include <fstream>
#include <utility>

#include "core/logger.hpp"
#include "core/thread_pool.hpp"
#include "rendering/normal_map.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_texture.hpp"

namespace wowee {
namespace rendering {

namespace fs = std::filesystem;

NormalMapCache::NormalMapCache() = default;
NormalMapCache::~NormalMapCache() = default;

void NormalMapCache::initialize(VkContext* ctx, const std::string& cacheDir) {
    vkCtx_ = ctx;
    cacheDir_ = cacheDir;
    diskUsable_ = false;
    if (cacheDir_.empty()) return;

    std::error_code ec;
    fs::create_directories(fs::u8path(cacheDir_), ec);
    if (ec) {
        LOG_WARNING("NormalMapCache: cannot create ", cacheDir_, " (", ec.message(),
                    "); maps will be generated every run");
        return;
    }
    diskUsable_ = true;
    enforceDiskBudget();
}

void NormalMapCache::enforceDiskBudget() {
    if (!diskUsable_) return;
    std::error_code ec;

    struct Aged {
        fs::path path;
        fs::file_time_type when;
        uint64_t bytes;
    };
    std::vector<Aged> files;
    uint64_t total = 0;
    for (const auto& entry : fs::directory_iterator(fs::u8path(cacheDir_), ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const uint64_t size = static_cast<uint64_t>(entry.file_size(ec));
        if (ec) { ec.clear(); continue; }
        files.push_back({entry.path(), entry.last_write_time(ec), size});
        total += size;
    }
    if (total <= kMaxCacheBytes) return;

    // Oldest first, which is the only order available: nothing records when a
    // map was last read, and a file system's access time is off by default on
    // both platforms this runs on.
    std::sort(files.begin(), files.end(),
              [](const Aged& a, const Aged& b) { return a.when < b.when; });
    uint64_t removed = 0;
    for (const auto& f : files) {
        if (total - removed <= kMaxCacheBytes) break;
        std::error_code rmEc;
        if (fs::remove(f.path, rmEc)) removed += f.bytes;
    }
    LOG_INFO("NormalMapCache: disk cache was ", total / (1024 * 1024), " MB, removed ",
             removed / (1024 * 1024), " MB of the oldest to stay under ",
             kMaxCacheBytes / (1024 * 1024), " MB");
}

void NormalMapCache::shutdown() {
    // Every worker has to finish before its Pending is destroyed - the job holds
    // a pointer into it.
    for (auto& p : pending_) {
        if (p && p->job.valid()) p->job.wait();
    }
    pending_.clear();
    requested_.clear();
    ready_.clear();
    if (vkCtx_) {
        VkDevice device = vkCtx_->getDevice();
        VmaAllocator alloc = vkCtx_->getAllocator();
        for (auto& [key, tex] : owned_) {
            if (tex) tex->destroy(device, alloc);
        }
    }
    owned_.clear();
    uploadedBytes_ = 0;
    uploadedCount_ = 0;
    vkCtx_ = nullptr;
}

const NormalMapCache::Ready* NormalMapCache::lookup(const std::string& key) const {
    auto it = ready_.find(key);
    return it == ready_.end() ? nullptr : &it->second;
}

void NormalMapCache::request(const std::string& key, const pipeline::BLPImage& source,
                             float strength) {
    if (key.empty() || !vkCtx_) return;
    if (requested_.find(key) != requested_.end()) return;
    if (!source.isValid()) return;
    requested_[key] = true;

    // The bytes the hash is taken over: whatever form the loader kept, which is
    // the file's own content either way. Hashing the decoded pixels instead
    // would mean decoding before knowing whether the answer is already on disk,
    // which is most of the work this is avoiding.
    const std::vector<uint8_t>& hashSource =
        source.isBlockCompressed() ? source.mipmaps.front() : source.data;
    const std::string name = hashName(hashSource.data(), hashSource.size(), strength);
    const std::string path = diskUsable_ ? (cacheDir_ + "/" + name + ".rgba") : std::string();

    auto entry = std::make_unique<Pending>();
    entry->key = key;
    Pending* raw = entry.get();

    // A copy, because the caller's BLP is uploaded and freed on the render
    // thread's own schedule and this outlives that.
    pipeline::BLPImage sourceCopy = source;

    raw->job = core::ThreadPool::frameWorkers().submit(
        [raw, path, strength, src = std::move(sourceCopy)]() mutable {
            // Disk first. A hit is a file read and nothing else - no decode, no
            // blur, no Sobel - which is the whole reason the sidecar exists.
            if (!path.empty()) {
                std::ifstream in(path, std::ios::binary);
                if (in) {
                    SidecarHeader header{};
                    in.read(reinterpret_cast<char*>(&header), sizeof(header));
                    const size_t expect =
                        static_cast<size_t>(header.width) * header.height * 4;
                    if (in && header.width > 0 && header.height > 0 && expect > 0) {
                        std::vector<uint8_t> pixels(expect);
                        in.read(reinterpret_cast<char*>(pixels.data()),
                                static_cast<std::streamsize>(expect));
                        if (in.gcount() == static_cast<std::streamsize>(expect)) {
                            raw->pixels = std::move(pixels);
                            raw->width = header.width;
                            raw->height = header.height;
                            raw->variance = header.variance;
                            raw->fromDisk = true;
                            return;
                        }
                    }
                }
            }

            std::vector<uint8_t> decoded =
                src.isBlockCompressed() ? pipeline::BLPLoader::decodeBaseLevel(src) : src.data;
            if (decoded.empty()) return;

            uint32_t w = 0, h = 0;
            std::vector<uint8_t> capped =
                downsampleToCap(decoded, static_cast<uint32_t>(src.width),
                                static_cast<uint32_t>(src.height), w, h);
            if (capped.empty() || w == 0 || h == 0) return;

            float variance = 0.0f;
            std::vector<uint8_t> map =
                generateNormalHeightMap(capped.data(), w, h, strength, variance);
            if (map.empty()) return;

            raw->pixels = std::move(map);
            raw->width = w;
            raw->height = h;
            raw->variance = variance;

            // Written whatever the variance says, including for a texture whose
            // map will be thrown away: the next run would otherwise pay the
            // Sobel again to reach the same verdict.
            if (!path.empty() && raw->variance >= 0.0f) {
                SidecarHeader header{static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                                     variance};
                const std::string tmp = path + ".tmp";
                {
                    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                    if (out) {
                        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
                        out.write(reinterpret_cast<const char*>(raw->pixels.data()),
                                  static_cast<std::streamsize>(raw->pixels.size()));
                    }
                }
                // Renamed into place so a half-written file is never read as a
                // whole one by another process - or by this one after a crash.
                std::error_code ec;
                fs::rename(fs::u8path(tmp), fs::u8path(path), ec);
                if (ec) fs::remove(fs::u8path(tmp), ec);
            }
        });

    pending_.push_back(std::move(entry));
}

uint32_t NormalMapCache::pump(std::vector<std::string>* outReadyKeys, uint32_t maxUploads) {
    if (!vkCtx_ || pending_.empty()) return 0;
    uint32_t uploaded = 0;

    for (auto it = pending_.begin(); it != pending_.end() && uploaded < maxUploads;) {
        Pending* p = it->get();
        if (!p || !p->job.valid()) { it = pending_.erase(it); continue; }
        if (p->job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        // get() rethrows whatever the worker threw, on this thread, which is
        // the one recording the frame. The only thing it can throw is
        // bad_alloc out of one of the three vectors, and the answer to that is
        // this texture gets no map - not that the client goes down holding a
        // perfectly good frame.
        try {
            p->job.get();
        } catch (const std::exception& e) {
            LOG_WARNING("NormalMapCache: generating the map for ", p->key, " threw: ", e.what());
            p->pixels.clear();
        } catch (...) {
            LOG_WARNING("NormalMapCache: generating the map for ", p->key, " threw");
            p->pixels.clear();
        }

        if (p->pixels.empty() || p->variance < kMinVariance) {
            // Nothing worth binding. Recorded as ready-with-no-texture so the
            // caller stops asking; `lookup` answers null for it, which is the
            // flat fallback.
            ready_[p->key] = Ready{nullptr, p->variance};
            if (outReadyKeys) outReadyKeys->push_back(p->key);
            it = pending_.erase(it);
            ++uploaded;
            generation_.fetch_add(1);
            continue;
        }

        auto tex = std::make_unique<VkTexture>();
        if (tex->upload(*vkCtx_, p->pixels.data(), p->width, p->height,
                        VK_FORMAT_R8G8B8A8_UNORM, true)) {
            tex->createSampler(vkCtx_->getDevice(), VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                               VK_SAMPLER_ADDRESS_MODE_REPEAT);
        }
        if (!tex->isValid()) {
            ready_[p->key] = Ready{nullptr, 0.0f};
        } else {
            const size_t bytes = static_cast<size_t>(p->width) * p->height * 4;
            uploadedBytes_ += bytes + bytes / 3;  // plus the generated mip chain
            ++uploadedCount_;
            if (p->fromDisk) ++diskHits_; else ++generatedCount_;
            ready_[p->key] = Ready{tex.get(), p->variance};
            owned_[p->key] = std::move(tex);
        }
        if (outReadyKeys) outReadyKeys->push_back(p->key);
        it = pending_.erase(it);
        ++uploaded;
        generation_.fetch_add(1);

        // Said out loud every so often, because nothing else in this client
        // reports video memory and the phase file asks what the maps cost.
        if (uploadedCount_ > 0 && uploadedCount_ % 256 == 0) {
            LOG_INFO("NormalMapCache: ", uploadedCount_, " maps bound, ",
                     uploadedBytes_ / (1024 * 1024), " MB of them; ", diskHits_,
                     " read back from disk, ", generatedCount_, " derived, ",
                     pending_.size(), " still being made");
        }
    }
    return uploaded;
}

}  // namespace rendering
}  // namespace wowee
