#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SFML/Graphics.hpp"

namespace UI {
    enum class Icon : uint8_t {
        Flip, Hint, Bolt, Skip, Inspect, Plus, Menu, Close, Minus, Cpu, Users, Globe,
        Count
    };

    struct Palette {
        sf::Color Window{48, 46, 43};
        sf::Color Panel{38, 36, 33};
        sf::Color Inset{31, 30, 28};
        sf::Color Raised{58, 56, 53};
        sf::Color RaisedHover{74, 71, 67};
        sf::Color RaisedActive{90, 86, 81};
        sf::Color Border{63, 60, 56};
        sf::Color Text{240, 239, 237};
        sf::Color TextDim{168, 166, 162};
        sf::Color Danger{244, 160, 144};
        sf::Color Warning{232, 163, 61};
        sf::Color WarningText{246, 196, 112};
        sf::Color WarningBackground{51, 41, 26};
        sf::Color Scrim{0, 0, 0, 150};

        sf::Color Accent{129, 182, 76};
        sf::Color AccentHover{150, 196, 100};
        sf::Color OnAccent{27, 26, 24};

        [[nodiscard]]
        static Palette FromAccent(sf::Color accent);
    };

    struct Context {
        float Scale{1.f};
        Palette Pal;
        const sf::Font* Font{nullptr};
        const sf::Texture* Icons{nullptr};

        [[nodiscard]]
        inline float Px(float designUnits) const noexcept {
            return designUnits * Scale;
        }

        [[nodiscard]]
        inline unsigned int FontPx(float designUnits) const noexcept {
            return static_cast<unsigned int>(std::max(1.f, std::round(designUnits * Scale)));
        }

        [[nodiscard]]
        float TextWidth(const std::string& text, unsigned int size, bool bold = false) const;

        [[nodiscard]]
        std::pair<float, float> CapMetrics(unsigned int size, bool bold) const;

        [[nodiscard]]
        std::vector<std::string> Wrap(const std::string& text, unsigned int size, float maxWidth, bool bold = false) const;

        // shortens text with ".." until it fits
        [[nodiscard]]
        std::string Fit(const std::string& text, unsigned int size, float maxWidth, bool bold = false) const;

        [[nodiscard]]
        sf::Text& CachedText(const std::string& text, unsigned int size, bool bold) const;

    private:
        struct TextKey {
            std::string Text;
            unsigned int Size;
            bool Bold;

            bool operator==(const TextKey&) const = default;
        };

        struct TextKeyHash {
            std::size_t operator()(const TextKey& key) const noexcept {
                return std::hash<std::string>{}(key.Text) ^ (static_cast<std::size_t>(key.Size) << 1) ^ (key.Bold ? 0x9e3779b9u : 0u);
            }
        };

        mutable std::unordered_map<unsigned int, std::pair<float, float>> m_CapCache;

        mutable std::unordered_map<TextKey, sf::Text, TextKeyHash> m_Texts;
        mutable std::unordered_map<TextKey, float, TextKeyHash> m_Widths;
    };

    enum class Align : uint8_t {
        Left, Center, Right
    };

    class Painter final {
    private:
        mutable sf::FloatRect m_Clip;

    public:
        Painter(sf::RenderTarget& target, const Context& context);

        sf::RenderTarget& Target;
        const Context& Ctx;

        void Fill(sf::FloatRect rect, sf::Color color) const;
        void Round(sf::FloatRect rect, sf::Color color, float radius) const;
        void Ring(sf::FloatRect rect, sf::Color color, float radius, float thickness) const;

        void Text(
            const std::string& text, sf::FloatRect box, unsigned int size, sf::Color color,
            Align align = Align::Left, bool bold = false
        ) const;

        void Icon(UI::Icon icon, sf::FloatRect box, sf::Color color) const;

        [[nodiscard]]
        inline const sf::FloatRect& VisibleRect() const noexcept {
            return m_Clip;
        }

        class Clip final {
        private:
            const Painter& m_Painter;
            sf::View m_PreviousView;
            sf::FloatRect m_PreviousClip;
            bool m_Visible;

