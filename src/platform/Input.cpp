#include "platform/Input.h"

namespace rr {

void Input::HandleEvent(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                lDown_ = true;
                pendLPressed_ = true;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                rDown_ = true;
                pendRPressed_ = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                lDown_ = false;
                pendLReleased_ = true;
            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                rDown_ = false;
                pendRReleased_ = true;
            }
            break;
        case SDL_MOUSEWHEEL:
            pendWheel_ += ev.wheel.y;
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.scancode < SDL_NUM_SCANCODES) {
                keys_[ev.key.keysym.scancode] = true;
                if (!ev.key.repeat) pendPressed_[ev.key.keysym.scancode] = true;
            }
            break;
        case SDL_KEYUP:
            if (ev.key.keysym.scancode < SDL_NUM_SCANCODES) keys_[ev.key.keysym.scancode] = false;
            break;
        case SDL_TEXTINPUT:
            pendText_ += ev.text.text;
            break;
        default:
            break;
    }
}

void Input::BeginTick() {
    lPressed_ = pendLPressed_;
    lReleased_ = pendLReleased_;
    rPressed_ = pendRPressed_;
    rReleased_ = pendRReleased_;
    wheel_ = pendWheel_;
    pressed_ = pendPressed_;
    text_ = std::move(pendText_);
    pendLPressed_ = pendLReleased_ = pendRPressed_ = pendRReleased_ = false;
    pendWheel_ = 0;
    pendPressed_.fill(false);
    pendText_.clear();
}

}  // namespace rr
