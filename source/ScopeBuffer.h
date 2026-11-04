#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

//==============================================================================
// Lock-freier Ringpuffer fuer zwei Oszilloskop-Kanaele.
// Der Audio-Thread schreibt (push), der GUI-Thread liest (getL/getR).
// Kein Lock, keine Speicherallokation im Audio-Thread.
//==============================================================================
class ScopeBuffer
{
public:
    // 2^19 Samples pro Kanal: reicht fuer 1 s Fensterbreite bei 192 kHz
    // plus Suchbereich fuer den Trigger.
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

    // Nur aus dem Audio-Thread aufrufen
    inline void push (float l, float r) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        left[w & mask].store  (l, std::memory_order_relaxed);
        right[w & mask].store (r, std::memory_order_relaxed);
        writePos.store (w + 1, std::memory_order_release);
    }

    // Gesamtzahl der bisher geschriebenen Samples (laeuft immer weiter)
    uint64_t getWritePos() const noexcept   { return writePos.load (std::memory_order_acquire); }

    float getL (uint64_t index) const noexcept { return left[index & mask].load (std::memory_order_relaxed); }
    float getR (uint64_t index) const noexcept { return right[index & mask].load (std::memory_order_relaxed); }

private:
    std::unique_ptr<std::atomic<float>[]> left, right;
    std::atomic<uint64_t> writePos { 0 };
};