        public:
            Clip(const Painter& painter, sf::FloatRect rect);
            ~Clip();

            Clip(const Clip&) = delete;
            Clip& operator=(const Clip&) = delete;

            [[nodiscard]]
            inline bool Visible() const noexcept {
                return m_Visible;
            }
        };
    };


    constexpr inline const sf::Vector2f NoMouse{-1e6f, -1e6f};

    class Widget {
    protected:
        const Context& m_Ctx;
        sf::FloatRect m_Rect;
        std::function<bool()> m_ShowIf;

    public:
        explicit Widget(const Context& context) : m_Ctx(context) {}
        virtual ~Widget() = default;

        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;

        [[nodiscard]]
        virtual float PreferredHeight(float width) const = 0;

        [[nodiscard]]
        virtual float PreferredWidth() const {
            return 0.f;
        }

        virtual void SetRect(sf::FloatRect rect) {
            m_Rect = rect;
        }

        virtual void Update(float deltaTime, sf::Vector2f mouse) {}
        virtual void Render(const Painter& painter) const = 0;

        virtual bool OnPressed(sf::Vector2f position) { return false; }
        virtual void OnReleased(sf::Vector2f position) {}

        virtual bool OnScroll(sf::Vector2f position, float delta) { return false; }

        virtual void OnText(char32_t unicode) {}
        virtual bool OnKey(sf::Keyboard::Scancode key, bool control) { return false; }

        inline Widget& ShowIf(std::function<bool()> predicate) {
            m_ShowIf = std::move(predicate);
            return *this;
        }

        [[nodiscard]]
        inline bool Visible() const {
            return !m_ShowIf || m_ShowIf();
        }

        [[nodiscard]]
        inline const sf::FloatRect& Rect() const noexcept {
            return m_Rect;
        }
    };

    class Container : public Widget {
    protected:
        struct Child {
            std::unique_ptr<Widget> Item;
            bool Flex;
        };

        std::vector<Child> m_Children;
        Widget* m_Captured{nullptr};

    public:
        using Widget::Widget;

        template <class T, class... Args>
        T& Add(Args&&... args) {
            T* item = new T(m_Ctx, std::forward<Args>(args)...);
            m_Children.push_back({std::unique_ptr<Widget>(item), false});
            return *item;
        }

        template <class T, class... Args>
        T& AddFlex(Args&&... args) {
            T& item = Add<T>(std::forward<Args>(args)...);
            m_Children.back().Flex = true;
            return item;
        }

        inline Widget& Adopt(std::unique_ptr<Widget> widget) {
            m_Children.push_back({std::move(widget), false});
            return *m_Children.back().Item;
        }

        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
        bool OnScroll(sf::Vector2f position, float delta) override;
        void OnText(char32_t unicode) override;
        bool OnKey(sf::Keyboard::Scancode key, bool control) override;
    };

    class Column final : public Container {
    private:
        float m_Gap, m_PadX, m_PadY;
        std::optional<sf::Color> m_Fill, m_Border;
        float m_Radius{0.f};

    public:
        Column(const Context& context, float gap = 0.f, float padX = 0.f, float padY = 0.f) :
            Container(context), m_Gap(gap), m_PadX(padX), m_PadY(padY) {}

        inline Column& Background(sf::Color fill, std::optional<sf::Color> border, float radius) {
            m_Fill = fill;
            m_Border = border;
            m_Radius = radius;
            return *this;
        }

        float PreferredHeight(float width) const override;
        void SetRect(sf::FloatRect rect) override;
        void Render(const Painter& painter) const override;
    };

    class Row final : public Container {
    private:
        float m_Gap;

    public:
        Row(const Context& context, float gap = 0.f) : Container(context), m_Gap(gap) {}

        float PreferredHeight(float width) const override;
        void SetRect(sf::FloatRect rect) override;
    };

    class Grid final : public Container {
    private:
        int m_Columns;
        float m_Gap;

