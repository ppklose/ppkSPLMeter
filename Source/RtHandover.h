#pragma once
#include <atomic>
#include <memory>

//==============================================================================
// Real-time safe hand-over of an object built on the message thread.
//
// The message thread publishes a complete object; the audio thread takes it over
// at the start of a block (acquire) and parks the previous one, which the message
// thread deletes on its next publish (or in the destructor). The audio thread only
// swaps when nothing is parked, so it never allocates or frees.
//
// current() may be read on the message thread: the object it returns is only
// deleted by a later publish() on that same thread.
template <typename T>
class RtHandover
{
public:
    RtHandover() = default;
    ~RtHandover()
    {
        delete pending_.exchange (nullptr);
        delete retired_.exchange (nullptr);
        delete active_;
    }

    // Message thread
    void publish (std::unique_ptr<T> next)
    {
        delete retired_.exchange (nullptr, std::memory_order_acq_rel);   // no longer used by the audio thread
        delete pending_.exchange (next.release(), std::memory_order_acq_rel);
    }

    // Message thread while the audio thread is stopped (prepareToPlay)
    void replaceNow (std::unique_ptr<T> next)
    {
        delete pending_.exchange (nullptr);
        delete retired_.exchange (nullptr);
        delete active_;
        active_ = next.release();
        current_.store (active_, std::memory_order_release);
    }

    T* current() const noexcept { return current_.load (std::memory_order_acquire); }

    // Audio thread: takes over a pending object. Returns true when it swapped.
    bool acquire() noexcept
    {
        if (pending_.load (std::memory_order_acquire) == nullptr
            || retired_.load (std::memory_order_acquire) != nullptr)
            return false;
        auto* next = pending_.exchange (nullptr, std::memory_order_acq_rel);
        if (next == nullptr)
            return false;
        auto* old = active_;
        active_ = next;
        current_.store (next, std::memory_order_release);
        retired_.store (old, std::memory_order_release);
        return true;
    }

    T* active() const noexcept { return active_; }   // audio thread

private:
    std::atomic<T*> pending_ { nullptr }, retired_ { nullptr }, current_ { nullptr };
    T*              active_ = nullptr;
};
