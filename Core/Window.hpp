#pragma once

#include "OS.hpp"
#include "Utils.hpp"
#include "MathVectors.hpp"

#include <vector>
#include <array>

namespace xor
{
    struct Input
    {
        struct MouseMove
        {
            int2 position = 0;

            MouseMove(int2 position = 0) : position(position) {}
        };

        struct MouseWheel
        {
            int delta = 0;

            MouseWheel(int delta = 0) : delta(delta) {}
        };

        struct Key
        {
            int code     = 0;
            bool pressed = false;

            Key(int code = 0, bool pressed = false) : code(code), pressed(pressed) {}
        };

        std::vector<MouseMove>  mouseMovements;
        std::vector<MouseWheel> mouseWheel;
        std::vector<Key>        keyEvents;
        std::vector<wchar_t>    characterInput;

        bool empty() const;
        void clear();
    };

    class Window
    {
        static const uint NumKeyCodes = 256;

        MovingPtr<HWND>               m_hWnd       = nullptr;
        int                           m_exitCode   = 0;
        bool                          m_terminate  = false;
        uint2                         m_size       = 0;
        std::array<bool, NumKeyCodes> m_keyHeld;
        Timer                         m_mainLoopTimer;
        Input                         m_input;

        static ATOM registerWindowClass();
        static LRESULT CALLBACK windowProcFun(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        void keyEvent(int keyCode, bool pressed);
        void mouseMove(int2 position);
        void mouseWheel(int delta);
    protected:
        LRESULT windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    public:
        Window(const char *title, uint2 size, int2 position = -1);
        virtual ~Window();

        uint2 size() const { return m_size; }
        HWND hWnd() { return m_hWnd; }

        int run();
        void terminate(int exitCode);

        virtual void mainLoop(double timeDelta) {}
        virtual void handleInput(const Input &input) {}

        virtual void keyUp(int keyCode) {}
        virtual void keyDown(int keyCode) {}
        bool isKeyHeld(int keyCode) const;

        void pumpMessages();
    };
}
