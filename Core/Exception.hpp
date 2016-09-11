#pragma once

#include "Core/Error.hpp"
#include "Core/String.hpp"

#include <exception>

namespace xor
{
    class Exception : public std::exception
    {
        String m_error;

        void printErrorMessage(const char *file = nullptr, int line = -1);
    public:
        Exception() = default;
        Exception(String error);
        Exception(const char *fmt, ...);
        Exception(const char *file, int line, String error);
        Exception(const char *file, int line, const char *fmt, ...);

        virtual const char *what() const override;
    };

}

// Work around poor inheriting constructor support in VS
#define XOR_EXCEPTION_TYPE(Type) struct Type : public ::xor::Exception { template <typename... Ts> Type(const Ts &... ts) : Exception(ts...) {} };
#define XOR_THROW(cond, ExType, ...) do { if (!(cond)) { throw ExType(__FILE__, __LINE__, __VA_ARGS__); } } while (false)
#define XOR_THROW_HR(hr, ExType) do { HRESULT hr__ = hr; if (!SUCCEEDED(hr__)) { throw ExType(__FILE__, __LINE__, errorMessage(hr__)); } } while (false)
