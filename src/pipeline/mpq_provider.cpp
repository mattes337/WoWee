#include "pipeline/mpq_provider.hpp"

#include "core/logger.hpp"

#ifdef WOWEE_HAVE_STORMLIB
// The library is named on the link line by CMake. Left to itself StormLib.h
// asks MSVC to link one of eight names built from the CRT and character-set
// flags - StormLibRAS.lib and friends - and a StormLib that is not spelled
// that way fails to link for a reason that names a file nobody has.
#define STORMLIB_NO_AUTO_LINK
#include <StormLib.h>

// Paths are passed as bytes. StormLib takes TCHAR, which is char here and
// wchar_t in a UNICODE build; every archive path would then be read as
// garbage. Failing at compile time beats an archive that will not open.
static_assert(sizeof(TCHAR) == sizeof(char),
              "wowee passes archive paths to StormLib as narrow strings; "
              "this build makes TCHAR wide");
#endif

namespace wowee {
namespace pipeline {

MpqProvider::MpqProvider() = default;

MpqProvider::~MpqProvider() {
    close();
}

#ifdef WOWEE_HAVE_STORMLIB

bool MpqProvider::isSupported() { return true; }

bool MpqProvider::open(const std::vector<std::string>& archives) {
    close();

    // Highest priority first, so the first archive holding a name wins.
    for (auto it = archives.rbegin(); it != archives.rend(); ++it) {
        HANDLE handle = nullptr;
        if (!SFileOpenArchive(it->c_str(), 0,
                              MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_ATTRIBUTES, &handle)) {
            LOG_WARNING("MpqProvider: cannot open archive: ", *it);
            continue;
        }
        auto archive = std::make_unique<Archive>();
        archive->path = *it;
        archive->handle = handle;
        archives_.push_back(std::move(archive));
    }

    if (archives_.empty()) {
        LOG_ERROR("MpqProvider: no archives opened from ", archives.size(), " candidate(s)");
        return false;
    }
    LOG_INFO("MpqProvider: opened ", archives_.size(), " archive(s), highest priority: ",
             archives_.front()->path);
    return true;
}

void MpqProvider::close() {
    for (auto& archive : archives_) {
        if (archive->handle) {
            SFileCloseArchive(static_cast<HANDLE>(archive->handle));
            archive->handle = nullptr;
        }
    }
    archives_.clear();
}

int MpqProvider::findArchive(const std::string& path) const {
    if (path.empty()) return -1;
    for (size_t i = 0; i < archives_.size(); ++i) {
        const Archive& archive = *archives_[i];
        std::lock_guard<std::mutex> lock(archive.mutex);
        HANDLE file = nullptr;
        if (!SFileOpenFileEx(static_cast<HANDLE>(archive.handle), path.c_str(),
                             SFILE_OPEN_FROM_MPQ, &file)) {
            continue;
        }
        DWORD flags = 0;
        const bool deleted =
            SFileGetFileInfo(file, SFileInfoFlags, &flags, sizeof(flags), nullptr) &&
            (flags & MPQ_FILE_DELETE_MARKER) != 0;
        SFileCloseFile(file);
        // A patch that deletes a file is answering for it: the search ends
        // here rather than falling through to the copy it was meant to remove.
        return deleted ? -1 : static_cast<int>(i);
    }
    return -1;
}

bool MpqProvider::exists(const std::string& path) const {
    return findArchive(path) >= 0;
}

std::string MpqProvider::sourceOf(const std::string& path) const {
    const int index = findArchive(path);
    return index >= 0 ? archives_[static_cast<size_t>(index)]->path : std::string{};
}

std::vector<uint8_t> MpqProvider::read(const std::string& path) const {
    const int index = findArchive(path);
    if (index < 0) return {};

    const Archive& archive = *archives_[static_cast<size_t>(index)];
    std::lock_guard<std::mutex> lock(archive.mutex);

    HANDLE file = nullptr;
    if (!SFileOpenFileEx(static_cast<HANDLE>(archive.handle), path.c_str(),
                         SFILE_OPEN_FROM_MPQ, &file)) {
        return {};
    }

    DWORD sizeHigh = 0;
    const DWORD size = SFileGetFileSize(file, &sizeHigh);
    if (size == SFILE_INVALID_SIZE || sizeHigh != 0) {
        // A >4 GB member cannot occur in any client wowee reads, and a size
        // this call could not determine is not one to allocate against.
        SFileCloseFile(file);
        return {};
    }
    if (size == 0) {
        SFileCloseFile(file);
        return {};
    }

    std::vector<uint8_t> data(size);
    DWORD bytesRead = 0;
    const bool ok = SFileReadFile(file, data.data(), size, &bytesRead, nullptr);
    SFileCloseFile(file);
    if (!ok || bytesRead != size) {
        LOG_WARNING("MpqProvider: short read of '", path, "' from ", archive.path);
        return {};
    }
    return data;
}

#else  // WOWEE_HAVE_STORMLIB

bool MpqProvider::isSupported() { return false; }

bool MpqProvider::open(const std::vector<std::string>& archives) {
    if (!archives.empty()) {
        LOG_WARNING("MpqProvider: built without StormLib; ", archives.size(),
                    " game archive(s) cannot be read. Extract assets instead.");
    }
    return false;
}

void MpqProvider::close() { archives_.clear(); }

int MpqProvider::findArchive(const std::string&) const { return -1; }

bool MpqProvider::exists(const std::string&) const { return false; }

std::string MpqProvider::sourceOf(const std::string&) const { return {}; }

std::vector<uint8_t> MpqProvider::read(const std::string&) const { return {}; }

#endif  // WOWEE_HAVE_STORMLIB

}  // namespace pipeline
}  // namespace wowee
