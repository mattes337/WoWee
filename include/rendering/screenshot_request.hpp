#pragma once
#include <string>
#include <utility>

namespace wowee::rendering {
enum class ScreenshotResult { Idle, Pending, Succeeded, Failed, Cancelled };

// Render-thread only. Skipped frames leave Pending untouched.
class ScreenshotRequest {
public:
    bool queue(std::string path) {
        if (path.empty() || result_ == ScreenshotResult::Pending) return false;
        path_ = std::move(path);
        result_ = ScreenshotResult::Pending;
        return true;
    }
    void complete(bool success) {
        if (result_ == ScreenshotResult::Pending)
            result_ = success ? ScreenshotResult::Succeeded : ScreenshotResult::Failed;
    }
    void cancel() {
        if (result_ == ScreenshotResult::Pending) result_ = ScreenshotResult::Cancelled;
    }
    ScreenshotResult result() const { return result_; }
    const std::string& path() const { return path_; }
private:
    ScreenshotResult result_ = ScreenshotResult::Idle;
    std::string path_;
};
}
