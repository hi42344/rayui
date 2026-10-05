#pragma once

#include <raylib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <concepts>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <list>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <set>
#include <sstream>
#include <iomanip>

#include "../coordinate.hpp"
#include "../event.hpp"
#include "../tween.hpp"

namespace rayui {

    // Core types //

    struct Size { double width = 0.0; double height = 0.0; };
    inline constexpr double AUTO_SIZE = -1.0;

    enum class Align { Start, Center, End, Stretch };
    enum class Axis { Vertical, Horizontal };
    enum class Fit { Contain, Cover, Stretch };
    enum class Anchor { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };
    enum class Toast_Level { Info, Success, Warning, Error };
    enum class Side { Top, Bottom, Left, Right };

    struct Key_State {
        bool backspace = false;
        bool del = false;
        bool left = false;
        bool right = false;
        bool home = false;
        bool end = false;
        bool enter = false;
        bool escape = false;
        bool select_all = false;
        bool copy = false;
        bool cut = false;
        bool paste = false;
        bool shift = false;
        bool ctrl = false;
        bool up = false;
        bool down = false;
        bool tab = false;
        bool space = false;
    };

    struct Input_State {
        coordinate::pos mouse;
        bool pressed = false;
        bool released = false;
        bool right_pressed = false;
        bool down = false;
        bool consumed = false;
        bool inactive = false;
        double wheel = 0.0;
        std::string typed;
        Key_State keys;
        std::vector<int> suppressed_keys;
    };
    //////////////////////////////////////////////////

    namespace widget { class Widget; }

    namespace Internal {
        inline coordinate::mapper mapper;
        inline std::uint64_t layout_generation = 1;
        inline widget::Widget* focus = nullptr;
        inline bool focus_claimed = false;
        inline std::vector<Rectangle> clip_stack;
        inline widget::Widget* popup = nullptr;
        inline std::vector<const widget::Widget*> overlay_queue;
        inline double opacity = 1.0;
        inline bool dimmed = false;
        inline widget::Widget* modal = nullptr;

        inline Color faded(Color color) {
            color.a = static_cast<unsigned char>(std::lround(color.a * opacity));
            return color;
        }
    }

    inline void request_layout() { ++Internal::layout_generation; }
    inline void clear_focus();

    // Theme //
    struct Theme {
        Color panel = { 45, 45, 52, 255 };
        Color text = { 235, 235, 240, 255 };
        Color button = { 70, 72, 90, 255 };
        Color button_hover = { 90, 94, 120, 255 };
        Color button_pressed = { 55, 57, 72, 255 };
        Color track = { 30, 30, 36, 255 };
        Color accent = { 96, 140, 255, 255 };
        Color knob = { 240, 240, 245, 255 };
        Color input = { 30, 30, 36, 255 };
        Color placeholder = { 140, 140, 150, 255 };
        Color selection = { 96, 140, 255, 110 };
        Color divider = { 70, 70, 80, 255 };
        Color tab_inactive = { 170, 170, 180, 255 };
        Color tooltip = { 18, 18, 22, 240 };
        Color popup = { 52, 52, 62, 255 };
        Color backdrop = { 0, 0, 0, 180 };
        double disabled_opacity = 0.45;
    };

    inline Theme theme;
    //////////////////////////////////////////////////

    // Font //
    class Font_Manager {
    public:
        double spacing = 0.05;

        bool load(const std::string& path, int base_size = 64) {
            Font loaded = LoadFontEx(path.c_str(), base_size, nullptr, 0);
            if (loaded.texture.id == 0 || loaded.texture.id == GetFontDefault().texture.id) return false;

            unload();
            SetTextureFilter(loaded.texture, TEXTURE_FILTER_BILINEAR);
            m_font = loaded;
            m_loaded = true;
            ++m_revision;
            m_advances.clear();
            request_layout();
            return true;
        }

        void unload() {
            if (!m_loaded) return;
            UnloadFont(m_font);
            m_loaded = false;
            ++m_revision;
            m_advances.clear();
            request_layout();
        }

        Font get() const { return m_loaded ? m_font : GetFontDefault(); }
        unsigned revision() const { return m_revision; }

        Size measure(const std::string& text, double size) const {
            if (text.empty()) return { 0.0, size };

            Vector2 extent = MeasureTextEx(get(), text.c_str(), REF_SIZE, static_cast<float>(REF_SIZE * spacing));
            double k = size / static_cast<double>(REF_SIZE);
            return { extent.x * k, extent.y * k };
        }

