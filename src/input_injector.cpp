#include "input_injector.h"
#include <iostream>
#include <thread>
#include <chrono>

InputInjector::InputInjector() {}

InputInjector::~InputInjector() {
    if (clicking_) {
        releaseClick();
    }
}

bool InputInjector::findWindow() {
    // GD window class or title
    hwnd_ = FindWindowA(nullptr, "Geometry Dash");
    if (!hwnd_) {
        // Try alternative titles
        hwnd_ = FindWindowA("glfw30", nullptr);
    }
    if (!hwnd_) {
        std::cerr << "[InputInjector] Could not find Geometry Dash window" << std::endl;
        return false;
    }
    std::cout << "[InputInjector] Found GD window: " << hwnd_ << std::endl;
    return true;
}

void InputInjector::pressClick() {
    if (!hwnd_) return;
    
    // Ensure GD is focused before sending input
    HWND fgWindow = GetForegroundWindow();
    if (fgWindow != hwnd_) {
        return; // Don't send input if GD is not focused
    }
    
    // Use SendInput with mouse click (more reliable with GLFW than keyboard)
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input.mi.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(INPUT));
    clicking_ = true;
}

void InputInjector::releaseClick() {
    if (!hwnd_) return;
    
    // Only release if we were clicking
    if (!clicking_) return;
    
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    input.mi.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(INPUT));
    clicking_ = false;
}

void InputInjector::click(int durationMs) {
    pressClick();
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    releaseClick();
}

void InputInjector::sendKeyDown() {
    if (!hwnd_) return;
    pressClick(); // Use the same SendInput method
}

void InputInjector::sendKeyUp() {
    if (!hwnd_) return;
    releaseClick(); // Use the same SendInput method
}
