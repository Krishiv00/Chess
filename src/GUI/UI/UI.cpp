#include "GUI/UI/UI.hpp"

#include <cmath>
#include <sstream>

#include "GUI/UI/Widgets.hpp"

#pragma region Context


namespace {
    float Linear(uint8_t channel) {
        const float c = channel / 255.f;
        return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    float Luminance(sf::Color c) {
        return 0.2126f * Linear(c.r) + 0.7152f * Linear(c.g) + 0.0722f * Linear(c.b);
    }

    float Contrast(sf::Color a, sf::Color b) {
        const float la = Luminance(a), lb = Luminance(b);
        return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
    }

    sf::Color Lighten(sf::Color c, float amount) {
        const auto mix = [amount](uint8_t v) { return static_cast<uint8_t>(v + (255 - v) * amount); };
        return sf::Color(mix(c.r), mix(c.g), mix(c.b), c.a);
    }

    sf::Text MakeText(const sf::Font& font, const std::string& text, unsigned int size, bool bold) {
        sf::Text result(font, text, size);
        if (bold) result.setStyle(sf::Text::Style::Bold);
        return result;
    }
}

UI::Palette UI::Palette::FromAccent(sf::Color base) {
    Palette palette;

    sf::Color accent = base;
    for (int i = 0; i < 24 && Contrast(accent, palette.Panel) < 3.f; ++i) {
        accent = Lighten(accent, 0.08f);
    }

    accent.a = 255;

    const sf::Color dark(27, 26, 24);

    palette.Accent = accent;
    palette.AccentHover = Lighten(accent, 0.15f);
    palette.OnAccent = Contrast(accent, dark) >= Contrast(accent, sf::Color::White) ? dark : sf::Color::White;

    return palette;
}

float UI::Context::TextWidth(const std::string& text, unsigned int size, bool bold) const {
    const TextKey key{text, size, bold};

    if (const auto it = m_Widths.find(key); it != m_Widths.end()) return it->second;
    if (m_Widths.size() > 8192) m_Widths.clear();

    return m_Widths.emplace(key, MakeText(*Font, text, size, bold).getLocalBounds().size.x).first->second;
}

sf::Text& UI::Context::CachedText(const std::string& text, unsigned int size, bool bold) const {
    const TextKey key{text, size, bold};

    if (const auto it = m_Texts.find(key); it != m_Texts.end()) return it->second;
    if (m_Texts.size() > 2048) m_Texts.clear();

    sf::Text& created = m_Texts.try_emplace(key, *Font, text, size).first->second;
    if (bold) created.setStyle(sf::Text::Style::Bold);

    return created;
}

std::pair<float, float> UI::Context::CapMetrics(unsigned int size, bool bold) const {
    const unsigned int key = size << 1 | (bold ? 1u : 0u);

    const auto it = m_CapCache.find(key);
    if (it != m_CapCache.end()) return it->second;

    const sf::FloatRect bounds = MakeText(*Font, "H", size, bold).getLocalBounds();

    return m_CapCache.emplace(key, std::make_pair(bounds.position.y, bounds.size.y)).first->second;
}

std::vector<std::string> UI::Context::Wrap(const std::string& text, unsigned int size, float maxWidth, bool bold) const {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word, line;

    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + ' ' + word;

        if (!line.empty() && TextWidth(candidate, size, bold) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
    }

    if (!line.empty()) lines.push_back(line);

    return lines;
}

std::string UI::Context::Fit(const std::string& text, unsigned int size, float maxWidth, bool bold) const {
    if (TextWidth(text, size, bold) <= maxWidth) return text;

    std::string result = text;

    while (!result.empty() && TextWidth(result + "..", size, bold) > maxWidth) result.pop_back();

    return result + "..";
}

#pragma region Painter



namespace {
    sf::Vector2f Pos(sf::FloatRect r) {
        return r.position;
    }
    sf::Vector2f Size(sf::FloatRect r) {
        return r.size;
    }
}

UI::Painter::Painter(sf::RenderTarget& target, const Context& context) :
    m_Clip(sf::Vector2f(), sf::Vector2f(target.getSize())), Target(target), Ctx(context) {}

void UI::Painter::Fill(sf::FloatRect rect, sf::Color color) const {
    Utils::RenderQuad(Target, color, Pos(rect), Size(rect));
}

void UI::Painter::Round(sf::FloatRect rect, sf::Color color, float radius) const {
    Utils::RenderRoundedRect(Target, color, Pos(rect), Size(rect), radius);
}

void UI::Painter::Ring(sf::FloatRect rect, sf::Color color, float radius, float thickness) const {
    Utils::RenderRoundedFrame(Target, color, Pos(rect), Size(rect), radius, thickness);
}

void UI::Painter::Text(
    const std::string& text, sf::FloatRect box, unsigned int size, sf::Color color, Align align, bool bold
) const {
    if (text.empty()) return;

    sf::Text& drawable = Ctx.CachedText(text, size, bold);
    drawable.setFillColor(color);

    const sf::FloatRect bounds = drawable.getLocalBounds();
    const auto [capTop, capHeight] = Ctx.CapMetrics(size, bold);

    float x = box.position.x;

    if (align == Align::Center) x += (box.size.x - bounds.size.x) * 0.5f - bounds.position.x;
    else if (align == Align::Right) x += box.size.x - bounds.size.x - bounds.position.x;

    const float y = box.position.y + box.size.y * 0.5f - capTop - capHeight * 0.5f;

    drawable.setPosition(sf::Vector2f(std::round(x), std::round(y)));

    Target.draw(drawable);
}

void UI::Painter::Icon(UI::Icon icon, sf::FloatRect box, sf::Color color) const {
    const float cell = static_cast<float>(Ctx.Icons->getSize().y);
    const float tx = static_cast<float>(icon) * cell;

    const float left = box.position.x, top = box.position.y;
    const float right = left + box.size.x, bottom = top + box.size.y;

    const sf::Vertex vertices[] = {
        sf::Vertex(sf::Vector2f(left, top), color, sf::Vector2f(tx, 0.f)),
        sf::Vertex(sf::Vector2f(right, top), color, sf::Vector2f(tx + cell, 0.f)),
        sf::Vertex(sf::Vector2f(left, bottom), color, sf::Vector2f(tx, cell)),
        sf::Vertex(sf::Vector2f(right, bottom), color, sf::Vector2f(tx + cell, cell))
    };

    Target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip, Ctx.Icons);
}