    public:
        Grid(const Context& context, int columns, float gap = 0.f) :
            Container(context), m_Columns(columns), m_Gap(gap) {}

        float PreferredHeight(float width) const override;
        void SetRect(sf::FloatRect rect) override;
    };

    class Spacer final : public Widget {
    private:
        float m_Height;

    public:
        Spacer(const Context& context, float height = 0.f) : Widget(context), m_Height(height) {}

        float PreferredHeight(float) const override { return m_Ctx.Px(m_Height); }
        void Render(const Painter&) const override {}
    };

    enum class Tone : uint8_t {
        Normal, Dim, Danger, Warning
    };

    class Label final : public Widget {
    private:
        std::string m_Text;
        float m_Size;
        Tone m_Tone;
        bool m_Bold;

        mutable float m_WrapWidth{-1.f};
        mutable std::vector<std::string> m_Lines;

        void wrap(float width) const;

    public:
        Label(const Context& context, std::string text, float size = 13.f, Tone tone = Tone::Dim, bool bold = false) :
            Widget(context), m_Text(std::move(text)), m_Size(size), m_Tone(tone), m_Bold(bold) {}

        void SetText(std::string text);
        void SetTone(Tone tone) noexcept { m_Tone = tone; }

        float PreferredHeight(float width) const override;
        void Render(const Painter& painter) const override;
    };

    enum class ButtonKind : uint8_t {
        Standard, Primary, Danger,
        Tool, // icon above a small label
        IconOnly
    };

    class Button final : public Widget {
    private:
        std::string m_Label;
        ButtonKind m_Kind;
        std::optional<UI::Icon> m_Icon;
        std::function<void()> m_OnClick;
        float m_Width{0.f};
        bool m_Enabled{true};
        bool m_Active{false};
        bool m_Hovered{false};
        bool m_Pressed{false};

    public:
        Button(
            const Context& context, std::string label, ButtonKind kind = ButtonKind::Standard,
            std::optional<UI::Icon> icon = std::nullopt, std::function<void()> onClick = {}
        ) : Widget(context), m_Label(std::move(label)), m_Kind(kind), m_Icon(icon), m_OnClick(std::move(onClick)) {}

        inline Button& Width(float designUnits) noexcept { m_Width = designUnits; return *this; }
        inline void SetLabel(std::string label) { m_Label = std::move(label); }
        inline void SetIcon(UI::Icon icon) noexcept { m_Icon = icon; }
        inline void SetEnabled(bool enabled) noexcept { m_Enabled = enabled; }
        inline void SetActive(bool active) noexcept { m_Active = active; }
        inline void SetOnClick(std::function<void()> onClick) { m_OnClick = std::move(onClick); }

        float PreferredHeight(float) const override;
        float PreferredWidth() const override;
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
    };

    class Toggle final : public Widget {
    private:
        bool& m_Value;
        std::function<void()> m_OnChange;
        float m_Slide;
        bool m_Hovered{false};
        bool m_Pressed{false};

    public:
        Toggle(const Context& context, bool& value, std::function<void()> onChange = {}) :
            Widget(context), m_Value(value), m_OnChange(std::move(onChange)), m_Slide(value ? 1.f : 0.f) {}

        float PreferredHeight(float) const override { return m_Ctx.Px(44.f); }
        float PreferredWidth() const override { return m_Ctx.Px(44.f); }
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
    };

    class Stepper final : public Widget {
    private:
        std::function<std::string()> m_Text;
        std::function<void(bool up)> m_Step;
        std::function<bool(bool up)> m_CanStep;

        int m_Held{0}; // -1 minus, +1 plus
        float m_RepeatTimer{0.f};
        int m_Hover{0};

        [[nodiscard]]
        sf::FloatRect buttonRect(bool up) const;

        void step(bool up);

    public:
        Stepper(
            const Context& context, std::function<std::string()> text,
            std::function<void(bool)> step, std::function<bool(bool)> canStep
        ) : Widget(context), m_Text(std::move(text)), m_Step(std::move(step)), m_CanStep(std::move(canStep)) {}

