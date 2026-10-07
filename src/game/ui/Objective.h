#pragma once

#include <memory>
#include <string>

#include "config/Config.h"
#include "gfx/Image.h"
#include "ui/TextWindow.h"

namespace rr {

class Font;
class Input;

// The mission briefing panel (Objective_*): shown when a level starts and again when it is won
// or lost. Images come from the level block (ObjectiveImage640x480 etc.), texts from the level's
// section of the ObjectiveText file ("Objective:", "Completion:", "Failure:" lines).
class ObjectiveWindow {
public:
    enum class State { Hidden, Briefing, Achieved, Failed };

    void Load(const Config& cfg, std::string_view gameName, std::string_view levelId, const Font* font);
    void Show(State s);
    State state() const { return state_; }
    bool showing() const { return state_ != State::Hidden; }

    // A click or Space/Enter dismisses the panel; returns the state that was dismissed.
    State HandleInput(const Input& in);
    void Draw(Renderer2D& r);

private:
    struct Page {
        std::unique_ptr<Image> image;
        float x = 0, y = 0;
        std::string text;
    };
    Page& PageFor(State s) { return s == State::Achieved ? achieved_ : s == State::Failed ? failed_ : briefing_; }
    const Page& PanelFor(State s) const;
    void ShowPage();
    int PageCount() const;
    int page_ = 0;

    Page briefing_, achieved_, failed_;
    TextWindow window_;
    State state_ = State::Hidden;
};

}  // namespace rr