UI::Painter::Clip::Clip(const Painter& painter, sf::FloatRect rect) :
    m_Painter(painter), m_PreviousView(painter.Target.getView()), m_PreviousClip(painter.m_Clip) {

    const float left = std::floor(rect.position.x), top = std::floor(rect.position.y);
    const float right = std::ceil(rect.position.x + rect.size.x), bottom = std::ceil(rect.position.y + rect.size.y);

    const std::optional<sf::FloatRect> clipped =
        painter.m_Clip.findIntersection(sf::FloatRect({left, top}, {right - left, bottom - top}));

    m_Visible = clipped && clipped->size.x >= 1.f && clipped->size.y >= 1.f;

    if (!m_Visible) return;

    const sf::Vector2f window(painter.Target.getSize());

    sf::View view(*clipped);
    view.setViewport(sf::FloatRect(
        {clipped->position.x / window.x, clipped->position.y / window.y},
        {clipped->size.x / window.x, clipped->size.y / window.y}
    ));

    painter.Target.setView(view);
    painter.m_Clip = *clipped;
}

UI::Painter::Clip::~Clip() {
    if (!m_Visible) return;

    m_Painter.Target.setView(m_PreviousView);
    m_Painter.m_Clip = m_PreviousClip;
}

#pragma region Widgets

namespace {
    using namespace UI;

    sf::Color Fade(sf::Color color, float factor) {
        return Utils::ConvertAlpha(color, factor);
    }

    sf::FloatRect Box(float x, float y, float w, float h) {
        return sf::FloatRect({x, y}, {w, h});
    }

    float Snap(float v) {
        return std::round(v);
    }
}

#pragma region Containers

void Container::Update(float deltaTime, sf::Vector2f mouse) {
    for (Child& child : m_Children) {
        if (child.Item->Visible()) child.Item->Update(deltaTime, mouse);
    }
}

void Container::Render(const Painter& painter) const {
    const sf::FloatRect& visible = painter.VisibleRect();

    for (const Child& child : m_Children) {
        if (!child.Item->Visible()) continue;
        if (!child.Item->Rect().findIntersection(visible)) continue;

        child.Item->Render(painter);
    }
}

bool Container::OnPressed(sf::Vector2f position) {
    bool taken = false;

    for (Child& child : m_Children) {
        if (!child.Item->Visible()) continue;

        if (child.Item->OnPressed(position) && !taken) {
            taken = true;
            m_Captured = child.Item.get();
        }
    }

    return taken;
}

void Container::OnReleased(sf::Vector2f position) {
    if (!m_Captured) return;

    Widget* captured = std::exchange(m_Captured, nullptr);
    captured->OnReleased(position);
}

bool Container::OnScroll(sf::Vector2f position, float delta) {
    for (Child& child : m_Children) {
        if (child.Item->Visible() && child.Item->OnScroll(position, delta)) return true;
    }

    return false;
}

void Container::OnText(char32_t unicode) {
    for (Child& child : m_Children) {
        if (child.Item->Visible()) child.Item->OnText(unicode);
    }
}

bool Container::OnKey(sf::Keyboard::Scancode key, bool control) {
    for (Child& child : m_Children) {
        if (child.Item->Visible() && child.Item->OnKey(key, control)) return true;
    }

    return false;
}

float Column::PreferredHeight(float width) const {
    const float inner = width - 2.f * m_Ctx.Px(m_PadX);

    float height = 2.f * m_Ctx.Px(m_PadY);
    int count = 0;

    for (const Child& child : m_Children) {
        if (!child.Item->Visible()) continue;

        height += child.Item->PreferredHeight(inner);
        ++count;
    }

    return height + std::max(0, count - 1) * m_Ctx.Px(m_Gap);
}

void Column::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);

    const float x = rect.position.x + m_Ctx.Px(m_PadX);
    const float width = rect.size.x - 2.f * m_Ctx.Px(m_PadX);

    float fixed = 2.f * m_Ctx.Px(m_PadY);
    int count = 0, flexible = 0;

    for (const Child& child : m_Children) {
        if (!child.Item->Visible()) continue;

        ++count;

        if (child.Flex) ++flexible;
        else fixed += child.Item->PreferredHeight(width);
    }

    fixed += std::max(0, count - 1) * m_Ctx.Px(m_Gap);

    const float flexHeight = flexible ? std::max(0.f, (rect.size.y - fixed) / static_cast<float>(flexible)) : 0.f;

    float y = rect.position.y + m_Ctx.Px(m_PadY);

    for (Child& child : m_Children) {
        if (!child.Item->Visible()) continue;

        const float height = child.Flex ? flexHeight : child.Item->PreferredHeight(width);

        child.Item->SetRect(Box(x, y, width, height));
        y += height + m_Ctx.Px(m_Gap);
    }
}

void Column::Render(const Painter& painter) const {
    if (m_Fill) painter.Round(m_Rect, *m_Fill, m_Ctx.Px(m_Radius));
    if (m_Border) painter.Ring(m_Rect, *m_Border, m_Ctx.Px(m_Radius), std::max(1.f, m_Ctx.Px(1.f)));

    Container::Render(painter);
}

namespace {
    std::vector<float> RowWidths(const std::vector<const Widget*>& items, float total, float gap) {
        float fixed = 0.f;
        int shared = 0;

        for (const Widget* item : items) {
            if (item->PreferredWidth() > 0.f) fixed += item->PreferredWidth();
            else ++shared;
        }

        fixed += std::max(0, static_cast<int>(items.size()) - 1) * gap;

        const float share = shared ? std::max(0.f, (total - fixed) / static_cast<float>(shared)) : 0.f;

        std::vector<float> widths;

        for (const Widget* item : items) {
            widths.push_back(item->PreferredWidth() > 0.f ? item->PreferredWidth() : share);
        }

        return widths;
    }
}

