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

    void Window::keyEvent(int keyCode, bool pressed)
    {
        if (keyCode < NumKeyCodes)
        {
            m_keyHeld[keyCode] = pressed;
            m_input.keyEvents.emplace_back(keyCode, pressed);

            if (pressed)
                keyDown(keyCode);
            else
                keyUp(keyCode);
        }
    }

    void Window::mouseMove(int2 position)
    {
        m_input.mouseMovements.emplace_back(position);

        static HCURSOR arrow = LoadCursorA(nullptr, IDC_ARROW);
        SetCursor(arrow);
    }

    void Window::mouseWheel(int delta)
    {
        m_input.mouseWheel.emplace_back(delta);
    }

    LRESULT Window::windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CLOSE:
            PostQuitMessage(0); break;
        case WM_LBUTTONDOWN:
            keyEvent(VK_LBUTTON, true); break;
        case WM_LBUTTONUP:
            keyEvent(VK_LBUTTON, false); break;
        case WM_RBUTTONDOWN:
            keyEvent(VK_RBUTTON, true); break;
        case WM_RBUTTONUP:
            keyEvent(VK_RBUTTON, false); break;
        case WM_MBUTTONDOWN:
            keyEvent(VK_MBUTTON, true); break;
        case WM_MBUTTONUP:
            keyEvent(VK_MBUTTON, false); break;
        case WM_KEYDOWN:
            keyEvent(static_cast<int>(wParam), true); break;
        case WM_KEYUP:
            keyEvent(static_cast<int>(wParam), false); break;
        case WM_MOUSEWHEEL:
            mouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            break;
        case WM_MOUSEMOVE:
            mouseMove({
                static_cast<int16_t>(lParam),
                static_cast<int16_t>(lParam >> 16)
            });
            break;
        case WM_CHAR:
        {
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch == wParam)
                m_input.characterInput.emplace_back(ch);
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
        mainLoop(0.0);

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

            if (!m_input.empty())
            {
                handleInput(m_input);
                m_input.clear();
            }

            double delta = m_mainLoopTimer.seconds();
            mainLoop(delta);

            m_mainLoopTimer.reset();
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

    bool Input::empty() const
    {
        return mouseMovements.empty() &&
            mouseWheel.empty() &&
            keyEvents.empty() &&
            characterInput.empty();
    }

    void Input::clear()
    {
        mouseMovements.clear();
        mouseWheel.clear();
        keyEvents.clear();
        characterInput.clear();
    }

}