        float PreferredHeight(float) const override { return m_Ctx.Px(44.f); }
        float PreferredWidth() const override { return m_Ctx.Px(176.f); }
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
    };

    using StepFunction = std::function<int(int value, bool up)>;

    [[nodiscard]]
    std::unique_ptr<Stepper> MakeIntStepper(
        const Context& context, int& value, int min, int max,
        StepFunction next, std::function<std::string(int)> format, std::function<void()> onChange
    );

    struct Choice {
        std::string Title;
        std::string Note;
        std::optional<UI::Icon> Icon;
        bool Enabled{true};
    };

    class ChoiceBar final : public Widget {
    private:
        std::vector<Choice> m_Choices;
        std::function<int()> m_Get;
        std::function<void(int)> m_Set;
        float m_Width;
        bool m_Cards;
        int m_Hover{-1};
        int m_Pressed{-1};

        [[nodiscard]]
        sf::FloatRect cell(std::size_t index) const;

        [[nodiscard]]
        int indexAt(sf::Vector2f position) const;

    public:
        ChoiceBar(
            const Context& context, std::vector<Choice> choices,
            std::function<int()> get, std::function<void(int)> set, float width = 0.f
        );

        float PreferredHeight(float) const override { return m_Ctx.Px(m_Cards ? 84.f : 44.f); }
        float PreferredWidth() const override { return m_Ctx.Px(m_Width); }
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
    };

    class TextField final : public Widget {
    private:
        std::string m_Value;
        std::string m_Placeholder;
        std::function<void()> m_OnChange;
        bool m_Focused{false};
        float m_Time{0.f};

    public:
        TextField(const Context& context, std::string placeholder, std::function<void()> onChange = {}) :
            Widget(context), m_Placeholder(std::move(placeholder)), m_OnChange(std::move(onChange)) {}

        [[nodiscard]]
        inline const std::string& Value() const noexcept { return m_Value; }
        void Paste();
        void Focus() noexcept { m_Focused = true; }

        float PreferredHeight(float) const override { return m_Ctx.Px(44.f); }
        void Update(float deltaTime, sf::Vector2f mouse) override { m_Time += deltaTime; }
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnText(char32_t unicode) override;
        bool OnKey(sf::Keyboard::Scancode key, bool control) override;
    };

    class TileGrid final : public Widget {
    public:
        struct Model {
            std::size_t Count;
            int Columns;
            float TileHeight;
            sf::Vector2f PreviewSize;
            std::function<std::string(std::size_t)> Name;
            std::function<void(const Painter&, sf::FloatRect, std::size_t)> DrawPreview;
            std::function<std::size_t()> Selected;
            std::function<void(std::size_t)> Select;
        };

    private:
        Model m_Model;
        int m_Hover{-1};
        int m_Pressed{-1};

        mutable float m_FitWidth{-1.f};
        mutable std::vector<std::string> m_Names;

        [[nodiscard]]
        sf::FloatRect tile(std::size_t index) const;

        [[nodiscard]]
        int indexAt(sf::Vector2f position) const;

    public:
        TileGrid(const Context& context, Model model) : Widget(context), m_Model(std::move(model)) {}

        float PreferredHeight(float width) const override;
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
    };

    class ScrollView final : public Widget {
    private:
        std::unique_ptr<Widget> m_Content;
        float m_MaxHeight;
        float m_Offset{0.f};
        float m_Target{0.f};
        float m_ContentHeight{0.f};
        bool m_StickToEnd{false};

        [[nodiscard]]
        float maxOffset() const noexcept;

        void layout();

    public:
        ScrollView(const Context& context, std::unique_ptr<Widget> content, float maxHeight = 0.f) :
            Widget(context), m_Content(std::move(content)), m_MaxHeight(maxHeight) {}

        void ScrollToEnd() noexcept { m_StickToEnd = true; }

        float PreferredHeight(float width) const override;
        void SetRect(sf::FloatRect rect) override;
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
        bool OnScroll(sf::Vector2f position, float delta) override;
        void OnText(char32_t unicode) override;
        bool OnKey(sf::Keyboard::Scancode key, bool control) override;
    };