float Row::PreferredHeight(float width) const {
    std::vector<const Widget*> items;

    for (const Child& child : m_Children) {
        if (child.Item->Visible()) items.push_back(child.Item.get());
    }

    const std::vector<float> widths = RowWidths(items, width, m_Ctx.Px(m_Gap));

    float height = 0.f;

    for (std::size_t i = 0; i < items.size(); ++i) height = std::max(height, items[i]->PreferredHeight(widths[i]));

    return height;
}

void Row::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);

    std::vector<Widget*> items;

    for (Child& child : m_Children) {
        if (child.Item->Visible()) items.push_back(child.Item.get());
    }

    const std::vector<float> widths = RowWidths({items.begin(), items.end()}, rect.size.x, m_Ctx.Px(m_Gap));

    float x = rect.position.x;

    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i]->SetRect(Box(x, rect.position.y, widths[i], rect.size.y));
        x += widths[i] + m_Ctx.Px(m_Gap);
    }
}

float Grid::PreferredHeight(float width) const {
    const float gap = m_Ctx.Px(m_Gap);
    const float cell = (width - gap * (m_Columns - 1)) / static_cast<float>(m_Columns);

    float height = 0.f, rowHeight = 0.f;
    int index = 0, rows = 0;

    for (const Child& child : m_Children) {
        if (!child.Item->Visible()) continue;

        rowHeight = std::max(rowHeight, child.Item->PreferredHeight(cell));

        if (++index % m_Columns == 0) {
            height += rowHeight;
            rowHeight = 0.f;
            ++rows;
        }
    }

    if (index % m_Columns != 0) {
        height += rowHeight;
        ++rows;
    }

    return height + std::max(0, rows - 1) * gap;
}

void Grid::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);

    const float gap = m_Ctx.Px(m_Gap);
    const float cell = (rect.size.x - gap * (m_Columns - 1)) / static_cast<float>(m_Columns);

    std::vector<Widget*> row;
    float y = rect.position.y;

    const auto flush = [&]() {
        if (row.empty()) return;

        float height = 0.f;
        for (const Widget* item : row) height = std::max(height, item->PreferredHeight(cell));

        for (std::size_t i = 0; i < row.size(); ++i) {
            row[i]->SetRect(Box(rect.position.x + static_cast<float>(i) * (cell + gap), y, cell, height));
        }

        y += height + gap;
        row.clear();
        };

    for (Child& child : m_Children) {
        if (!child.Item->Visible()) continue;

        row.push_back(child.Item.get());

        if (static_cast<int>(row.size()) == m_Columns) flush();
    }

    flush();
}

#pragma region Text and buttons

void Label::wrap(float width) const {
    if (width == m_WrapWidth) return;

    m_WrapWidth = width;
    m_Lines = m_Ctx.Wrap(m_Text, m_Ctx.FontPx(m_Size), width, m_Bold);
}

void Label::SetText(std::string text) {
    if (text == m_Text) return;

    m_Text = std::move(text);
    m_WrapWidth = -1.f;
}

float Label::PreferredHeight(float width) const {
    wrap(width);
    return static_cast<float>(m_Lines.size()) * m_Ctx.Px(m_Size * 1.5f);
}

void Label::Render(const Painter& painter) const {
    wrap(m_Rect.size.x);

    const Palette& pal = m_Ctx.Pal;
    sf::Color color = pal.TextDim;
    if (m_Tone == Tone::Normal) color = pal.Text;
    else if (m_Tone == Tone::Danger) color = pal.Danger;
    else if (m_Tone == Tone::Warning) color = pal.WarningText;
    const float lineHeight = m_Ctx.Px(m_Size * 1.5f);

    for (std::size_t i = 0; i < m_Lines.size(); ++i) {
        painter.Text(
            m_Lines[i], Box(m_Rect.position.x, m_Rect.position.y + static_cast<float>(i) * lineHeight, m_Rect.size.x, lineHeight),
            m_Ctx.FontPx(m_Size), color, Align::Left, m_Bold
        );
    }
}

float Button::PreferredHeight(float) const {
    return m_Ctx.Px(m_Kind == ButtonKind::Tool ? 64.f : 44.f);
}

float Button::PreferredWidth() const {
    if (m_Width > 0.f) return m_Ctx.Px(m_Width);

    return m_Kind == ButtonKind::IconOnly ? m_Ctx.Px(44.f) : 0.f;
}

void Button::Update(float, sf::Vector2f mouse) {
    m_Hovered = m_Enabled && m_Rect.contains(mouse);
}

void Button::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;
    const bool filled = m_Kind == ButtonKind::Primary && m_Enabled;

    sf::Color fill = m_Pressed ? pal.RaisedActive : m_Hovered ? pal.RaisedHover : pal.Raised;
    if (filled) fill = m_Hovered || m_Pressed ? pal.AccentHover : pal.Accent;

    sf::Color ink = pal.Text;
    if (!m_Enabled) ink = Fade(pal.TextDim, 0.55f);
    else if (filled) ink = pal.OnAccent;
    else if (m_Kind == ButtonKind::Danger) ink = pal.Danger;
    else if (m_Active) ink = pal.Accent;

    const float radius = m_Ctx.Px(8.f);
    painter.Round(m_Rect, fill, radius);

    if (m_Active) painter.Ring(m_Rect, pal.Accent, radius, m_Ctx.Px(2.f));

    const float x = m_Rect.position.x, y = m_Rect.position.y, w = m_Rect.size.x, h = m_Rect.size.y;

    if (m_Kind == ButtonKind::Tool) {
        const float icon = m_Ctx.Px(24.f);

        if (m_Icon) painter.Icon(*m_Icon, Box(Snap(x + (w - icon) * 0.5f), Snap(y + h * 0.17f), icon, icon), ink);

        painter.Text(m_Label, Box(x, y + h * 0.64f, w, h * 0.26f), m_Ctx.FontPx(12.f), ink, Align::Center, true);
    } else if (m_Kind == ButtonKind::IconOnly) {
        const float icon = m_Ctx.Px(20.f);

        if (m_Icon) painter.Icon(*m_Icon, Box(Snap(x + (w - icon) * 0.5f), Snap(y + (h - icon) * 0.5f), icon, icon), ink);
    } else {
        painter.Text(m_Label, m_Rect, m_Ctx.FontPx(14.f), ink, Align::Center, true);
    }
}

