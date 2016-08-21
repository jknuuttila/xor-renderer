#include "Window.hpp"
#include "Error.hpp"

namespace xor
{
    static const int WindowThisPtrIndex = 0;
    static const char WindowClassName[] = "XORWindow";

    ATOM Window::registerWindowClass()
    {
        WNDCLASSEX c;
        zero(c);
        c.cbSize        = sizeof(c);
        c.cbWndExtra    = 2 * sizeof(uintptr_t);
        c.style  		= CS_HREDRAW | CS_VREDRAW;
        c.lpfnWndProc   = &Window::windowProcFun;
        c.hInstance     = GetModuleHandle(nullptr);
        c.lpszClassName = WindowClassName;
        return RegisterClassEx(&c);
    }

    LRESULT CALLBACK Window::windowProcFun(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        auto w = reinterpret_cast<Window *>(GetWindowLongPtrA(hWnd, WindowThisPtrIndex));
        if (w)
        {
            return w->windowProc(hWnd, uMsg, wParam, lParam);
        }
        else
        {
            switch (uMsg)
            {
            case WM_CLOSE:
                PostQuitMessage(0);
                break;
            default:
                break;
            }

            return DefWindowProc(hWnd, uMsg, wParam, lParam);
        }
    }

    LRESULT Window::windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        case WM_KEYDOWN:
        {
            int keyCode = static_cast<int>(wParam);
            m_keyHeld[keyCode] = true;
            keyDown(keyCode);
            break;
        }
        case WM_KEYUP:
        {
            int keyCode = static_cast<int>(wParam);
            m_keyHeld[keyCode] = false;
            keyUp(keyCode);
            break;
        }
        default:
            break;
        }

        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    Window::Window(const char *title, uint2 size, int2 position)
        : m_size(size)
    {
        m_keyHeld.fill(false);

        auto w = size.x;
        auto h = size.y;
        auto x = position.x;
        auto y = position.y;

        static ATOM windowClass = registerWindowClass();

        x = (x < 0) ? CW_USEDEFAULT : x;
        y = (y < 0) ? CW_USEDEFAULT : y;

        RECT rect;
        rect.left   = 0;
        rect.top    = 0;
        rect.right  = w;
        rect.bottom = h;

        DWORD style = WS_SYSMENU | WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        AdjustWindowRectEx(&rect, style, FALSE, 0);

        w = rect.right - rect.left;
        h = rect.bottom - rect.top;

        m_hWnd = CreateWindowExA(
            0,
            WindowClassName,
            title,
            style,
            x, y,
            w, h,
            nullptr,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr);

        XOR_CHECK_LAST_ERROR();

        SetWindowLongPtrA(m_hWnd, WindowThisPtrIndex, reinterpret_cast<LONG_PTR>(this));
    }

    Window::~Window()
    {
        if (m_hWnd)
            DestroyWindow(m_hWnd);
    }

    int Window::run()
    {
        while (!m_terminate)
        {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE) != 0)
            {
                if (msg.message == WM_QUIT)
                    terminate(0);

                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }

            mainLoop();
        }

        return m_exitCode;
    }

    void Window::terminate(int exitCode)
    {
        m_exitCode  = exitCode;
        m_terminate = true;
    }

    bool Window::isKeyHeld(int keyCode) const
    {
        return m_keyHeld[keyCode];
    }

    // TODO: Move this elsewhere
    class FontRasterizer
    {
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

        struct TextPixels
        {
            static const unsigned BytesPerPixel = 4;

            unsigned width;
            unsigned height;
            std::vector<uint8_t> pixels;

            TextPixels(unsigned width = 0, unsigned height = 0);

            unsigned rowPitch() const
            {
                return width * BytesPerPixel;
            }
        };
        TextPixels renderText(const std::string &text);
    };

    void FontRasterizer::ensureBitmap(int w, int h)
    {
        if (bitmapW >= w && bitmapH >= h)
            return;

        if (bitmap)
            DeleteObject(bitmap);

        bitmap = CreateCompatibleBitmap(GetDC(nullptr), w, h);
        XOR_CHECK(bitmap != nullptr, "Could not get bitmap");
        bitmapW = w;
        bitmapH = h;

        SelectObject(memoryDC, bitmap);
    }

    FontRasterizer::FontRasterizer(const std::vector<std::string> &fontNamesInPreferenceOrder,
                                   int pointSize)
    {
        memoryDC = CreateCompatibleDC(nullptr);
        XOR_CHECK(memoryDC != nullptr, "Could not get memory hDC");

        auto bpp = GetDeviceCaps(memoryDC, BITSPIXEL);

        int height = -MulDiv(pointSize, GetDeviceCaps(memoryDC, LOGPIXELSY), 72);

        std::vector<std::string> names(fontNamesInPreferenceOrder.begin(), fontNamesInPreferenceOrder.end());
        // Try the default last
        names.emplace_back("");

        for (auto &n : names)
        {
            for (DWORD quality : { ANTIALIASED_QUALITY, DEFAULT_QUALITY })
            {
                hFont = CreateFontA(
                    height,
                    0, // default width
                    0, 0, // no tilt
                    FW_NORMAL, FALSE, FALSE, FALSE, // normal weight
                    ANSI_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    quality,
                    FIXED_PITCH | FF_DONTCARE,
                    n.c_str());

                if (hFont)
                    break;
            }

            if (hFont)
                break;
        }

        XOR_CHECK(hFont != nullptr, "Could not get font");
        SelectObject(memoryDC, hFont);
        SetTextColor(memoryDC, RGB(255, 255, 255));
        SetBkColor(memoryDC, RGB(0, 0, 0));

        bitmap = nullptr;
        bitmapW = 0;
        bitmapH = 0;
    }

    FontRasterizer::~FontRasterizer()
    {
        if (bitmap)
            DeleteObject(bitmap);
        DeleteObject(hFont);
        DeleteDC(memoryDC);
    }

    FontRasterizer::TextPixels FontRasterizer::renderText(const std::string &text)
    {
        // Find out how big the text is first, so we can make sure we have enough room
        SIZE textSize ={ 0 };
        GetTextExtentPoint32A(memoryDC,
                              text.c_str(),
                              static_cast<int>(text.size()),
                              &textSize);

        TextPixels textPixels(textSize.cx, textSize.cy);

        ensureBitmap(textPixels.width, textPixels.height);

        RECT rect;
        rect.left   = 0;
        rect.top    = 0;
        rect.right  = textPixels.width;
        rect.bottom = textPixels.height;

        // Draw the actual text in the bitmap
        auto foo = GetTextColor(memoryDC);
        TextOutA(memoryDC, 0, 0, text.c_str(), static_cast<int>(text.size()));
        //DrawTextA(memoryDC, text.c_str(), static_cast<int>(text.size()), &rect, DT_NOCLIP);

        BITMAP bmp;
        GetObject(bitmap, sizeof(bmp), &bmp);

        // Recover the rasterized image
        BITMAPINFO info;
        zero(info);
        auto &h = info.bmiHeader;
        h.biSize = sizeof(h);
        h.biWidth = textPixels.width;
        h.biHeight = textPixels.height;
        h.biPlanes = 1;
        h.biBitCount = 32;
        h.biCompression = BI_RGB;
        h.biSizeImage = 0;
        h.biXPelsPerMeter = 0;
        h.biYPelsPerMeter = 0;
        h.biClrUsed = 0;
        h.biClrImportant = 0;

        int linesCopied = GetDIBits(
            memoryDC, bitmap,
            0, textPixels.height,
            textPixels.pixels.data(),
            &info,
            DIB_RGB_COLORS);

        XOR_CHECK(linesCopied == textPixels.height, "Could not get all scan lines of rasterized font.");

        // The scanlines are upside down, so reverse them.
        std::vector<uint8_t> scanlineTemp(textPixels.width * TextPixels::BytesPerPixel);

        auto rowPtr = [&](size_t row)
        {
            return textPixels.pixels.data() + row * textPixels.rowPitch();
        };
        for (size_t i = 0; i < textPixels.height / 2; ++i)
        {
            size_t j = textPixels.height - 1 - i;
            size_t N = sizeBytes(scanlineTemp);
            memcpy(scanlineTemp.data(), rowPtr(i), N);
            memcpy(rowPtr(i), rowPtr(j), N);
            memcpy(rowPtr(j), scanlineTemp.data(), N);
        }

        return textPixels;
    }

    FontRasterizer::TextPixels::TextPixels(unsigned width, unsigned height)
        : width(width)
        , height(height)
        , pixels(width * height * BytesPerPixel)
    {
    }

}