        double pen(const std::string& text, double size) const {
            if (text.empty()) return 0.0;

            const float spacing_px = static_cast<float>(REF_SIZE * spacing);
            Vector2 extent = MeasureTextEx(get(), text.c_str(), REF_SIZE, spacing_px);
            return (static_cast<double>(extent.x) + static_cast<double>(spacing_px)) / static_cast<double>(REF_SIZE) * size;
        }

        double advance(int codepoint, double size) const {
            if (m_advance_spacing != spacing) {
                m_advances.clear();
                m_advance_spacing = spacing;
            }

            auto found = m_advances.find(codepoint);
            if (found == m_advances.end()) {
                int bytes = 0;
                const char* utf8 = CodepointToUTF8(codepoint, &bytes);
                std::string glyph(utf8, static_cast<size_t>(bytes));
                found = m_advances.emplace(codepoint, pen(glyph, 1.0)).first;
            }
            return found->second * size;
        }

        void draw(const std::string& text, coordinate::pos top_left, double size, Color color) const {
            if (text.empty()) return;

            Color faded = Internal::faded(color);
            if (faded.a == 0) return;

            float px = Internal::mapper.logical_to_screen_length(size);
            DrawTextEx(get(), text.c_str(), Internal::mapper.logical_to_screen(top_left), px,
                px * static_cast<float>(spacing), faded);
        }

    private:
        static constexpr float REF_SIZE = 100.0f;
        Font m_font{};
        bool m_loaded = false;
        unsigned m_revision = 0;
        mutable std::unordered_map<int, double> m_advances;
        mutable double m_advance_spacing = -1.0;
    };

    inline Font_Manager font;
    //////////////////////////////////////////////////

    // Meshes //
    namespace mesh {

        inline coordinate::tri_mesh from_polygon(const std::vector<coordinate::pos>& points, coordinate::pos center) {
            coordinate::tri_mesh result;
            result.tris.reserve(points.size());
            for (size_t i = 0; i < points.size(); ++i) {
                result.tris.push_back({ center, points[i], points[(i + 1) % points.size()] });
            }
            result.update_bounds();
            return result;
        }

        inline coordinate::tri_mesh make_rect(const coordinate::rect& r, double radius = 0.0, int corner_segments = 6) {
            radius = std::clamp(radius, 0.0, std::min(r.width, r.height) * 0.5);
            coordinate::pos center{ r.x + r.width * 0.5, r.y + r.height * 0.5 };

            std::vector<coordinate::pos> points;
            if (radius <= 0.0) {
                points = {
                    { r.x, r.y },
                    { r.x + r.width, r.y },
                    { r.x + r.width, r.y + r.height },
                    { r.x, r.y + r.height }
                };
                return from_polygon(points, center);
            }

            const std::array<coordinate::pos, 4> corners = { {
                { r.x + r.width - radius, r.y + radius },
                { r.x + r.width - radius, r.y + r.height - radius },
                { r.x + radius, r.y + r.height - radius },
                { r.x + radius, r.y + radius }
            } };
            constexpr double QUARTER = std::numbers::pi * 0.5;

            for (int c = 0; c < 4; ++c) {
                double start = (c - 1) * QUARTER;
                for (int i = 0; i <= corner_segments; ++i) {
                    double angle = start + QUARTER * i / corner_segments;
                    points.push_back({ corners[c].x + std::cos(angle) * radius, corners[c].y + std::sin(angle) * radius });
                }
            }

            return from_polygon(points, center);
        }

        inline coordinate::tri_mesh make_circle(coordinate::pos center, double radius, int segments = 24) {
            std::vector<coordinate::pos> points;
            points.reserve(static_cast<size_t>(segments));
            for (int i = 0; i < segments; ++i) {
                double angle = 2.0 * std::numbers::pi * i / segments;
                points.push_back({ center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius });
            }
            return from_polygon(points, center);
        }

        inline coordinate::tri_mesh make_line(coordinate::pos a, coordinate::pos b, double thickness) {
            coordinate::tri_mesh m;
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len <= 1e-9) return m;

            const double nx = -dy / len * thickness * 0.5;
            const double ny = dx / len * thickness * 0.5;

