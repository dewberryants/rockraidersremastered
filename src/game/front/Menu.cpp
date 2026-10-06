#include "game/front/Menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/Log.h"
#include "fs/Path.h"
#include "game/Sfx.h"

namespace rr::front {

namespace {
int ToInt(std::string_view s) { return std::atoi(std::string(s).c_str()); }
float ToFloat(std::string_view s) { return float(std::atof(std::string(s).c_str())); }

ItemType ParseItemType(std::string_view s) {
    if (EqualsNoCase(s, "cycle")) return ItemType::Cycle;
    if (EqualsNoCase(s, "trigger")) return ItemType::Trigger;
    if (EqualsNoCase(s, "textinput")) return ItemType::TextInput;
    if (EqualsNoCase(s, "slider")) return ItemType::Slider;
    if (EqualsNoCase(s, "realslider")) return ItemType::RealSlider;
    return ItemType::Next;
}

// "MenuN" -> menu index N-1.
int MenuLinkIndex(std::string_view s) { return s.size() > 4 ? ToInt(s.substr(4)) - 1 : -1; }

bool Inside(float px, float py, float x, float y, float w, float h) {
    // The original's test is inclusive on all edges.
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

int StrWidth(const Font* f, std::string_view s) { return f ? f->StringWidth(s) : 0; }
int FontHeight(const Font* f) { return f ? f->height() : 0; }
}  // namespace

std::string ReplaceUnderscores(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

int MenuSet::IndexOf(std::string_view title) const {
    const std::string wanted = ReplaceUnderscores(title);
    for (size_t i = 0; i < menus.size(); ++i)
        if (EqualsNoCase(menus[i]->title, wanted)) return int(i);
    return -1;
}

const Image* AssetCache::LoadImage(std::string_view path) {
    if (path.empty()) return nullptr;
    const std::string key = NormalizeGamePath(path);
    auto it = images_.find(key);
    if (it != images_.end()) return it->second.get();
    auto img = Image::Load(path);
    const Image* raw = img.get();
    images_[key] = std::move(img);
    return raw;
}

const Font* AssetCache::LoadFont(std::string_view path) {
    if (path.empty()) return nullptr;
    const std::string key = NormalizeGamePath(path);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second.get();
    auto font = Font::Load(path);
    const Font* raw = font.get();
    fonts_[key] = std::move(font);
    return raw;
}

std::unique_ptr<MenuSet> LoadMenuSet(const Config& cfg, std::string_view gameName, std::string_view name,
                                     std::vector<Binding> bindings, AssetCache& cache) {
    auto set = std::make_unique<MenuSet>();
    const std::string base = "Menu::" + std::string(name);
    const int count = cfg.GetInt(Config::Id(gameName, base, "MenuCount")).value_or(0);
    auto key = [&](const std::string& menuPath, std::string_view k) { return Config::Id(gameName, menuPath, k); };

    // Pass 1: create the menus so that "Next" items can link to any of them.
    for (int m = 0; m < count; ++m) {
        auto menu = std::make_unique<Menu>();
        menu->configPath = base + "::Menu" + std::to_string(m + 1);
        const std::string& p = menu->configPath;
        const std::string rawTitle = cfg.GetString(key(p, "Title"), "");
        menu->fullName = ReplaceUnderscores(cfg.GetString(key(p, "FullName"), ""));
        const auto pos = SplitTokens(cfg.GetString(key(p, "Position"), ""), ":");
        if (pos.size() >= 2) {
            menu->x = ToInt(pos[0]);
            menu->y = ToInt(pos[1]);
        }
        menu->menuFont = cache.LoadFont(cfg.GetString(key(p, "MenuFont"), ""));
        menu->autoCentre = cfg.GetBool(key(p, "AutoCenter")) == BoolTri::True;
        menu->displayTitle = cfg.GetBool(key(p, "DisplayTitle")) == BoolTri::True;
        if (auto anchored = cfg.GetString(key(p, "Anchored"))) {
            const auto a = SplitTokens(*anchored, ":");
            menu->anchored = true;
            if (a.size() >= 2) {
                menu->anchorX = ToInt(a[0]);
                menu->anchorY = ToInt(a[1]);
            }
        }
        // The original measured the title before replacing underscores.
        if (menu->autoCentre && !rawTitle.empty()) menu->titleCentre = -(StrWidth(menu->menuFont, rawTitle) / 2);
        menu->canScroll = cfg.GetBool(key(p, "CanScroll")) == BoolTri::True;
        menu->title = ReplaceUnderscores(rawTitle);

        // "MenuImage file[:x:y[:trans]]": with a position the image is drawn there, otherwise as a
        // (possibly scrolling) full-screen background.
        for (int dark = 0; dark < 2; ++dark) {
            const auto spec = cfg.GetString(key(p, dark ? "MenuImageDark" : "MenuImage"));
            if (!spec) continue;
            const auto parts = SplitTokens(*spec, ":");
            if (parts.size() > 2) {
                menu->imagePositioned = true;
                menu->imageX = ToFloat(parts[1]);
                menu->imageY = ToFloat(parts[2]);
            }
            const Image* img = cache.LoadImage(parts[0]);
            if (img && parts.size() == 4 && ToInt(parts[3]) == 1) {
                // Cached images are shared; transparency is set on the cache entry.
                auto* mut = const_cast<Image*>(img);
                if (dark) mut->SetupTrans({0, 0, 0}, {0, 0, 0});
                else mut->SetPenZeroTrans();
            }
            (dark ? menu->imageDark : menu->image) = img;
        }
        menu->playRandom = cfg.GetBool(key(p, "PlayRandom")) == BoolTri::True;
        set->menus.push_back(std::move(menu));
    }

    // Pass 2: overlays and items.
    size_t nextBinding = 0;
    auto takeBinding = [&]() -> Binding {
        if (nextBinding < bindings.size()) return bindings[nextBinding++];
        return Binding{};
    };
    for (int m = 0; m < count; ++m) {
        Menu& menu = *set->menus[size_t(m)];
        const std::string& p = menu.configPath;
        const Font* lo = cache.LoadFont(cfg.GetString(key(p, "LoFont"), ""));
        const Font* hi = cache.LoadFont(cfg.GetString(key(p, "HiFont"), ""));
        const int itemCount = cfg.GetInt(key(p, "ItemCount")).value_or(0);

        for (int o = 1;; ++o) {
            auto spec = cfg.GetString(key(p, "Overlay" + std::to_string(o)));
            if (!spec) spec = cfg.GetString(key(p, "!Overlay" + std::to_string(o)));
            if (!spec) break;
            const auto parts = SplitTokens(*spec, ":");
            MenuOverlay ov;
            ov.flicPath = std::string(parts[0]);
            if (parts.size() > 1 && !GetSfx().GetType(parts[1], ov.sfx)) ov.sfx = SFX_NULL;
            if (parts.size() > 3) {
                ov.x = ToInt(parts[2]);
                ov.y = ToInt(parts[3]);
            }
            menu.overlays.push_back(ov);
        }

        for (int i = 1; i <= itemCount; ++i) {
            const auto value = cfg.GetString(key(p, "Item" + std::to_string(i)));
            if (!value) continue;
            const auto t = SplitTokens(*value, ":");
            if (t.empty()) continue;
            const ItemType type = ParseItemType(t[0]);
            auto item = std::make_unique<MenuItem>();
            item->type = type;
            item->loFont = lo;
            item->hiFont = hi;
            auto field = [&](size_t n) -> std::string_view { return n < t.size() ? t[n] : std::string_view(); };
            item->x = ToInt(field(1));
            item->y = ToInt(field(2));

            auto setBanner = [&](std::string_view banner) {
                item->banner = ReplaceUnderscores(banner);
                if (menu.autoCentre && lo && hi) {
                    item->loCentre = -(StrWidth(lo, item->banner) / 2);
                    item->hiCentre = -(StrWidth(hi, item->banner) / 2);
                }
            };
            auto setImages = [&](std::string_view loName, std::string_view hiName, std::string_view tip) {
                item->isImage = true;
                item->loImage = cache.LoadImage(loName);
                item->hiImage = cache.LoadImage(hiName);
                item->toolTip = std::string(tip);
                if (menu.autoCentre && item->loImage && item->hiImage) {
                    item->loCentre = -(item->loImage->width() / 2);
                    item->hiCentre = -(item->hiImage->width() / 2);
                }
            };

            switch (type) {
                case ItemType::Cycle: {
                    Binding b = takeBinding();
                    item->cycle.value = b.intValue;
                    item->cycle.callback = b.onInt;
                    item->cycle.x2 = ToInt(field(3));
                    item->cycle.y2 = ToInt(field(4));
                    setBanner(field(5));
                    const int n = ToInt(field(6));
                    for (int k = 0; k < n && size_t(7 + k) < t.size(); ++k)
                        item->cycle.names.push_back(ReplaceUnderscores(t[size_t(7 + k)]));
                    break;
                }
                case ItemType::Trigger: {
                    Binding b = takeBinding();
                    item->trigger.value = b.intValue;
                    item->trigger.callback = b.onTrigger;
                    if (t.size() == 8) {  // Trigger:x:y:lo.bmp:hi.bmp:dull.bmp:tooltip:end
                        item->trigger.end = ToInt(t[7]) == 1;
                        setImages(t[3], t[4], t[6]);
                    } else if (t.size() == 5) {  // Trigger:x:y:banner:end
                        item->trigger.end = ToInt(t[4]) == 1;
                        setBanner(t[3]);
                    } else {
                        continue;
                    }
                    break;
                }
                case ItemType::Slider: {
                    Binding b = takeBinding();
                    item->slider.value = b.intValue;
                    item->slider.callback = b.onInt;
                    item->slider.x2 = ToInt(field(3));
                    item->slider.y2 = ToInt(field(4));
                    item->slider.min = ToInt(field(6));
                    item->slider.max = ToInt(field(7));
                    if (t.size() == 16) {
                        for (size_t k = 0; k < 8; ++k) {
                            const Image* img = cache.LoadImage(t[8 + k]);
                            if (img) const_cast<Image*>(img)->SetPenZeroTrans();
                            item->slider.images[k] = img;
                        }
                    }
                    if (item->slider.max <= item->slider.min) continue;
                    setBanner(field(5));
                    break;
                }
                case ItemType::RealSlider: {
                    Binding b = takeBinding();
                    item->real.value = b.realValue;
                    item->real.callback = b.onReal;
                    item->real.x2 = ToInt(field(3));
                    item->real.y2 = ToInt(field(4));
                    item->real.min = ToFloat(field(6));
                    item->real.max = ToFloat(field(7));
                    item->real.step = ToFloat(field(8));
                    if (item->real.max <= item->real.min) continue;
                    setBanner(field(5));
                    break;
                }
                case ItemType::Next: {
                    if (t.size() == 8) {  // Next:x:y:lo.bmp:hi.bmp:dull.bmp:tooltip:MenuN
                        item->next = set->menu(MenuLinkIndex(t[7]));
                        setImages(t[3], t[4], t[6]);
                    } else if (t.size() == 6) {  // Next:x:y:banner:MenuN:NotInTuto
                        item->next = set->menu(MenuLinkIndex(t[4]));
                        item->notInTutorial = true;
                        setBanner(t[3]);
                    } else {  // Next:x:y:banner:MenuN (empty link allowed)
                        item->next = field(4).empty() ? nullptr : set->menu(MenuLinkIndex(field(4)));
                        setBanner(field(3));
                    }
                    break;
                }
                case ItemType::TextInput:
                case ItemType::Select:
                    continue;  // text input was never configurable; select lists are built in code
            }
            menu.items.push_back(std::move(item));
        }
    }
    return set;
}

// --- Runtime ------------------------------------------------------------------------------

namespace {

// Hit test of a select list: -2 = "more" marker, -3 = "back" marker, entry index, or -1.
int SelectHitTest(const Menu& menu, const MenuItem& item, const MenuContext& ctx) {
    const SelectData& s = item.select;
    const Font* f = item.hiFont;
    const int h = FontHeight(f);
    if (Inside(ctx.mouseX, ctx.mouseY, float(s.upX + menu.x), float(s.upY + menu.y), float(StrWidth(f, s.upText)),
               float(h)))
        return -2;
    if (Inside(ctx.mouseX, ctx.mouseY, float(s.downX + menu.x), float(s.downY + menu.y),
               float(StrWidth(f, s.downText)), float(h)))
        return -3;
    for (int i = 0; i < s.visibleCount; ++i) {
        const int idx = s.scroll + i;
        if (idx >= int(s.entries.size())) break;
        const SelectEntry& e = s.entries[size_t(idx)];
        if (!e.enabled) continue;
        float x = float(item.x + s.x + e.x + ctx.scrollX + menu.x);
        const float y = float(s.lineHeight * i + item.y + s.y + e.y + ctx.scrollY + menu.y);
        const float w = float(e.widths[0]);
        if (menu.autoCentre) x -= float(int(w) / 2);
        if (Inside(ctx.mouseX, ctx.mouseY, x, y, w, float(e.heights[0]))) return idx;
    }
    return -1;
}

bool ItemBounds(const Menu& menu, int index, float& x, float& y, float& w, float& h) {
    const MenuItem& it = *menu.items[size_t(index)];
    const bool hovered = menu.hovered == index;
    if (it.type == ItemType::Select) return false;
    if (it.type == ItemType::Slider) {
        const SliderData& s = it.slider;
        const float bx = float(it.hiCentre + menu.x + it.x);
        const float by = float(menu.y + it.y);
        const Font* f = hovered ? it.hiFont : it.loFont;
        const float bw = float(StrWidth(f, it.banner)), bh = float(FontHeight(f));
        const float sx = float(s.x2 + it.x + menu.x), sy = float(s.y2 + it.y + menu.y);
        const float capW = s.images[3] ? float(s.images[3]->width()) : 0.0f;
        const float plusW = s.images[4] ? float(s.images[4]->width()) : 0.0f;
        const float left = std::min(sx, bx), top = std::min(sy, by);
        const float right = std::max(sx + capW + 104.0f + plusW, bx + bw);
        const float bottom = std::max(by + bh, sy + 20.0f);
        x = left;
        y = top;
        w = right - left;
        h = bottom - top;
        return true;
    }
    x = float(menu.x + it.hiCentre + it.x);
    y = float(it.y + menu.y);
    if (it.isImage) {
        const Image* img = hovered ? it.hiImage : it.loImage;
        w = img ? float(img->width()) : 0.0f;
        h = img ? float(img->height()) : 0.0f;
        return true;
    }
    const Font* f = hovered ? it.hiFont : it.loFont;
    w = float(StrWidth(f, it.banner));
    h = float(FontHeight(f));
    return true;
}

// Mirrors the original scan: it stops at the first item without bounds unless it is a select list
// under the mouse.
bool FindItemUnderMouse(const Menu& menu, const MenuContext& ctx, int& index) {
    for (int i = 0; i < int(menu.items.size()); ++i) {
        float x, y, w, h;
        if (!ItemBounds(menu, i, x, y, w, h)) {
            const MenuItem& it = *menu.items[size_t(i)];
            if (it.type == ItemType::Select && SelectHitTest(menu, it, ctx) != -1) {
                index = i;
                return true;
            }
            return false;
        }
        if (Inside(ctx.mouseX, ctx.mouseY, x, y, w, h)) {
            index = i;
            return true;
        }
    }
    return false;
}

void SliderClick(const Menu& menu, const MenuItem& it, const MenuContext& ctx) {
    const SliderData& s = it.slider;
    const float x = float(menu.x + s.x2 + it.x), y = float(menu.y + it.y + s.y2);
    if (Inside(ctx.mouseX, ctx.mouseY, x, y, 100.0f, 20.0f)) {
        // The original stores range * fraction (truncated) without adding the minimum back;
        // every retail slider has a minimum of 0.
        const float t = std::clamp((float(int(ctx.mouseX)) - x) * 0.01f, 0.0f, 1.0f);
        *s.value = int(float(s.max - s.min) * t);
        return;
    }
    const Image* minus = s.images[5];
    const Image* plus = s.images[4];
    const Image* rightCap = s.images[3];
    if (!minus || !plus || !rightCap) return;
    if (Inside(ctx.mouseX, ctx.mouseY, x - float(minus->width()) - 4.0f, y, float(minus->width()),
               float(minus->height()))) {
        if (*s.value > s.min) --*s.value;
    } else if (Inside(ctx.mouseX, ctx.mouseY, x + float(rightCap->width()) + 104.0f, y, float(plus->width()),
                      float(plus->height()))) {
        if (*s.value < s.max) ++*s.value;
    }
}

}  // namespace

Menu* UpdateMenuInput(Menu& menu, MenuContext& ctx, float elapsed) {
    menu.hovered = -1;
    int index = -1;
    MenuItem* item = nullptr;
    if (FindItemUnderMouse(menu, ctx, index)) {
        menu.hovered = index;
        item = menu.items[size_t(index)].get();
    }
    if (!item) return &menu;
    if (item->notInTutorial && ctx.tutorialActive) return &menu;

    const bool clicked = ctx.mouseReleased;
    bool handled = false;
    if (ctx.rightKey || clicked) {
        if (ctx.playOkay) ctx.playOkay();
        switch (item->type) {
            case ItemType::Cycle:
                if (item->cycle.value && !item->cycle.names.empty()) {
                    *item->cycle.value = (*item->cycle.value + 1) % int(item->cycle.names.size());
                    if (item->cycle.callback) item->cycle.callback(*item->cycle.value);
                }
                handled = clicked;
                break;
            case ItemType::TextInput:
                item->text.caret = std::min(item->text.caret + 1, std::max(0, item->text.maxLength - 1));
                handled = clicked;
                break;
            case ItemType::Slider:
                if (!item->slider.value) break;
                if (clicked) SliderClick(menu, *item, ctx);
                else *item->slider.value = std::min(*item->slider.value + 1, item->slider.max);
                if (item->slider.callback) item->slider.callback(*item->slider.value);
                break;
            case ItemType::RealSlider:
                if (item->real.value) {
                    *item->real.value = std::min(*item->real.value + item->real.step, item->real.max);
                    if (item->real.callback) item->real.callback(*item->real.value);
                }
                handled = clicked;
                break;
            case ItemType::Select: {
                if (!clicked) break;
                SelectData& s = item->select;
                const int hit = SelectHitTest(menu, *item, ctx);
                if (hit == -3) {
                    if (s.scroll > 0) --s.scroll;
                } else if (hit == -2) {
                    if (s.scroll < int(s.entries.size()) - s.visibleCount) ++s.scroll;
                } else if (hit >= 0) {
                    if (s.value) *s.value = hit;
                    if (s.callback) s.callback(elapsed, hit);
                    if (s.next) return s.next;
                    menu.closed = true;
                }
                break;
            }
            default:
                break;
        }
    }
    if (ctx.leftKey) {
        switch (item->type) {
            case ItemType::Cycle:
                if (item->cycle.value && !item->cycle.names.empty()) {
                    if (--*item->cycle.value < 0) *item->cycle.value = int(item->cycle.names.size()) - 1;
                }
                break;
            case ItemType::TextInput:
                item->text.caret = std::max(0, item->text.caret - 1);
                break;
            case ItemType::Slider:
                if (item->slider.value) {
                    *item->slider.value = std::max(*item->slider.value - 1, item->slider.min);
                    if (item->slider.callback) item->slider.callback(*item->slider.value);
                }
                break;
            case ItemType::RealSlider:
                if (item->real.value) {
                    *item->real.value = std::max(*item->real.value - item->real.step, item->real.min);
                    if (item->real.callback) item->real.callback(*item->real.value);
                }
                break;
            default:
                break;
        }
    }
    if (clicked && !handled) {
        if (item->type == ItemType::Trigger) {
            if (item->trigger.callback) item->trigger.callback();
            if (item->trigger.end) {
                menu.closed = true;
                if (item->trigger.value) *item->trigger.value = 1;
            }
        } else if (item->type == ItemType::Next) {
            return item->next;
        }
    }
    return &menu;
}

void UpdateMenuScroll(const Menu& menu, MenuContext& ctx) {
    if (!menu.image || !menu.canScroll) {
        ctx.scrollX = ctx.scrollY = 0;
        return;
    }
    const float my = ctx.mouseY;
    if (my <= 280.0f && my >= 200.0f) return;
    if (my < 480.0f && !ctx.input->leftDown() && !ctx.input->rightDown()) {
        // Speed grows with the square of the distance from the screen centre.
        const float t = my * 0.2083333f - 50.0f;
        float speed = t * t * 0.02f;
        if (my < 240.0f) speed = -speed;
        ctx.scrollY += int(speed * -0.25f);
    }
    if (ctx.scrollY > 0) ctx.scrollY = 0;
    if (ctx.scrollY - 480 < -menu.image->height()) ctx.scrollY = 480 - menu.image->height();
}

namespace {
void DrawSlider(Renderer2D& r, const SliderData& s, float x, float y, int valueIndex, int range,
                const MenuContext& ctx) {
    const float fill = float((unsigned(valueIndex) * 100u) / unsigned(std::max(1, range)));
    const Image* off = s.images[0];
    const Image* on = s.images[1];
    if (on) {
        const RectF src{0, 0, fill, 20};
        on->Draw(r, x, y, &src);
    }
    if (off) {
        const RectF src{fill, 0, 100.0f - fill, 20};
        off->Draw(r, x + fill, y, &src);
    }
    if (s.images[2]) s.images[2]->Draw(r, x, y);
    if (s.images[3]) s.images[3]->Draw(r, x + 100.0f, y);
    if (s.images[5] && s.images[7]) {
        const Image* m = s.images[5];
        const float mx = x - float(m->width() + 4);
        const bool over = Inside(ctx.mouseX, ctx.mouseY, mx, y, float(m->width()), float(m->height()));
        (over ? s.images[7] : s.images[5])->Draw(r, mx, y);
    }
    if (s.images[4] && s.images[6] && s.images[3]) {
        const Image* p = s.images[4];
        const float px = float(s.images[3]->width()) + x + 104.0f;
        const bool over = Inside(ctx.mouseX, ctx.mouseY, px, y, float(p->width()), float(p->height()));
        (over ? s.images[6] : s.images[4])->Draw(r, px, y);
    }
}

void DrawSelectEntry(Renderer2D& r, float x, float y, const Font* font, const SelectEntry& e, int imageType) {
    if (e.isText) {
        if (font) font->Draw(r, x, y, e.text);
        return;
    }
    const Image* img = e.images[size_t(imageType)];
    // Only drawn when at least partly on the 640x480 screen.
    if (img && x < 640.0f && x > -float(img->width()) && y > -float(img->height()) && y < 480.0f) img->Draw(r, x, y);
}
}  // namespace

void DrawMenuBackground(const Menu& menu, const MenuContext& ctx, Renderer2D& r) {
    // Positioned image, or a 640x480 window into a (scrollable) tall image.
    if (!menu.image) return;
    if (menu.imagePositioned) {
        menu.image->Draw(r, menu.imageX, menu.imageY);
    } else {
        const RectF src{float(-ctx.scrollX), float(-ctx.scrollY), 640.0f, 480.0f};
        menu.image->Draw(r, 0, 0, &src);
    }
}

void DrawMenuItems(Menu& menu, MenuContext& ctx, Renderer2D& r, float elapsed, int screenWidth) {
    if (menu.displayTitle && menu.menuFont) {
        const int w = StrWidth(menu.menuFont, menu.title);
        menu.menuFont->Draw(r, float(screenWidth / 2 - w / 2), float(menu.anchorY + menu.y), menu.title);
    }

    for (int i = 0; i < int(menu.items.size()); ++i) {
        MenuItem& it = *menu.items[size_t(i)];
        const bool hovered = (i == menu.hovered) && !(it.notInTutorial && ctx.tutorialActive);
        const Font* font = hovered ? it.hiFont : it.loFont;
        const int centre = hovered ? it.hiCentre : it.loCentre;
        const float bx = float(centre + it.x + menu.x), by = float(it.y + menu.y);
        if (font && !it.isImage && !it.banner.empty()) font->Draw(r, bx, by, it.banner);

        switch (it.type) {
            case ItemType::Cycle:
                if (font && it.cycle.value && *it.cycle.value >= 0 && *it.cycle.value < int(it.cycle.names.size()))
                    font->Draw(r, float(it.cycle.x2 + centre + it.x + menu.x), float(it.cycle.y2 + it.y + menu.y),
                               it.cycle.names[size_t(*it.cycle.value)]);
                break;
            case ItemType::TextInput:
                if (font)
                    font->Draw(r, float(it.text.x2 + centre + it.x + menu.x), float(it.text.y2 + it.y + menu.y),
                               it.text.buffer);
                break;
            case ItemType::Slider:
                if (it.slider.value)
                    DrawSlider(r, it.slider, float(it.slider.x2 + it.x + menu.x), float(it.slider.y2 + it.y + menu.y),
                               *it.slider.value - it.slider.min, it.slider.max - it.slider.min, ctx);
                break;
            case ItemType::RealSlider:
                if (font && it.real.value) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.2f", double(*it.real.value));
                    font->Draw(r, float(it.real.x2 + centre + it.x + menu.x), float(it.real.y2 + it.y + menu.y), buf);
                }
                break;
            case ItemType::Select: {
                SelectData& s = it.select;
                const int hit = hovered ? SelectHitTest(menu, it, ctx) : -1;
                for (int k = 0; k < s.visibleCount; ++k) {
                    const int idx = s.scroll + k;
                    if (idx >= int(s.entries.size())) break;
                    const SelectEntry& e = s.entries[size_t(idx)];
                    const int centreX = menu.autoCentre ? -(e.widths[0] / 2) : 0;
                    const float ex = float(s.x + e.x + centreX + it.x + menu.x + ctx.scrollX);
                    const float ey = float(s.lineHeight * k + s.y + e.y + it.y + menu.y + ctx.scrollY);
                    int imageType = e.enabled ? 1 : 2;
                    const Font* entryFont = it.loFont;
                    if (hit == idx) {
                        imageType = e.enabled ? 0 : 2;
                        entryFont = it.hiFont;
                        if (s.callback) s.callback(elapsed, idx);
                    }
                    DrawSelectEntry(r, ex, ey, entryFont, e, imageType);
                }
                if (s.scroll < int(s.entries.size()) - s.visibleCount) {
                    const Font* f = hit == -2 ? it.hiFont : it.loFont;
                    if (f) f->Draw(r, float(s.upX + menu.x), float(s.upY + menu.y), s.upText);
                }
                if (s.scroll > 0) {
                    const Font* f = hit == -3 ? it.hiFont : it.loFont;
                    if (f) f->Draw(r, float(s.downX + menu.x), float(s.downY + menu.y), s.downText);
                }
                break;
            }
            default:
                break;
        }
    }

}

void DrawMenuButtons(const Menu& menu, MenuContext& ctx, Renderer2D& r) {
    // Bitmap buttons are drawn last, without centring offsets.
    for (int i = 0; i < int(menu.items.size()); ++i) {
        const MenuItem& it = *menu.items[size_t(i)];
        if (!it.isImage) continue;
        const float x = float(it.x + menu.x), y = float(it.y + menu.y);
        if (menu.hovered == i) {
            if (it.hiImage) it.hiImage->Draw(r, x, y);
            if (ctx.showToolTip && !it.toolTip.empty()) ctx.showToolTip(it.toolTip);
        } else if (it.loImage) {
            it.loImage->Draw(r, x, y);
        }
    }
}

}  // namespace rr::front