    class Header final : public Widget {
    private:
        std::string m_Title;
        Button m_Close;

    public:
        Header(const Context& context, std::string title, std::function<void()> onClose) :
            Widget(context), m_Title(std::move(title)),
            m_Close(context, "", ButtonKind::IconOnly, UI::Icon::Close, std::move(onClose)) {}

        void SetTitle(std::string title) { m_Title = std::move(title); }

        float PreferredHeight(float) const override { return m_Ctx.Px(44.f); }
        void SetRect(sf::FloatRect rect) override;
        void Update(float deltaTime, sf::Vector2f mouse) override { m_Close.Update(deltaTime, mouse); }
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override { return m_Close.OnPressed(position); }
        void OnReleased(sf::Vector2f position) override { m_Close.OnReleased(position); }
    };

    class FormRow final : public Widget {
    private:
        std::string m_Title;
        std::string m_Description;
        std::unique_ptr<Widget> m_Control;
        bool m_Pressed{false};

        mutable float m_WrapWidth{-1.f};
        mutable std::vector<std::string> m_Lines;

        [[nodiscard]]
        float textWidth(float width) const;

        void wrap(float width) const;

    public:
        FormRow(const Context& context, std::string title, std::string description, std::unique_ptr<Widget> control) :
            Widget(context), m_Title(std::move(title)), m_Description(std::move(description)), m_Control(std::move(control)) {}

        float PreferredHeight(float width) const override;
        void SetRect(sf::FloatRect rect) override;
        void Update(float deltaTime, sf::Vector2f mouse) override { m_Control->Update(deltaTime, mouse); }
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
        bool OnScroll(sf::Vector2f, float) override { return false; }
    };

    class TabView final : public Widget {
    private:
        struct Page {
            std::string Name;
            std::unique_ptr<ScrollView> View;
        };

        std::vector<Page> m_Pages;
        Header m_Header;
        std::size_t m_Active{0};
        int m_Hover{-1};

        [[nodiscard]]
        sf::FloatRect tabRect(std::size_t index) const;

        [[nodiscard]]
        sf::FloatRect contentRect() const;

    public:
        TabView(const Context& context, std::function<void()> onClose) :
            Widget(context), m_Header(context, "", std::move(onClose)) {}

        void AddPage(std::string name, std::unique_ptr<Widget> content);

        float PreferredHeight(float) const override { return 0.f; }
        void SetRect(sf::FloatRect rect) override;
        void Update(float deltaTime, sf::Vector2f mouse) override;
        void Render(const Painter& painter) const override;
        bool OnPressed(sf::Vector2f position) override;
        void OnReleased(sf::Vector2f position) override;
        bool OnScroll(sf::Vector2f position, float delta) override;
        void OnText(char32_t unicode) override;
        bool OnKey(sf::Keyboard::Scancode key, bool control) override;
    };

    class Dialog final {
    private:
        const Context& m_Ctx;
        std::unique_ptr<Widget> m_Root;
        float m_Width, m_Height, m_Padding;
        sf::Vector2f m_Window;
        sf::FloatRect m_Frame;
        float m_Appear{0.f};
        bool m_Closed{false};

        void layout();

    public:
        Dialog(const Context& context, std::unique_ptr<Widget> root, float width, float height = 0.f, float padding = 24.f) :
            m_Ctx(context), m_Root(std::move(root)), m_Width(width), m_Height(height), m_Padding(padding) {}

        void SetWindow(sf::Vector2f size);
        void Update(float deltaTime, sf::Vector2f mouse);
        void Render(const Painter& painter) const;

        void OnPressed(sf::Vector2f position);
        void OnReleased(sf::Vector2f position);
        void OnScroll(sf::Vector2f position, float delta);
        void OnText(char32_t unicode);
        void OnKey(sf::Keyboard::Scancode key, bool control);

        inline void Close() noexcept { m_Closed = true; }

        [[nodiscard]]
        inline bool Closed() const noexcept { return m_Closed; }
    };
}
