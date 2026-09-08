#pragma once
#include <string>
#include <utility>
#include <vector>

namespace wowee::rendering {
enum class ScreenshotResult { Idle, Pending, Succeeded, Failed, Cancelled };

// Render-thread only. Skipped frames leave Pending untouched.
class ScreenshotRequest {
public:
    bool queue(std::string path) {
        if (closed_) return false;
        if (path.empty() || result_ == ScreenshotResult::Pending) return reject();
        path_ = std::move(path);
        result_ = ScreenshotResult::Pending;
        return true;
    }
    void complete(bool success) {
        if (result_ == ScreenshotResult::Pending) {
            result_ = success ? ScreenshotResult::Succeeded : ScreenshotResult::Failed;
            completions_.push_back(result_);
        }
    }
    void cancel() {
        if (result_ == ScreenshotResult::Pending) {
            result_ = ScreenshotResult::Cancelled;
            completions_.push_back(result_);
        }
    }
    bool reject() {
        if (!closed_) completions_.push_back(ScreenshotResult::Failed);
        return false;
    }
    void close() { closed_ = true; cancel(); }
    std::vector<ScreenshotResult> consumeCompletions() {
        return std::exchange(completions_, {});
    }
    ScreenshotResult result() const { return result_; }
    const std::string& path() const { return path_; }
private:
    ScreenshotResult result_ = ScreenshotResult::Idle;
    std::string path_;
    bool closed_ = false;
    std::vector<ScreenshotResult> completions_;
};
}
