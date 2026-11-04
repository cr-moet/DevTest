#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

//==============================================================================
// Lock-free ring buffer for the two scope/FFT channels.
// The audio thread writes (push), the GUI thread reads (getL/getR).
// No locks and no memory allocation on the audio thread.
//==============================================================================
class ScopeBuffer
{
public:
    // 2^19 samples per channel: enough for a 1 s window at 192 kHz
    // plus the search range for the trigger.
    static constexpr int size = 1 << 19;
    static constexpr uint64_t mask = (uint64_t) size - 1;

    ScopeBuffer()
        : left  (new std::atomic<float>[size]),
          right (new std::atomic<float>[size])
    {
        clear();
    }

    void clear() noexcept
    {
        for (std::size_t i = 0; i < (std::size_t) size; ++i)
        {
            left[i].store  (0.0f, std::memory_order_relaxed);
            right[i].store (0.0f, std::memory_order_relaxed);
        }
    }

    // Call from the audio thread only
    inline void push (float l, float r) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        left[w & mask].store  (l, std::memory_order_relaxed);
        right[w & mask].store (r, std::memory_order_relaxed);
        writePos.store (w + 1, std::memory_order_release);
    }

    // Total number of samples written so far (keeps counting up)
    uint64_t getWritePos() const noexcept   { return writePos.load (std::memory_order_acquire); }

    float getL (uint64_t index) const noexcept { return left[index & mask].load (std::memory_order_relaxed); }
    float getR (uint64_t index) const noexcept { return right[index & mask].load (std::memory_order_relaxed); }

private:
    std::unique_ptr<std::atomic<float>[]> left, right;
    std::atomic<uint64_t> writePos { 0 };
};