bool Button::OnPressed(sf::Vector2f position) {
    m_Pressed = m_Enabled && m_Rect.contains(position);
    return m_Pressed;
}

void Button::OnReleased(sf::Vector2f position) {
    const bool click = m_Pressed && m_Enabled && m_Rect.contains(position);
    m_Pressed = false;

    if (click && m_OnClick) m_OnClick();
}

void Toggle::Update(float deltaTime, sf::Vector2f mouse) {
    m_Hovered = m_Rect.contains(mouse);
    m_Slide = Utils::ExponentiallyMoveTo(m_Slide, m_Value ? 1.f : 0.f, 24.f * deltaTime, 0.01f);
}

void Toggle::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;

    const float w = m_Ctx.Px(44.f), h = m_Ctx.Px(24.f);
    const sf::FloatRect track = Box(
        Snap(m_Rect.position.x + (m_Rect.size.x - w) * 0.5f), Snap(m_Rect.position.y + (m_Rect.size.y - h) * 0.5f), w, h
    );

    const sf::Color off = m_Hovered ? pal.RaisedActive : pal.RaisedHover;
    const sf::Color on = m_Hovered ? pal.AccentHover : pal.Accent;

    painter.Round(track, m_Value ? on : off, h * 0.5f);

    const float knob = m_Ctx.Px(18.f), inset = m_Ctx.Px(3.f);
    const float x = track.position.x + inset + m_Slide * (w - knob - 2.f * inset);

    painter.Round(Box(x, track.position.y + inset, knob, knob), m_Value ? pal.OnAccent : pal.TextDim, knob * 0.5f);
}

bool Toggle::OnPressed(sf::Vector2f position) {
    m_Pressed = m_Rect.contains(position);
    return m_Pressed;
}

void Toggle::OnReleased(sf::Vector2f position) {
    const bool click = m_Pressed && m_Rect.contains(position);
    m_Pressed = false;

    if (!click) return;

    m_Value = !m_Value;
    if (m_OnChange) m_OnChange();
}

sf::FloatRect Stepper::buttonRect(bool up) const {
    const float size = m_Ctx.Px(44.f);
    const float x = up ? m_Rect.position.x + m_Rect.size.x - size : m_Rect.position.x;

    return Box(x, m_Rect.position.y + (m_Rect.size.y - size) * 0.5f, size, size);
}

void Stepper::step(bool up) {
    if (m_CanStep(up)) m_Step(up);
}

void Stepper::Update(float deltaTime, sf::Vector2f mouse) {
    m_Hover = buttonRect(false).contains(mouse) ? -1 : buttonRect(true).contains(mouse) ? 1 : 0;

    if (m_Held == 0) return;

    if (!sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
        m_Held = 0;
        return;
    }

    constexpr float RepeatInterval = 0.075f;

    m_RepeatTimer -= deltaTime;

    for (int burst = 0; m_RepeatTimer <= 0.f && burst < 4; ++burst) {
        step(m_Held > 0);
        m_RepeatTimer += RepeatInterval;
    }
}

void Stepper::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;

    for (const bool up : {false, true}) {
        const sf::FloatRect rect = buttonRect(up);
        const bool enabled = m_CanStep(up);
        const bool held = m_Held == (up ? 1 : -1);

        sf::Color fill = pal.Raised;
        if (enabled && held) fill = pal.RaisedActive;
        else if (enabled && m_Hover == (up ? 1 : -1)) fill = pal.RaisedHover;

        painter.Round(rect, fill, m_Ctx.Px(8.f));

        const float icon = m_Ctx.Px(20.f);
        painter.Icon(
            up ? UI::Icon::Plus : UI::Icon::Minus,
            Box(Snap(rect.position.x + (rect.size.x - icon) * 0.5f), Snap(rect.position.y + (rect.size.y - icon) * 0.5f), icon, icon),
            enabled ? pal.Text : Fade(pal.TextDim, 0.4f)
        );
    }

    const float left = buttonRect(false).position.x + m_Ctx.Px(44.f);
    const float right = buttonRect(true).position.x;

    painter.Text(m_Text(), Box(left, m_Rect.position.y, right - left, m_Rect.size.y), m_Ctx.FontPx(16.f), pal.Text, Align::Center, true);
}

bool Stepper::OnPressed(sf::Vector2f position) {
    if (buttonRect(false).contains(position)) m_Held = -1;
    else if (buttonRect(true).contains(position)) m_Held = 1;
    else return false;

    step(m_Held > 0);
    m_RepeatTimer = 0.4f;

    return true;
}

void Stepper::OnReleased(sf::Vector2f) {
    m_Held = 0;
}

std::unique_ptr<Stepper> UI::MakeIntStepper(
    const Context& context, int& value, int min, int max,
    StepFunction next, std::function<std::string(int)> format, std::function<void()> onChange
) {
    return std::make_unique<Stepper>(
        context,
        [&value, format = std::move(format)]() { return format(value); },
        [&value, min, max, next, onChange = std::move(onChange)](bool up) {
            const int target = std::clamp(next(value, up), min, max);

            if (target == value) return;

            value = target;
            if (onChange) onChange();
        },
        [&value, min, max, next](bool up) { return std::clamp(next(value, up), min, max) != value; }
    );
}

