#define WIN32_LEAN_AND_MEAN
#include "TaskRunner.hpp"
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <objbase.h>
namespace stoutwall {
struct TaskRunner::Impl {
    struct Item { uint64_t id; int32_t operation; Work work; };
    HWND window = nullptr;
    HANDLE cancel = nullptr;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Item> queue;
    std::thread worker;
    uint64_t nextId = 1;
    bool stopping = false;
    bool started = false;
};
static std::wstring ErrorText(const char* text) {
    if (!text || !*text) return L"后台操作失败。";
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (n <= 1) return L"后台操作失败。";
    std::wstring result(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), n);
    result.pop_back();
    return result;
}
bool TaskRunner::Start(HWND uiWindow) {
    if (!uiWindow || !IsWindow(uiWindow) || impl_) return false;
    std::unique_ptr<Impl> p(new Impl); p->window = uiWindow; p->cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!p->cancel) return false;
    Impl* state = p.get();
    try {
        state->worker = std::thread([state] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (;;) {
                Impl::Item item;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->ready.wait(lock, [state] { return state->stopping || !state->queue.empty(); });
                    if (state->stopping) break;
                    item = std::move(state->queue.front()); state->queue.pop_front();
                }
                TaskResult* result = new TaskResult; result->id = item.id; result->operation = item.operation;
                try { *result = item.work(); result->id = item.id; result->operation = item.operation; }
                catch (const std::exception& e) { result->code = -1; result->message = ErrorText(e.what()); }
                catch (...) { result->code = -1; result->message = L"后台操作失败。"; }
                if (!PostMessageW(state->window, WM_STOUTWALL_TASK_DONE, 0, reinterpret_cast<LPARAM>(result))) delete result;
            }
            if (SUCCEEDED(com)) CoUninitialize();
        });
    } catch (...) { return false; }
    state->started = true; impl_ = p.release(); return true;
}
uint64_t TaskRunner::Submit(int32_t operation, Work work) {
    if (!impl_ || !work) return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping) return 0;
    const uint64_t id = impl_->nextId++;
    impl_->queue.push_back({id, operation, std::move(work)}); impl_->ready.notify_one(); return id;
}
HANDLE TaskRunner::CancellationEvent() const noexcept { return impl_ ? impl_->cancel : nullptr; }
void TaskRunner::Stop() {
    if (!impl_) return;
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stopping = true; impl_->queue.clear(); }
    SetEvent(impl_->cancel);
    impl_->ready.notify_all(); if (impl_->worker.joinable()) impl_->worker.join();
    MSG pending{};
    while (PeekMessageW(&pending, impl_->window, WM_STOUTWALL_TASK_DONE, WM_STOUTWALL_TASK_DONE, PM_REMOVE))
        delete reinterpret_cast<TaskResult*>(pending.lParam);
    CloseHandle(impl_->cancel); delete impl_; impl_ = nullptr;
}
TaskRunner::~TaskRunner() { Stop(); }
} // namespace stoutwall
