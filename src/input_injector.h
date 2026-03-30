#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>

// Sends mouse/keyboard inputs to the GD window to control the player
class InputInjector {
public:
    InputInjector();
    ~InputInjector();

    bool findWindow();
    HWND getWindow() const { return hwnd_; }

    void pressClick();
    void releaseClick();
    void click(int durationMs = 50);

    // Direct PostMessage-based input (works even if window not focused)
    void sendKeyDown();
    void sendKeyUp();

    bool isClicking() const { return clicking_; }

private:
    HWND hwnd_ = nullptr;
    bool clicking_ = false;
};