#pragma region Choices and input

ChoiceBar::ChoiceBar(
    const Context& context, std::vector<Choice> choices,
    std::function<int()> get, std::function<void(int)> set, float width
) : Widget(context), m_Choices(std::move(choices)), m_Get(std::move(get)), m_Set(std::move(set)), m_Width(width) {
    m_Cards = std::any_of(m_Choices.begin(), m_Choices.end(), [](const Choice& c) { return c.Icon.has_value(); });
}

sf::FloatRect ChoiceBar::cell(std::size_t index) const {
    const float gap = m_Ctx.Px(8.f);
    const float count = static_cast<float>(m_Choices.size());
    const float width = (m_Rect.size.x - gap * (count - 1.f)) / count;

    return Box(m_Rect.position.x + static_cast<float>(index) * (width + gap), m_Rect.position.y, width, m_Rect.size.y);
}

int ChoiceBar::indexAt(sf::Vector2f position) const {
    for (std::size_t i = 0; i < m_Choices.size(); ++i) {
        if (cell(i).contains(position)) return static_cast<int>(i);
    }

    return -1;
}

void ChoiceBar::Update(float, sf::Vector2f mouse) {
    m_Hover = indexAt(mouse);
}

void ChoiceBar::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;
    const int selected = m_Get();

    for (std::size_t i = 0; i < m_Choices.size(); ++i) {
        const Choice& choice = m_Choices[i];
        const sf::FloatRect rect = cell(i);
        const bool hovered = choice.Enabled && m_Hover == static_cast<int>(i);
        const float alpha = choice.Enabled ? 1.f : 0.4f;

        painter.Round(rect, hovered ? pal.RaisedHover : pal.Raised, m_Ctx.Px(10.f));

        if (selected == static_cast<int>(i)) painter.Ring(rect, pal.Accent, m_Ctx.Px(10.f), m_Ctx.Px(2.f));

        const sf::Color title = Fade(selected == static_cast<int>(i) ? pal.Accent : pal.Text, alpha);
        const float x = rect.position.x, y = rect.position.y, w = rect.size.x, h = rect.size.y;

        if (!m_Cards) {
            painter.Text(choice.Title, rect, m_Ctx.FontPx(14.f), title, Align::Center, true);
            continue;
        }

        const float icon = m_Ctx.Px(28.f);

        if (choice.Icon) painter.Icon(*choice.Icon, Box(Snap(x + (w - icon) * 0.5f), Snap(y + h * 0.12f), icon, icon), title);

        painter.Text(choice.Title, Box(x, y + h * 0.54f, w, h * 0.2f), m_Ctx.FontPx(14.f), title, Align::Center, true);
        painter.Text(choice.Note, Box(x, y + h * 0.74f, w, h * 0.18f), m_Ctx.FontPx(11.5f), Fade(pal.TextDim, alpha), Align::Center);
    }
}

bool ChoiceBar::OnPressed(sf::Vector2f position) {
    m_Pressed = indexAt(position);

    return m_Pressed >= 0 && m_Choices[static_cast<std::size_t>(m_Pressed)].Enabled;
}

void ChoiceBar::OnReleased(sf::Vector2f position) {
    const int index = indexAt(position);
    const int pressed = std::exchange(m_Pressed, -1);

    if (index >= 0 && index == pressed && m_Choices[static_cast<std::size_t>(index)].Enabled) m_Set(index);
}

void TextField::Paste() {
    std::string text = sf::Clipboard::getString().toAnsiString();

    for (char& c : text) {
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    }

    const std::size_t first = text.find_first_not_of(' ');
    const std::size_t last = text.find_last_not_of(' ');

    m_Value = first == std::string::npos ? "" : text.substr(first, std::min<std::size_t>(last - first + 1, 120));

    if (m_OnChange) m_OnChange();
}

void TextField::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;
    const float radius = m_Ctx.Px(8.f);

    painter.Round(m_Rect, pal.Inset, radius);
    painter.Ring(m_Rect, m_Focused ? pal.Accent : pal.Border, radius, m_Ctx.Px(m_Focused ? 2.f : 1.f));

    const float pad = m_Ctx.Px(12.f);
    const sf::FloatRect inner = Box(m_Rect.position.x + pad, m_Rect.position.y, m_Rect.size.x - 2.f * pad, m_Rect.size.y);

    const Painter::Clip clip(painter, inner);
    if (!clip.Visible()) return;

    const unsigned int size = m_Ctx.FontPx(14.f);

    if (m_Value.empty()) {
        painter.Text(m_Placeholder, inner, size, pal.TextDim);
    } else {
        const float width = m_Ctx.TextWidth(m_Value, size);
        const float shift = std::min(0.f, inner.size.x - width - m_Ctx.Px(2.f));

        painter.Text(m_Value, Box(inner.position.x + shift, inner.position.y, width + 4.f, inner.size.y), size, pal.Text);

        if (m_Focused && std::fmod(m_Time, 1.f) < 0.6f) {
            painter.Fill(
                Box(Snap(inner.position.x + shift + width + m_Ctx.Px(2.f)), inner.position.y + inner.size.y * 0.25f, std::max(1.f, m_Ctx.Px(1.5f)), inner.size.y * 0.5f),
                pal.Text
            );
        }
    }
}

bool TextField::OnPressed(sf::Vector2f position) {
    m_Focused = m_Rect.contains(position);
    m_Time = 0.f;

    return m_Focused;
}

void TextField::OnText(char32_t unicode) {
    if (!m_Focused || unicode < 32 || unicode > 126 || m_Value.size() >= 120) return;

    m_Value += static_cast<char>(unicode);

    if (m_OnChange) m_OnChange();
}

bool TextField::OnKey(sf::Keyboard::Scancode key, bool control) {
    if (!m_Focused) return false;

    if (key == sf::Keyboard::Scancode::Backspace && !m_Value.empty()) {
        if (control) m_Value.clear();
        else m_Value.pop_back();
    } else if (key == sf::Keyboard::Scancode::Delete) {
        m_Value.clear();
    } else if (control && key == sf::Keyboard::Scancode::V) {
        Paste();
        return true;
    } else {
        return false;
    }

    if (m_OnChange) m_OnChange();

    return true;
}

