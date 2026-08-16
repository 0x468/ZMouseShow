#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace zmouse::capture
{
enum class FailureCategory
{
    none,
    timeout,
    access_lost,
    mode_unsupported,
    session_disconnected,
    duplication_limit,
    protected_content,
    device_removed,
    unknown,
};

enum class FrameResult
{
    frame,
    no_frame,
    unavailable,
    rebuild_required,
};

struct Diagnostics
{
    bool available{};
    std::string adapter;
    std::string output;
    std::string format{"BGRA8"};
    FailureCategory last_failure{FailureCategory::none};
    bool exclusion_applied{};
    std::uint64_t device_rebuild_count{};
};

[[nodiscard]] constexpr std::string_view failure_name(const FailureCategory category) noexcept
{
    switch (category)
    {
    case FailureCategory::none:
        return "none";
    case FailureCategory::timeout:
        return "timeout";
    case FailureCategory::access_lost:
        return "access_lost";
    case FailureCategory::mode_unsupported:
        return "mode_unsupported";
    case FailureCategory::session_disconnected:
        return "session_disconnected";
    case FailureCategory::duplication_limit:
        return "duplication_limit";
    case FailureCategory::protected_content:
        return "protected_content";
    case FailureCategory::device_removed:
        return "device_removed";
    case FailureCategory::unknown:
        return "unknown";
    }
    return "unknown";
}

class Lifecycle final
{
  public:
    void enable() noexcept
    {
        enabled_ = true;
    }
    void disable() noexcept
    {
        enabled_ = false;
        active_ = false;
    }
    void mark_ready() noexcept
    {
        if (enabled_)
        {
            active_ = true;
        }
    }
    void mark_failure(const FailureCategory failure) noexcept
    {
        diagnostics_.last_failure = failure;
        if (failure == FailureCategory::timeout)
        {
            return;
        }
        active_ = false;
        if (failure == FailureCategory::access_lost || failure == FailureCategory::device_removed ||
            failure == FailureCategory::session_disconnected)
        {
            ++diagnostics_.device_rebuild_count;
        }
    }

    [[nodiscard]] bool enabled() const noexcept
    {
        return enabled_;
    }
    [[nodiscard]] bool active() const noexcept
    {
        return active_;
    }
    [[nodiscard]] const Diagnostics& diagnostics() const noexcept
    {
        return diagnostics_;
    }
    [[nodiscard]] Diagnostics& diagnostics() noexcept
    {
        return diagnostics_;
    }

  private:
    bool enabled_{};
    bool active_{};
    Diagnostics diagnostics_{};
};
} // namespace zmouse::capture
