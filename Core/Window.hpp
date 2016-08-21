#pragma once

#include "OS.hpp"
#include "Utils.hpp"
#include "MathVectors.hpp"

#include <array>

namespace xor
{
    class Window
    {
        static const uint NumKeyCodes = 256;

        MovingPtr<HWND>               m_hWnd       = nullptr;
        int                           m_exitCode   = 0;
        bool                          m_terminate  = false;
        uint2                         m_size       = 0;
        std::array<bool, NumKeyCodes> m_keyHeld;

        static ATOM registerWindowClass();
        static LRESULT CALLBACK windowProcFun(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    protected:
        LRESULT windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    public:
        Window(const char *title, uint2 size, int2 position = -1);
        virtual ~Window();

        uint2 size() const { return m_size; }
        HWND hWnd() { return m_hWnd; }

        int run();
        void terminate(int exitCode);

        virtual void mainLoop() {}

        virtual void keyUp(int keyCode) {}
        virtual void keyDown(int keyCode) {}
        bool isKeyHeld(int keyCode) const;
    };
}
