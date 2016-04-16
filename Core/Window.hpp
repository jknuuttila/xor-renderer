#pragma once

#include "OS.hpp"
#include "Utils.hpp"

namespace xor
{
    class Window
    {
        MovingPtr<HWND> m_hWnd       = nullptr;
        int             m_exitCode   = 0;
        bool            m_terminate  = false;

        static ATOM registerWindowClass();
        static LRESULT CALLBACK windowProcFun(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    protected:
        LRESULT windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    public:
        // TODO: use int2
        Window(const char *title, int w, int h, int x = -1, int y = -1);
        virtual ~Window();

        HWND hWnd() { return m_hWnd; }

        int run();
        void terminate(int exitCode);

        virtual void mainLoop() {}
    };
}