#pragma region Tiles and scrolling

sf::FloatRect TileGrid::tile(std::size_t index) const {
    const float gap = m_Ctx.Px(8.f);
    const float width = (m_Rect.size.x - gap * static_cast<float>(m_Model.Columns - 1)) / static_cast<float>(m_Model.Columns);
    const float height = m_Ctx.Px(m_Model.TileHeight);

    const std::size_t column = index % static_cast<std::size_t>(m_Model.Columns);
    const std::size_t row = index / static_cast<std::size_t>(m_Model.Columns);

    return Box(
        m_Rect.position.x + static_cast<float>(column) * (width + gap),
        m_Rect.position.y + static_cast<float>(row) * (height + gap), width, height
    );
}

int TileGrid::indexAt(sf::Vector2f position) const {
    if (!m_Rect.contains(position) || m_Model.Count == 0) return -1;

    const float gap = m_Ctx.Px(8.f);
    const float width = (m_Rect.size.x - gap * static_cast<float>(m_Model.Columns - 1)) / static_cast<float>(m_Model.Columns);
    const float height = m_Ctx.Px(m_Model.TileHeight);

    const int column = static_cast<int>(std::floor((position.x - m_Rect.position.x) / (width + gap)));
    const int row = static_cast<int>(std::floor((position.y - m_Rect.position.y) / (height + gap)));

    if (column < 0 || column >= m_Model.Columns || row < 0) return -1;

    const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(m_Model.Columns) + static_cast<std::size_t>(column);

    if (index >= m_Model.Count) return -1;

    return tile(index).contains(position) ? static_cast<int>(index) : -1;
}

float TileGrid::PreferredHeight(float) const {
    const std::size_t rows = (m_Model.Count + static_cast<std::size_t>(m_Model.Columns) - 1) / static_cast<std::size_t>(m_Model.Columns);

    if (rows == 0) return 0.f;

    return static_cast<float>(rows) * m_Ctx.Px(m_Model.TileHeight) + static_cast<float>(rows - 1) * m_Ctx.Px(8.f);
}

void TileGrid::Update(float, sf::Vector2f mouse) {
    m_Hover = indexAt(mouse);
}

void TileGrid::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;
    const float pad = m_Ctx.Px(10.f);
    const sf::Vector2f preview(m_Ctx.Px(m_Model.PreviewSize.x), m_Ctx.Px(m_Model.PreviewSize.y));
    const std::size_t selected = m_Model.Selected();
    const unsigned int fontSize = m_Ctx.FontPx(13.f);
    const float textWidth = tile(0).size.x - 3.f * pad - preview.x;

    if (m_FitWidth != textWidth || m_Names.size() != m_Model.Count) {
        m_FitWidth = textWidth;
        m_Names.clear();

        for (std::size_t i = 0; i < m_Model.Count; ++i) m_Names.push_back(m_Ctx.Fit(m_Model.Name(i), fontSize, textWidth, true));
    }

    for (std::size_t i = 0; i < m_Model.Count; ++i) {
        const sf::FloatRect rect = tile(i);

        if (!rect.findIntersection(painter.VisibleRect())) continue;

        painter.Round(rect, m_Hover == static_cast<int>(i) ? pal.RaisedHover : pal.Raised, m_Ctx.Px(8.f));

        if (i == selected) painter.Ring(rect, pal.Accent, m_Ctx.Px(8.f), m_Ctx.Px(2.f));

        m_Model.DrawPreview(
            painter, Box(rect.position.x + pad, Snap(rect.position.y + (rect.size.y - preview.y) * 0.5f), preview.x, preview.y), i
        );

        painter.Text(
            m_Names[i], Box(rect.position.x + 2.f * pad + preview.x, rect.position.y, textWidth, rect.size.y),
            fontSize, i == selected ? pal.Accent : pal.Text, Align::Left, true
        );
    }
}

bool TileGrid::OnPressed(sf::Vector2f position) {
    m_Pressed = indexAt(position);
    return m_Pressed >= 0;
}

void TileGrid::OnReleased(sf::Vector2f position) {
    const int index = indexAt(position);

    if (index >= 0 && index == std::exchange(m_Pressed, -1)) m_Model.Select(static_cast<std::size_t>(index));
}

float ScrollView::maxOffset() const noexcept {
    return std::max(0.f, m_ContentHeight - m_Rect.size.y);
}

void ScrollView::layout() {
    float width = m_Rect.size.x;
    float height = m_Content->PreferredHeight(width);

    if (height > m_Rect.size.y || m_MaxHeight > 0.f) {
        width -= m_Ctx.Px(10.f);
        height = m_Content->PreferredHeight(width);
    }

    m_ContentHeight = height;

    if (m_StickToEnd) {
        m_Target = maxOffset();
        m_StickToEnd = false;
    }

    m_Target = std::clamp(m_Target, 0.f, maxOffset());
    m_Offset = std::clamp(m_Offset, 0.f, maxOffset());

    m_Content->SetRect(Box(m_Rect.position.x, m_Rect.position.y - std::round(m_Offset), width, height));
}

float ScrollView::PreferredHeight(float width) const {
    const float height = m_Content->PreferredHeight(width);

    return m_MaxHeight > 0.f ? std::min(height, m_Ctx.Px(m_MaxHeight)) : height;
}

void ScrollView::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);
    layout();
}

void ScrollView::Update(float deltaTime, sf::Vector2f mouse) {
    m_Offset = Utils::ExponentiallyMoveTo(m_Offset, m_Target, 22.f * deltaTime, 0.5f);

    layout();

    m_Content->Update(deltaTime, m_Rect.contains(mouse) ? mouse : NoMouse);
}

