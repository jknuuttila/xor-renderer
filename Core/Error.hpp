#pragma once

#include "OS.hpp"

#ifndef XOR_ASSERTIONS
#if defined(_DEBUG)
#define XOR_ASSERTIONS 1
#endif
#endif

namespace xor
{

#define XOR_DEBUG_BREAK_IF_FALSE(cond) do { if (!cond) \
{ \
    if (IsDebuggerPresent()) \
        DebugBreak(); \
    else \
        TerminateProcess(GetCurrentProcess(), 1); \
} } while (false)

    namespace detail
    {
        bool checkImpl(bool cond, const char *fmt, ...);
        bool checkHRImpl(HRESULT hr);
        bool checkLastErrorImpl();
    }

    class String;
    String errorMessage(HRESULT hr);
}

#define XOR_CHECK(cond, ...)   XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkImpl(cond, ## __VA_ARGS__))
#define XOR_CHECK_HR(hr)       XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkHRImpl(hr))
#define XOR_CHECK_LAST_ERROR() XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkLastErrorImpl())

#if XOR_ASSERTIONS
#define XOR_ASSERT(cond, ...)   XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkImpl(cond, ## __VA_ARGS__))
#define XOR_ASSERT_HR(hr)       XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkHRImpl(hr))
#define XOR_ASSERT_LAST_ERROR() XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkLastErrorImpl())
#else
#define XOR_ASSERT(cond, ...)   do {} while (false)
#define XOR_ASSERT_HR(hr)       do {} while (false)
#define XOR_ASSERT_LAST_ERROR() do {} while (false)
#endif
