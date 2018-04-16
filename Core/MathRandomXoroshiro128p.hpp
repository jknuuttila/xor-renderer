#pragma once

// Modified for C++ from original xoroshiro128p, original copyright
// notice reproduced.

/*  Written in 2016 by David Blackman and Sebastiano Vigna (vigna@acm.org)

To the extent possible under law, the author has dedicated all copyright
and related and neighboring rights to this software to the public domain
worldwide. This software is distributed without any warranty.

See <http://creativecommons.org/publicdomain/zero/1.0/>. */

/* This is the successor to xorshift128+. It is the fastest full-period
   generator passing BigCrush without systematic failures, but due to the
   relatively short period it is acceptable only for applications with a
   mild amount of parallelism; otherwise, use a xorshift1024* generator.

   Beside passing BigCrush, this generator passes the PractRand test suite
   up to (and included) 16TB, with the exception of binary rank tests, as
   the lowest bit of this generator is an LFSR of degree 128. The next bit
   can be described by an LFSR of degree 8256, but in the long run it will
   fail linearity tests, too. The other bits needs a much higher degree to
   be represented as LFSRs.

   We suggest to use a sign test to extract a random Boolean value, and
   right shifts to extract subsets of bits.

   Note that the generator uses a simulated rotate operation, which most C
   compilers will turn into a single instruction. In Java, you can use
   Long.rotateLeft(). In languages that do not make low-level rotation
   instructions accessible xorshift128+ could be faster.

   The state must be seeded so that it is not everywhere zero. If you have
   a 64-bit seed, we suggest to seed a splitmix64 generator and use its
   output to fill s. */

#include <cstdint>
#include <numeric>

namespace Xor
{
    constexpr uint64_t DefaultRandomSeed0 = 39529057;
    constexpr uint64_t DefaultRandomSeed1 = 61768894;

    class Random
    {
        uint64_t s[2];
    public:
        using result_type = uint64_t;

        Random(uint64_t seed0 = DefaultRandomSeed0,
               uint64_t seed1 = DefaultRandomSeed1)
        {
            s[0] = seed0;
            s[1] = seed1;
        }

        static Random nonDeterministicSeed()
        {
            std::random_device rnd;

            uint32_t dw0 = rnd();
            uint32_t dw1 = rnd();
            uint32_t dw2 = rnd();
            uint32_t dw3 = rnd();

            uint64_t q0 = dw0;
            uint64_t q1 = dw1;
            uint64_t q2 = dw2;
            uint64_t q3 = dw3;

            uint64_t s0 = (q1 << 32) | q0;
            uint64_t s1 = (q3 << 32) | q2;

            return Random(s0, s1);
        }

        static uint64_t min() { return 0; }
        static uint64_t max() { return std::numeric_limits<uint64_t>::max(); }

        uint64_t operator()() { return next(); }

        uint64_t next(void) {
            const uint64_t s0 = s[0];
            uint64_t s1 = s[1];
            const uint64_t result = s0 + s1;

            s1 ^= s0;
            s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
            s[1] = rotl(s1, 36); // c

            return result;
        }


        /* This is the jump function for the generator. It is equivalent
           to 2^64 calls to next(); it can be used to generate 2^64
           non-overlapping subsequences for parallel computations. */
        void jump(void) {
            static const uint64_t JUMP[] = { 0xbeac0467eba5facb, 0xd86b048b86aa9922 };

            uint64_t s0 = 0;
            uint64_t s1 = 0;
            for (int i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
                for (int b = 0; b < 64; b++) {
                    if (JUMP[i] & UINT64_C(1) << b) {
                        s0 ^= s[0];
                        s1 ^= s[1];
                    }
                    next();
                }

            s[0] = s0;
            s[1] = s1;
        }

    private:
        static inline uint64_t rotl(const uint64_t x, int k) {
            return (x << k) | (x >> (64 - k));
        }
    };
}