void ScrollView::Render(const Painter& painter) const {
    const Painter::Clip clip(painter, m_Rect);
    if (!clip.Visible()) return;

    m_Content->Render(painter);

    const float range = maxOffset();

    if (range <= 0.f) return;

    const float view = m_Rect.size.y;
    const float thumb = std::max(m_Ctx.Px(24.f), view * view / m_ContentHeight);
    const float y = m_Rect.position.y + (view - thumb) * (m_Offset / range);
    const float width = m_Ctx.Px(4.f);

    painter.Round(
        Box(m_Rect.position.x + m_Rect.size.x - width - m_Ctx.Px(1.f), y, width, thumb),
        Fade(m_Ctx.Pal.TextDim, 0.45f), width * 0.5f
    );
}

bool ScrollView::OnPressed(sf::Vector2f position) {
    return m_Rect.contains(position) && m_Content->OnPressed(position);
}

void ScrollView::OnReleased(sf::Vector2f position) {
    m_Content->OnReleased(position);
}

bool ScrollView::OnScroll(sf::Vector2f position, float delta) {
    if (!m_Rect.contains(position)) return false;
    if (m_Content->OnScroll(position, delta)) return true;

    const float target = std::clamp(m_Target - delta * m_Ctx.Px(56.f), 0.f, maxOffset());

    if (target == m_Target) return false;

    m_Target = target;
    return true;
}

void ScrollView::OnText(char32_t unicode) {
    m_Content->OnText(unicode);
}

bool ScrollView::OnKey(sf::Keyboard::Scancode key, bool control) {
    return m_Content->OnKey(key, control);
}

#pragma region Rows, tabs and dialogs

void Header::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);

    const float size = m_Ctx.Px(44.f);
    m_Close.SetRect(Box(rect.position.x + rect.size.x - size, rect.position.y + (rect.size.y - size) * 0.5f, size, size));
}

void Header::Render(const Painter& painter) const {
    painter.Text(
        m_Title, Box(m_Rect.position.x, m_Rect.position.y, m_Rect.size.x - m_Ctx.Px(56.f), m_Rect.size.y),
        m_Ctx.FontPx(22.f), m_Ctx.Pal.Text, Align::Left, true
    );

    m_Close.Render(painter);
}

float FormRow::textWidth(float width) const {
    return std::max(m_Ctx.Px(80.f), width - m_Control->PreferredWidth() - m_Ctx.Px(16.f));
}

void FormRow::wrap(float width) const {
    const float text = textWidth(width);

    if (text == m_WrapWidth) return;

    m_WrapWidth = text;
    m_Lines = m_Ctx.Wrap(m_Description, m_Ctx.FontPx(12.5f), text);
}

float FormRow::PreferredHeight(float width) const {
    wrap(width);

    const float lines = static_cast<float>(m_Lines.size());
    const float text = m_Ctx.Px(20.f) + (lines > 0.f ? m_Ctx.Px(2.f) + lines * m_Ctx.Px(17.f) : 0.f);
    const float control = m_Control->PreferredHeight(m_Control->PreferredWidth());

    return std::max(text, control) + 2.f * m_Ctx.Px(12.f);
}

void FormRow::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);

    const float width = m_Control->PreferredWidth();
    const float height = m_Control->PreferredHeight(width);

    m_Control->SetRect(Box(rect.position.x + rect.size.x - width, Snap(rect.position.y + (rect.size.y - height) * 0.5f), width, height));
}

void FormRow::Render(const Painter& painter) const {
    wrap(m_Rect.size.x);

    const Palette& pal = m_Ctx.Pal;
    const float lines = static_cast<float>(m_Lines.size());
    const float block = m_Ctx.Px(20.f) + (lines > 0.f ? m_Ctx.Px(2.f) + lines * m_Ctx.Px(17.f) : 0.f);

    float y = Snap(m_Rect.position.y + (m_Rect.size.y - block) * 0.5f);
    const float x = m_Rect.position.x, width = m_WrapWidth;

    painter.Text(m_Title, Box(x, y, width, m_Ctx.Px(20.f)), m_Ctx.FontPx(15.f), pal.Text, Align::Left, true);
    y += m_Ctx.Px(22.f);

    for (const std::string& line : m_Lines) {
        painter.Text(line, Box(x, y, width, m_Ctx.Px(17.f)), m_Ctx.FontPx(12.5f), pal.TextDim);
        y += m_Ctx.Px(17.f);
    }

    painter.Fill(Box(x, Snap(m_Rect.position.y + m_Rect.size.y - 1.f), m_Rect.size.x, 1.f), pal.Border);

    m_Control->Render(painter);
}

bool FormRow::OnPressed(sf::Vector2f position) {
    m_Pressed = m_Control->OnPressed(position);
    return m_Pressed;
}

void FormRow::OnReleased(sf::Vector2f position) {
    if (std::exchange(m_Pressed, false)) m_Control->OnReleased(position);
}

void TabView::AddPage(std::string name, std::unique_ptr<Widget> content) {
    m_Pages.push_back({std::move(name), std::make_unique<ScrollView>(m_Ctx, std::move(content))});

    if (m_Pages.size() == 1) m_Header.SetTitle(m_Pages[0].Name);
}

sf::FloatRect TabView::tabRect(std::size_t index) const {
    return Box(
        m_Rect.position.x + m_Ctx.Px(16.f), m_Rect.position.y + m_Ctx.Px(56.f) + static_cast<float>(index) * m_Ctx.Px(48.f),
        m_Ctx.Px(136.f), m_Ctx.Px(44.f)
    );
}

sf::FloatRect TabView::contentRect() const {
    const float left = m_Ctx.Px(168.f + 28.f), pad = m_Ctx.Px(20.f);

    return Box(m_Rect.position.x + left, m_Rect.position.y + pad, m_Rect.size.x - left - m_Ctx.Px(28.f), m_Rect.size.y - 2.f * pad);
}

