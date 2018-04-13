#include "OS.hpp"
#include "Error.hpp"
#include "Utils.hpp"
#include "String.hpp"
#include "Math.hpp"

#include <cstring>
#include <vector>

namespace Xor
{
    Timer::Timer()
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        period = 1.0 / static_cast<double>(f.QuadPart);

        reset();
    }

    void Timer::reset()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        start = now.QuadPart;
    }

    double Timer::seconds() const
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        uint64_t ticks = now.QuadPart - start;
        return static_cast<double>(ticks) * period;
    }

    SequenceTracker::Bit SequenceTracker::bit(SeqNum seqNum) const
    {
        Bit b;
        
        uint64_t offset       = static_cast<uint64_t>(seqNum - m_uncompletedBase);
        uint64_t offsetQwords = offset / 64;
        uint64_t offsetBits   = offset % 64;

        b.qword = (offsetQwords < m_uncompletedBits.size())
            ? static_cast<int64_t>(offsetQwords)
            : -1;

        b.mask = 1ull << offsetBits;

        return b;
    }

    void SequenceTracker::removeCompletedBits()
    {
        auto begin = m_uncompletedBits.begin();

        auto firstNonZero = std::find_if(
            begin,
            m_uncompletedBits.end(),
            [] (auto v) { return v != 0; });

        auto zeroes = firstNonZero - begin;

        // Always leave at least one qword, because some of 
        // its bits might be completely unused.
        if (zeroes > 1)
        {
            auto remove = zeroes - 1;
            m_uncompletedBits.erase(begin, begin + remove);
            m_uncompletedBase += static_cast<uint64_t>(remove) * 64;
        }
    }

    int64_t SequenceTracker::lowestSetBit() const
    {
        int64_t base = 0;

        for (auto &qword : m_uncompletedBits)
        {
            auto lowest = firstbitlow(qword);

            if (lowest >= 0)
            {
                return lowest + base;
            }

            base += 64;
        }

        return -1;
    }

    SeqNum SequenceTracker::start()
    {
        SeqNum seqNum = m_next;
        ++m_next;

        auto b = bit(seqNum);
        if (b.qword < 0)
        {
            XOR_ASSERT(seqNum == m_uncompletedBits.size() * 64 + m_uncompletedBase,
                       "Sequence number out of sync.");

            m_uncompletedBits.emplace_back(1);
        }
        else
        {
            m_uncompletedBits[b.qword] |= b.mask;
        }

        return seqNum;
    }

    void SequenceTracker::complete(SeqNum seqNum)
    {
        XOR_ASSERT(seqNum >= 0, "Sequence numbers must be non-negative.");
        XOR_ASSERT(!hasCompleted(seqNum),
                   "Sequence number %lld was completed twice.",
                   static_cast<lld>(seqNum));

        auto b = bit(seqNum);

        XOR_ASSERT(b.qword >= 0,
                   "Sequence number %lld was never started.",
                   static_cast<lld>(seqNum));

        m_uncompletedBits[b.qword] &= ~b.mask;

        removeCompletedBits();
    }

    SeqNum SequenceTracker::newestStarted() const
    {
        return m_next - 1;
    }

    SeqNum SequenceTracker::oldestUncompleted() const
    {
        auto lowest = lowestSetBit();

        if (lowest >= 0)
            return m_uncompletedBase + static_cast<uint64_t>(lowest);
        else
            return InvalidSeqNum;
    }

    bool SequenceTracker::hasCompleted(SeqNum seqNum) const
    {
        XOR_ASSERT(seqNum >= 0, "Sequence numbers must be non-negative.");

        if (seqNum < m_uncompletedBase)
            return true;

        auto b = bit(seqNum);

        XOR_ASSERT(b.qword >= 0,
                   "Sequence number %lld was never started.",
                   static_cast<lld>(seqNum));

        return (m_uncompletedBits[b.qword] & b.mask) == 0;
    }

    String toString(bool b)
    {
        return b ? "true" : "false";
    }

    String toString(uint u)
    {
        return String::format("%u", u);
    }

    String toString(int i)
    {
        return String::format("%d", i);
    }

    String toString(float f)
    {
        return String::format("%f", f);
    }

    String toString(double d)
    {
        return String::format("%f", d);
    }
}
