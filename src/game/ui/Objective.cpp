#include "game/ui/Objective.h"

#include <algorithm>
#include <cstdlib>

#include "fs/FileSystem.h"
#include "fs/Path.h"
#include "platform/Input.h"

namespace rr {

namespace {

constexpr char kPageBreak = '\a';  // TextWindow page separator
constexpr char kBackslash = '\\';

// "<image>,<x>,<y>" (an optional ",r,g,b" names a transparent colour instead of pen zero).
void LoadImage(const Config& cfg, const std::string& id, std::unique_ptr<Image>& image, float& x, float& y) {
    const auto v = cfg.GetString(id);
    if (!v) return;
    const auto f = SplitTokens(*v, ",");
    image = Image::Load(f[0]);
    if (!image) return;
    if (f.size() >= 6) {
        const Rgb8 key{uint8_t(std::atoi(std::string(f[3]).c_str())), uint8_t(std::atoi(std::string(f[4]).c_str())),
                       uint8_t(std::atoi(std::string(f[5]).c_str()))};
        image->SetupTrans(key, key);
    } else {
        image->SetPenZeroTrans();
    }
    if (f.size() >= 3) {
        x = float(std::atoi(std::string(f[1]).c_str()));
        y = float(std::atoi(std::string(f[2]).c_str()));
    }
}

// Decodes the file's escapes: backslash-a starts a page, backslash-n a line.
std::string DecodeEscapes(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == kBackslash && i + 1 < text.size() && (text[i + 1] == 'a' || text[i + 1] == 'n')) {
            out += text[i + 1] == 'a' ? kPageBreak : '\n';
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

// The level's "[name]" section: "Key: text" lines.
std::string SectionLine(const std::string& file, std::string_view section, std::string_view key) {
    bool inSection = false;
    size_t pos = 0;
    while (pos < file.size()) {
        size_t end = file.find('\n', pos);
        if (end == std::string::npos) end = file.size();
        std::string_view line(file.data() + pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        if (!line.empty() && line.front() == '[') {
            inSection = line.size() > 2 && EqualsNoCase(line.substr(1, line.size() - 2), section);
            continue;
        }
        if (!inSection) continue;
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos || !EqualsNoCase(line.substr(0, colon), key)) continue;
        std::string text(line.substr(colon + 1));
        text.erase(0, text.find_first_not_of(" \t"));
        std::replace(text.begin(), text.end(), '_', ' ');
        return DecodeEscapes(text);
    }
    return {};
}

}  // namespace

void ObjectiveWindow::Load(const Config& cfg, std::string_view gameName, std::string_view levelId, const Font* font) {
    const std::string base = Config::Id(gameName, levelId);
    LoadImage(cfg, Config::Id(base, "ObjectiveImage640x480"), briefing_.image, briefing_.x, briefing_.y);
    LoadImage(cfg, Config::Id(base, "ObjectiveAcheivedImage640x480"), achieved_.image, achieved_.x, achieved_.y);
    LoadImage(cfg, Config::Id(base, "ObjectiveFailedImage640x480"), failed_.image, failed_.x, failed_.y);
    // TODO(objective): ObjectiveAcheivedAVI, advisor animations, page buttons, mission sample.
    const auto file = Fs().ReadText(cfg.GetString(Config::Id(base, "ObjectiveText"), ""));
    const size_t sep = levelId.rfind("::");
    const std::string_view section = sep == std::string_view::npos ? levelId : levelId.substr(sep + 2);
    if (file) {
        briefing_.text = SectionLine(*file, section, "Objective");
        achieved_.text = SectionLine(*file, section, "Completion");
        failed_.text = SectionLine(*file, section, "Failure");
    }
    window_.SetFont(font);
    window_.EnableCentering(false);
    state_ = State::Hidden;
}

void ObjectiveWindow::Show(State s) {
    state_ = s;
    if (s == State::Hidden) return;
    page_ = 0;
    ShowPage();
}

void ObjectiveWindow::ShowPage() {
    const Page& p = PageFor(state_);
    const Page& panel = PanelFor(state_);
    // The text area inside the panel image (Objective_LoadObjectiveText): 60,60 390 x 180.
    window_.SetArea({panel.x + 60.0f, panel.y + 60.0f, 390.0f, 180.0f});
    window_.Clear();
    size_t start = 0;
    for (int i = 0; i < page_ && start != std::string::npos; ++i) {
        start = p.text.find(kPageBreak, start);
        if (start != std::string::npos) ++start;
    }
    if (start == std::string::npos) return;
    const size_t end = p.text.find(kPageBreak, start);
    window_.Print(std::string_view(p.text).substr(start, end == std::string::npos ? std::string::npos : end - start));
}

int ObjectiveWindow::PageCount() const {
    const Page& p = state_ == State::Achieved ? achieved_ : state_ == State::Failed ? failed_ : briefing_;
    return 1 + int(std::count(p.text.begin(), p.text.end(), kPageBreak));
}

ObjectiveWindow::State ObjectiveWindow::HandleInput(const Input& in) {
    if (!showing()) return State::Hidden;
    if (in.leftReleased() || in.KeyPressed(SDL_SCANCODE_SPACE) || in.KeyPressed(SDL_SCANCODE_RETURN)) {
        // Each click turns a page; the last page closes the panel.
        // TODO(objective): the next/back page buttons of the original.
        if (page_ + 1 < PageCount()) {
            ++page_;
            ShowPage();
            return State::Hidden;
        }
        const State s = state_;
        state_ = State::Hidden;
        return s;
    }
    return State::Hidden;
}

void ObjectiveWindow::Draw(Renderer2D& r) {
    if (!showing()) return;
    const Page& p = PanelFor(state_);
    if (p.image) p.image->Draw(r, p.x, p.y);
    window_.Draw(r);
}

const ObjectiveWindow::Page& ObjectiveWindow::PanelFor(State s) const {
    // Levels only name the briefing image; the result pages are shown on the same panel.
    const Page& p = s == State::Achieved ? achieved_ : s == State::Failed ? failed_ : briefing_;
    return p.image ? p : briefing_;
}

}  // namespace rr
