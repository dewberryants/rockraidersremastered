#include "ui/TextWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace rr {

float TextWindow::Draw(Renderer2D& r, int scrollFromEnd, ColorF tint) const {
    if (!font_ || font_->height() <= 0) return area_.y;
    // Only the first page (up to '\a') is shown.
    const std::string_view text = std::string_view(text_).substr(0, text_.find('\a'));

    struct Line {
        size_t begin, end;
        int width;
    };
    std::vector<Line> lines;
    size_t lineStart = 0, lastBreak = std::string_view::npos;
    int width = 0, widthAtBreak = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            lines.push_back({lineStart, i, width});
            lineStart = i + 1;
            lastBreak = std::string_view::npos;
            width = 0;
            continue;
        }
        const int cw = font_->CharWidth(c);
        if (std::isspace(c)) {
            lastBreak = i;
            widthAtBreak = width;
        }
        width += cw;
        if (float(width) > area_.w && lastBreak != std::string_view::npos && lastBreak >= lineStart) {
            lines.push_back({lineStart, lastBreak, widthAtBreak});
            width -= widthAtBreak + font_->CharWidth(static_cast<unsigned char>(text[lastBreak]));
            lineStart = lastBreak + 1;
            lastBreak = std::string_view::npos;
        }
    }
    if (lineStart < text.size()) lines.push_back({lineStart, text.size(), width});

    const int lineHeight = font_->height();
    const int visible = std::max(1, int(area_.h) / lineHeight);
    const int total = int(lines.size());
    const int maxScroll = std::max(0, total - visible);
    const int first = std::max(0, total - visible - std::clamp(scrollFromEnd, 0, maxScroll));
    float y = area_.y;
    for (int i = first; i < total && i < first + visible; ++i) {
        const Line& l = lines[size_t(i)];
        const float x = centred_ ? area_.x + (area_.w - float(l.width)) * 0.5f : area_.x;
        font_->Draw(r, std::floor(x), y, text.substr(l.begin, l.end - l.begin), tint);
        y += float(lineHeight);
    }
    return y;
}

}  // namespace rr
