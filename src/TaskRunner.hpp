#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace stoutwall {
inline constexpr UINT WM_STOUTWALL_TASK_DONE = WM_APP + 0x251;
struct TaskResult {
    uint64_t id = 0;
    int32_t operation = 0;
    int32_t code = 0;
    std::wstring message;
    std::vector<uint8_t> bytes;
    std::shared_ptr<void> payload;
};
// Dispatches synchronous DLL/API work on one persistent worker. This preserves
// gallery.dll's one-session, one-thread contract and keeps the message pump free.
class TaskRunner final {
public:
    using Work = std::function<TaskResult()>;
    bool Start(HWND uiWindow);
    uint64_t Submit(int32_t operation, Work work);
    HANDLE CancellationEvent() const noexcept;
    void Stop();
    ~TaskRunner();
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;
    TaskRunner() = default;
private:
    struct Impl;
    Impl* impl_ = nullptr;
};
} // namespace stoutwall
