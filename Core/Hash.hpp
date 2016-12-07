#pragma once

#include "external/SpookyHash/SpookyV2.h"
#include "Utils.hpp"

#include <cstdint>

namespace xor
{
    class Hash
    {
        SpookyHash m_hash;
    public:
        Hash(uint64_t seed1 = 0, uint64_t seed2 = 0)
        {
            m_hash.Init(seed1, seed2);
        }

        Hash &bytes(const void *bytes, size_t size)
        {
            m_hash.Update(bytes, size);
            return *this;
        }

        Hash &bytes(const void *begin, const void *end)
        {
            m_hash.Update(begin, static_cast<size_t>(
                static_cast<const uint8_t *>(end) -
                static_cast<const uint8_t *>(begin)));
            return *this;
        }

        Hash &bytes(Span<const uint8_t> bytes)
        {
            m_hash.Update(bytes.data(), bytes.size());
            return *this;
        }

        template <typename T>
        Hash &pod(const T &t)
        {
            m_hash.Update(&t, sizeof(t));
            return *this;
        }

        uint64_t done()
        {
            uint64_t a, b;
            m_hash.Final(&a, &b);
            return a;
        }

        operator uint64_t()
        {
            return done();
        }

        std::pair<uint64_t, uint64_t> done128()
        {
            uint64_t a, b;
            m_hash.Final(&a, &b);
            return { a, b };
        }
    };

    inline uint64_t hashBytes(Span<const uint8_t> bytes)
    {
        return Hash().bytes(bytes);
    }

    inline uint64_t hashBytes(const void *bytes, size_t size)
    {
        return Hash().bytes(bytes, size);
    }

    inline uint64_t hashBytes(const void *begin, const void *end)
    {
        return Hash().bytes(begin, end);
    }

    template <typename... Ts>
    uint64_t hashPods(const Ts &... ts)
    {
        Hash h;
        int dummy[] = { (h.pod(ts), 0)... };
        return h.done();
    }

    struct PodHash
    {
        template <typename T>
        size_t operator()(const T &t) const
        {
            return hashPods(t);
        }
    };

    struct PodEqual
    {
        template <typename T>
        bool operator()(const T &a, const T &b) const
        {
            return memcmp(&a, &b, sizeof(a)) == 0;
        }
    };
}