            m.tris.push_back({ { a.x + nx, a.y + ny },{ b.x + nx, b.y + ny },{ b.x - nx, b.y - ny } });
            m.tris.push_back({ { a.x + nx, a.y + ny },{ b.x - nx, b.y - ny },{ a.x - nx, a.y - ny } });
            m.update_bounds();
            return m;
        }

        inline void draw(const coordinate::tri_mesh& m, Color color) {
            color = Internal::faded(color);
            if (color.a == 0) return;

            for (const auto& t : m.tris) {
                Vector2 a = Internal::mapper.logical_to_screen(t.a);
                Vector2 b = Internal::mapper.logical_to_screen(t.b);
                Vector2 c = Internal::mapper.logical_to_screen(t.c);

                float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (cross > 0.0f) std::swap(b, c);

                DrawTriangle(a, b, c, color);
            }
        }

        /// @brief Soft drop shadow behind a rounded rectangle. Stacks several
        /// rounded rects of decreasing size with a small alpha each, giving a
        /// natural falloff. Draw this *before* the panel it belongs to.
        inline void drop_shadow(const coordinate::rect& r, double corner_radius,
            double spread = 10.0, double strength = 0.4, Color color = BLACK) {
            constexpr int LAYERS = 8;
            const unsigned char base_alpha = static_cast<unsigned char>(
                std::clamp(strength * 255.0 / LAYERS, 3.0, 40.0));

            for (int i = LAYERS; i >= 1; --i) {
                const double t = static_cast<double>(i) / LAYERS;
                const double grow = spread * t;
                const double y_offset = 2.0 * t * t;
                Color c = color;
                c.a = base_alpha;
                mesh::draw(
                    mesh::make_rect({
                        r.x - grow,
                        r.y - grow + y_offset,
                        r.width + grow * 2.0,
                        r.height + grow * 2.0
                        }, corner_radius + grow),
                    c);
            }
        }

    }
    //////////////////////////////////////////////////

    // State //
    template <typename T>
    class State {
    public:
        using value_type = T;

        State() : m_box(std::make_shared<Box>()) {}
        explicit State(T initial) : m_box(std::make_shared<Box>(std::move(initial))) {}

        const T& get() const { return m_box->value; }

        void set(T new_value) const {
            if constexpr (std::equality_comparable<T>) {
                if (m_box->value == new_value) return;
            }
            m_box->value = std::move(new_value);
            request_layout();
            m_box->changed.Fire(m_box->value);
        }

        void toggle() const requires std::same_as<T, bool> { set(!get()); }

        event::Signal<T>& OnChanged() const { return m_box->changed; }
        std::shared_ptr<void> anchor() const { return m_box; }

    private:
        struct Box {
            T value{};
            event::Signal<T> changed;
            Box() = default;
            explicit Box(T initial) : value(std::move(initial)) {}
        };

        std::shared_ptr<Box> m_box;
    };

    template <typename T>
    inline auto make_state(T initial) { return State<T>(std::move(initial)); }
    //////////////////////////////////////////////////

    class Text;

    // Format helpers //
    namespace detail {

        template <typename T>
        inline std::string fmt_value(const T& value) {
            using V = std::remove_cvref_t<T>;
            if constexpr (std::same_as<V, Text>) {
                return value.get();
            }
            else if constexpr (std::same_as<V, bool>) return value ? "true" : "false";
            else if constexpr (std::integral<V>) return std::to_string(value);
            else if constexpr (std::floating_point<V>) {
                std::ostringstream ss;
                ss << value;
                return ss.str();
            }
            else return std::string(value);
        }

        template <typename T> struct is_state_type : std::false_type {};
        template <typename T> struct is_state_type<State<T>> : std::true_type {};

        struct Format_Link {
            std::string pattern;
            State<std::string> target;
            std::vector<std::function<std::string()>> getters;
            std::list<event::ScopedConnection> connections;
            std::vector<std::shared_ptr<void>> anchors;

            void render() {
                std::string out;
                out.reserve(pattern.size() + getters.size() * 8);

                size_t arg = 0;
                size_t i = 0;
                while (i < pattern.size()) {
                    const char c = pattern[i];
                    if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '{') { out += '{'; i += 2; }
                    else if (c == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') { out += '}'; i += 2; }
                    else if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
                        if (arg < getters.size()) out += getters[arg++]();
                        i += 2;
                    }
                    else { out += c; ++i; }
                }
                target.set(std::move(out));
            }
        };

        template <typename T>
        inline auto make_format_getter(T value) {
            if constexpr (is_state_type<std::remove_cvref_t<T>>::value) {
                return [value]() { return fmt_value(value.get()); };
            }
            else {
                return [value]() { return fmt_value(value); };
            }
        }

        template <typename T>
        inline void attach_format_subscription(std::shared_ptr<Format_Link>, T) {}

        template <typename T>
        inline void attach_format_subscription(std::shared_ptr<Format_Link> link, State<T> state) {
            link->anchors.push_back(state.anchor());
            std::weak_ptr<Format_Link> weak = link;
            link->connections.emplace_back(state.OnChanged().Connect(
                [weak](const T&) { if (auto l = weak.lock()) l->render(); }));
        }

    }
    //////////////////////////////////////////////////

    // Text //
    class Text {
    public:
        Text() : m_state(std::string()) {}
        Text(const char* literal) : m_state(std::string(literal)) {}
        Text(std::string value) : m_state(std::move(value)) {}
        Text(State<std::string> state) : m_state(std::move(state)) {}

        template <typename T, typename Format>
            requires std::invocable<Format&, const T&>&&
        std::convertible_to<std::invoke_result_t<Format&, const T&>, std::string>
            static Text from(State<T> source, Format format) {
            Text result{ std::string(format(source.get())) };
            result.m_keepalive = std::make_shared<Link<T, Format>>(std::move(source), result.m_state, std::move(format));
            return result;
        }

        template <typename... Args>
        static Text from_format(std::string pattern, Args... args) {
            Text result{ std::string() };

            auto link = std::make_shared<detail::Format_Link>();
            link->pattern = std::move(pattern);
            link->target = result.m_state;

            (link->getters.emplace_back(detail::make_format_getter(args)), ...);
            (detail::attach_format_subscription(link, args), ...);

            link->render();
            result.m_keepalive = link;
            return result;
        }

        const std::string& get() const { return m_state.get(); }
        event::Signal<std::string>& OnChanged() const { return m_state.OnChanged(); }
        std::shared_ptr<void> anchor() const { return m_keepalive ? m_keepalive : m_state.anchor(); }

    private:
        template <typename T, typename Format>
        struct Link {
            State<T> source;
            State<std::string> target;
            event::ScopedConnection connection;

            Link(State<T> from, State<std::string> to, Format format)
                : source(std::move(from))
                , target(std::move(to))
                , connection(source.OnChanged().Connect([to = target, format](const T& value) mutable {
                to.set(std::string(format(value)));
                    })) {
            }
        };

        State<std::string> m_state;
        std::shared_ptr<void> m_keepalive;
    };

    /// @brief Reactive text interpolation. Args may be States or plain values.
    /// Use `{}` for a slot, `{{` / `}}` for literal braces.
    template <typename... Args>
    inline Text fmt(std::string pattern, Args&&... args) {
        return Text::from_format(std::move(pattern), std::forward<Args>(args)...);
    }
    //////////////////////////////////////////////////

    // Animation //
    namespace detail {

        using Tween = tween::Tween<float>;

        struct Animation_Box {
            float value = 0.0f;
            float target = 0.0f;
            std::unique_ptr<Tween> active;
            bool running = false;
            bool queued = false;
            event::Signal<double> changed;
            event::Signal<> finished;

            Animation_Box() = default;
            explicit Animation_Box(double initial)
                : value(static_cast<float>(initial))
                , target(static_cast<float>(initial)) {
            }
        };

    }

    class Animated;

    class Animator {
    public:
        void update(double dt) {
            if (m_active.empty()) return;

            auto snapshot = std::move(m_active);
            m_active.clear();

            for (auto& weak : snapshot) {
                auto box = weak.lock();
                if (!box) continue;

                box->queued = false;
                if (!box->running || !box->active) continue;

                box->active->update(static_cast<float>(dt));
                const bool done = box->active->is_done();
                if (done) box->value = box->target;
                box->changed.Fire(static_cast<double>(box->value));

                if (!box->running || box->queued) continue;

                if (done) {
                    box->running = false;
                    box->finished.Fire();
                }
                else {
                    box->queued = true;
                    m_active.push_back(weak);
                }
            }
        }

        void tick(double now) {
            double dt = m_last_tick < 0.0 ? 0.0 : std::min(now - m_last_tick, MAX_STEP);
            m_last_tick = now;
            update(std::max(dt, 0.0));
        }

        size_t active_count() const { return m_active.size(); }

    private:
        friend class Animated;
        static constexpr double MAX_STEP = 0.1;
        double m_last_tick = -1.0;
        std::vector<std::weak_ptr<detail::Animation_Box>> m_active;
    };

    inline Animator animator;

    // Hotkeys //
    enum Hotkey_Mod {
        HOTKEY_NONE = 0,
        HOTKEY_SHIFT = 1 << 0,
        HOTKEY_CTRL = 1 << 1,
        HOTKEY_ALT = 1 << 2,
        HOTKEY_SUPER = 1 << 3,
    };

    namespace Internal {
        struct Hotkey_Entry {
            int key = 0;
            int mods = 0;
            std::function<void()> callback;
            std::uint64_t id = 0;
        };

        inline std::vector<Hotkey_Entry> hotkeys;
        inline std::uint64_t next_hotkey_id = 1;

        inline int current_hotkey_mods() {
            int mods = 0;
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) mods |= HOTKEY_SHIFT;
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= HOTKEY_CTRL;
            if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) mods |= HOTKEY_ALT;
            if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) mods |= HOTKEY_SUPER;
            return mods;
        }
    }

    struct Hotkey_Handle {
        std::uint64_t id = 0;

        void remove() const {
            auto& v = Internal::hotkeys;
            v.erase(std::remove_if(v.begin(), v.end(),
                [this](const Internal::Hotkey_Entry& e) { return e.id == id; }), v.end());
        }

        explicit operator bool() const { return id != 0; }
    };

    /// @brief Register a global keyboard shortcut. Re-registering the same
    /// key + mods replaces the previous binding.
    inline Hotkey_Handle hotkey(int key, int mods, std::function<void()> callback) {
        auto& v = Internal::hotkeys;
        v.erase(std::remove_if(v.begin(), v.end(),
            [key, mods](const Internal::Hotkey_Entry& e) {
                return e.key == key && e.mods == mods;
            }), v.end());

        Internal::Hotkey_Entry entry;
        entry.key = key;
        entry.mods = mods;
        entry.callback = std::move(callback);
        entry.id = Internal::next_hotkey_id++;
        const std::uint64_t id = entry.id;
        v.push_back(std::move(entry));
        return Hotkey_Handle{ id };
    }
    //////////////////////////////////////////////////

    inline tween::TweenInfo ease(double seconds, tween::EasingStyle style = tween::EasingStyle::Quad,
        tween::EasingDirection direction = tween::EasingDirection::Out) {
        tween::TweenInfo info;
        info.time = static_cast<float>(seconds);
        info.easing_style = style;
        info.easing_direction = direction;
        return info;
    }

    class Animated {
    public:
        Animated() : m_box(std::make_shared<detail::Animation_Box>()) {}
        explicit Animated(double initial) : m_box(std::make_shared<detail::Animation_Box>(initial)) {}

        double get() const { return static_cast<double>(m_box->value); }

        void set(double value) const {
            cancel();

            float next = static_cast<float>(value);
            if (m_box->value == next) return;

            m_box->value = next;
            m_box->target = next;
            m_box->changed.Fire(get());
        }

        void animate_to(double target, const tween::TweenInfo& info) const {
            if (info.time <= 0.0f) { set(target); return; }

            m_box->active.reset();
            m_box->target = static_cast<float>(target);
            m_box->active = std::make_unique<detail::Tween>(
                tween::TweenService::create(m_box->value, info, m_box->target));
            m_box->active->play();
            m_box->running = true;

            if (!m_box->queued) {
                m_box->queued = true;
                animator.m_active.push_back(m_box);
            }
        }

        void animate_to(double target, double seconds) const { animate_to(target, ease(seconds)); }

        void stop() const { cancel(); }
        bool is_animating() const { return m_box->running; }

        event::Signal<double>& OnChanged() const { return m_box->changed; }
        event::Signal<>& OnFinished() const { return m_box->finished; }
        std::shared_ptr<void> anchor() const { return m_box; }

    private:
        void cancel() const {
            m_box->running = false;
            m_box->active.reset();
        }

        std::shared_ptr<detail::Animation_Box> m_box;
    };
    //////////////////////////////////////////////////

}