void TabView::SetRect(sf::FloatRect rect) {
    Widget::SetRect(rect);

    const sf::FloatRect content = contentRect();
    const float header = m_Ctx.Px(44.f), gap = m_Ctx.Px(8.f);

    m_Header.SetRect(Box(content.position.x, content.position.y, content.size.x, header));

    if (!m_Pages.empty()) {
        m_Pages[m_Active].View->SetRect(Box(
            content.position.x, content.position.y + header + gap, content.size.x, content.size.y - header - gap
        ));
    }
}

void TabView::Update(float deltaTime, sf::Vector2f mouse) {
    m_Hover = -1;

    for (std::size_t i = 0; i < m_Pages.size(); ++i) {
        if (tabRect(i).contains(mouse)) m_Hover = static_cast<int>(i);
    }

    m_Header.Update(deltaTime, mouse);

    if (!m_Pages.empty()) m_Pages[m_Active].View->Update(deltaTime, mouse);
}

void TabView::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;

    const float rail = m_Ctx.Px(168.f), radius = m_Ctx.Px(14.f);
    painter.Round(Box(m_Rect.position.x, m_Rect.position.y, rail, m_Rect.size.y), pal.Panel, radius);
    painter.Fill(Box(m_Rect.position.x + radius, m_Rect.position.y, rail - radius, m_Rect.size.y), pal.Panel);

    painter.Text(
        "MENU", Box(m_Rect.position.x + m_Ctx.Px(28.f), m_Rect.position.y + m_Ctx.Px(20.f), rail, m_Ctx.Px(24.f)),
        m_Ctx.FontPx(11.5f), pal.TextDim, Align::Left, true
    );

    for (std::size_t i = 0; i < m_Pages.size(); ++i) {
        const sf::FloatRect tab = tabRect(i);
        const bool active = i == m_Active;

        if (active || m_Hover == static_cast<int>(i)) painter.Round(tab, active ? pal.Raised : Fade(pal.Raised, 0.5f), m_Ctx.Px(8.f));

        if (active) painter.Round(Box(tab.position.x + m_Ctx.Px(5.f), tab.position.y + tab.size.y * 0.25f, m_Ctx.Px(3.f), tab.size.y * 0.5f), pal.Accent, m_Ctx.Px(1.5f));

        painter.Text(
            m_Pages[i].Name, Box(tab.position.x + m_Ctx.Px(18.f), tab.position.y, tab.size.x - m_Ctx.Px(18.f), tab.size.y),
            m_Ctx.FontPx(15.f), active ? pal.Text : pal.TextDim, Align::Left, active
        );
    }

    m_Header.Render(painter);

    if (!m_Pages.empty()) m_Pages[m_Active].View->Render(painter);
}

bool TabView::OnPressed(sf::Vector2f position) {
    for (std::size_t i = 0; i < m_Pages.size(); ++i) {
        if (!tabRect(i).contains(position)) continue;

        m_Active = i;
        m_Header.SetTitle(m_Pages[i].Name);
        SetRect(m_Rect);

        return false;
    }

    const bool header = m_Header.OnPressed(position);

    return (!m_Pages.empty() && m_Pages[m_Active].View->OnPressed(position)) || header;
}

void TabView::OnReleased(sf::Vector2f position) {
    m_Header.OnReleased(position);

    if (!m_Pages.empty()) m_Pages[m_Active].View->OnReleased(position);
}

bool TabView::OnScroll(sf::Vector2f position, float delta) {
    return !m_Pages.empty() && m_Pages[m_Active].View->OnScroll(position, delta);
}

void TabView::OnText(char32_t unicode) {
    if (!m_Pages.empty()) m_Pages[m_Active].View->OnText(unicode);
}

bool TabView::OnKey(sf::Keyboard::Scancode key, bool control) {
    return !m_Pages.empty() && m_Pages[m_Active].View->OnKey(key, control);
}

void Dialog::layout() {
    const float width = std::min(m_Ctx.Px(m_Width), m_Window.x - m_Ctx.Px(32.f));
    const float padding = m_Ctx.Px(m_Padding);

    float height = m_Height > 0.f ? m_Ctx.Px(m_Height) : m_Root->PreferredHeight(width - 2.f * padding) + 2.f * padding;
    height = std::min(height, m_Window.y - m_Ctx.Px(32.f));

    m_Frame = Box(Snap((m_Window.x - width) * 0.5f), Snap((m_Window.y - height) * 0.5f), width, height);
    m_Root->SetRect(Box(m_Frame.position.x + padding, m_Frame.position.y + padding, width - 2.f * padding, height - 2.f * padding));
}

void Dialog::SetWindow(sf::Vector2f size) {
    m_Window = size;
    layout();
}

void Dialog::Update(float deltaTime, sf::Vector2f mouse) {
    m_Appear = std::min(1.f, m_Appear + deltaTime / 0.12f);

    layout();
    m_Root->Update(deltaTime, mouse);
}

void Dialog::Render(const Painter& painter) const {
    const Palette& pal = m_Ctx.Pal;

    painter.Fill(Box(0.f, 0.f, m_Window.x, m_Window.y), Fade(pal.Scrim, m_Appear));
    painter.Round(m_Frame, pal.Window, m_Ctx.Px(14.f));
    painter.Ring(m_Frame, pal.Border, m_Ctx.Px(14.f), std::max(1.f, m_Ctx.Px(1.f)));

    m_Root->Render(painter);
}

void Dialog::OnPressed(sf::Vector2f position) {
    if (!m_Frame.contains(position)) {
        Close();
        return;
    }

    m_Root->OnPressed(position);
}

void Dialog::OnReleased(sf::Vector2f position) {
    m_Root->OnReleased(position);
}

void Dialog::OnScroll(sf::Vector2f position, float delta) {
    m_Root->OnScroll(position, delta);
}

void Dialog::OnText(char32_t unicode) {
    m_Root->OnText(unicode);
}

void Dialog::OnKey(sf::Keyboard::Scancode key, bool control) {
    if (!m_Root->OnKey(key, control) && key == sf::Keyboard::Scancode::Escape) Close();
}
