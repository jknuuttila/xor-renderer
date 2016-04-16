#pragma once

#include <Windows.h>
#include <wrl/client.h>

#include <functional>
#include <vector>
#include <string>

#undef min
#undef max

using namespace Microsoft::WRL;

namespace xor {

template <typename T>
size_t size(const T &t) {
    using std::begin;
    using std::end;
    return end(t) - begin(t);
}

template <typename T>
size_t sizeBytes(const T &t) {
    return ::xor::size(t) * sizeof(t[0]);
}

template <typename T>
void zero(T &t) {
    memset(&t, 0, sizeof(t));
}

void vlog(const char *fmt, va_list ap);
void log(const char *fmt, ...);

#define XOR_DEBUG_BREAK_IF_FALSE(cond) if (!cond) { \
    if (IsDebuggerPresent()) \
        DebugBreak(); \
    else \
        TerminateProcess(GetCurrentProcess(), 1); \
}

namespace detail
{
    bool checkImpl(bool cond, const char *fmt, ...);
    bool checkHRImpl(HRESULT hr);
    bool checkLastErrorImpl();
}

#define XOR_CHECK(cond, ...)   XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkImpl(cond, ## __VA_ARGS__))
#define XOR_CHECK_HR(hr)       XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkHRImpl(hr))
#define XOR_CHECK_LAST_ERROR() XOR_DEBUG_BREAK_IF_FALSE(::xor::detail::checkLastErrorImpl())

struct Window {
    HWND hWnd;

    Window(const char *title, int w, int h, int x = -1, int y = -1);
    ~Window();

    void run(std::function<bool()> mainLoop);
};

class Timer {
    double period;
    uint64_t start;
public:
    Timer();
    double seconds() const;
};

std::vector<std::string> listFiles(const std::string &path, const std::string &pattern = "*");
std::vector<std::string> searchFiles(const std::string &path, const std::string &pattern);
std::string replaceAll(std::string s, const std::string &replacedString, const std::string &replaceWith);
std::vector<std::string> tokenize(const std::string &s, const std::string &delimiters);
std::vector<std::string> splitPath(const std::string &path);

std::string fileOpenDialog(const std::string &description, const std::string &pattern);
std::string fileSaveDialog(const std::string &description, const std::string &pattern);
std::string absolutePath(const std::string &path);

template <typename Iter>
std::string join(Iter begin, Iter end, std::string separator) {
    std::string s;

    if (begin == end)
        return s;

    auto it = begin;
    s = *it;
    ++it;

    while (it != end)
    {
        s += separator;
        s += *it;
        ++it;
    }

    return s;
}

class FontRasterizer {
    HFONT hFont;
    HDC memoryDC;
    HBITMAP bitmap;
    int bitmapW;
    int bitmapH;

    void ensureBitmap(int w, int h);
public:
    FontRasterizer(const std::vector<std::string> &fontNamesInPreferenceOrder,
                   int pointSize = 16);
    ~FontRasterizer();

    struct TextPixels {
        static const unsigned BytesPerPixel = 4;

        unsigned width;
        unsigned height;
        std::vector<uint8_t> pixels;

        TextPixels(unsigned width = 0, unsigned height = 0);

        unsigned rowPitch() const {
            return width * BytesPerPixel;
        }
    };
    TextPixels renderText(const std::string &text);
};

}

