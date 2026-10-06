#pragma once

#include <SDL.h>

#include <array>
#include <string>

namespace rr {

// Input state sampled once per game tick, like the original Input_ReadKeys/Input_ReadMouse2 which
// ran once per main-loop iteration. Events arriving between ticks are latched so that a click or
// key press shorter than a tick is never lost.
class Input {
public:
    void HandleEvent(const SDL_Event& ev);
    // Called at the start of each tick: publishes latched events, clears per-tick edges afterwards.
    void BeginTick();

    // Mouse position in UI logical coordinates (set by the app from window coordinates).
    void SetMousePos(float x, float y) {
        mouseX_ = x;
        mouseY_ = y;
    }
    float mouseX() const { return mouseX_; }
    float mouseY() const { return mouseY_; }

    bool leftDown() const { return lDown_; }
    bool rightDown() const { return rDown_; }
    bool leftClicked() const { return lPressed_; }    // went down during this tick
    bool leftReleased() const { return lReleased_; }  // went up during this tick
    bool rightClicked() const { return rPressed_; }
    bool rightReleased() const { return rReleased_; }
    int wheel() const { return wheel_; }

    bool KeyDown(SDL_Scancode sc) const { return keys_[sc]; }
    bool KeyPressed(SDL_Scancode sc) const { return pressed_[sc]; }
    // Text typed during this tick (for name entry).
    const std::string& text() const { return text_; }

private:
    float mouseX_ = 0, mouseY_ = 0;
    bool lDown_ = false, rDown_ = false;
    bool lPressed_ = false, lReleased_ = false, rPressed_ = false, rReleased_ = false;
    bool pendLPressed_ = false, pendLReleased_ = false, pendRPressed_ = false, pendRReleased_ = false;
    int wheel_ = 0, pendWheel_ = 0;
    std::array<bool, SDL_NUM_SCANCODES> keys_{};
    std::array<bool, SDL_NUM_SCANCODES> pressed_{};
    std::array<bool, SDL_NUM_SCANCODES> pendPressed_{};
    std::string text_, pendText_;
};

}  // namespace rr
