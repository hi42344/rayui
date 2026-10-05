#pragma once

#include "core.hpp"
#include "detail.hpp"

namespace rayui {

    class Element;

    namespace widget {

        // Base //
        class Widget {
        public:
            Widget(double width, double height, double flex)
                : m_width(width)
                , m_height(height)
                , m_flex(flex) {
            }

            virtual ~Widget() {
                if (Internal::focus == this) Internal::focus = nullptr;
                if (Internal::popup == this) Internal::popup = nullptr;
                if (Internal::modal == this) Internal::modal = nullptr;
            }
            Widget(const Widget&) = delete;
            Widget& operator=(const Widget&) = delete;

            Size desired() const {
                if (!m_visible) return {};

                Size content = measure();
                return {
                    m_width >= 0.0 ? m_width : content.width,
                    m_height >= 0.0 ? m_height : content.height
                };
            }

            virtual Size desired_for_width(double /*width*/) const { return desired(); }

            virtual double flex_weight() const { return m_flex; }

            double fixed_width() const { return m_width; }
            double fixed_height() const { return m_height; }

            bool is_enabled() const { return m_enabled; }
            void set_enabled(bool enabled) { m_enabled = enabled; }

            bool is_visible() const { return m_visible; }
            void set_visible(bool visible) {
                if (visible == m_visible) return;
                m_visible = visible;
                request_layout();
            }

            double opacity() const { return m_opacity; }
            void set_opacity(double opacity) { m_opacity = std::clamp(opacity, 0.0, 1.0); }

            void dispatch(Input_State& input) {
                if (m_enabled && m_visible && !input.inactive) {
                    update(input);
                    return;
                }
                go_inert();
            }

            void go_inert() {
                Input_State inert;
                inert.mouse = { INERT_POSITION, INERT_POSITION };
                inert.released = true;
                inert.consumed = true;
                inert.inactive = true;
                update(inert);

                if (Internal::focus == this) clear_focus();
            }

            void render() const {
                if (!m_visible || m_opacity <= 0.0) return;

                const double previous_opacity = Internal::opacity;
                const bool previous_dimmed = Internal::dimmed;

                Internal::opacity *= m_opacity;
                if (!m_enabled && !Internal::dimmed) {
                    Internal::opacity *= theme.disabled_opacity;
                    Internal::dimmed = true;
                }

                m_effective_opacity = Internal::opacity;
                const bool focused = (Internal::focus == this) && focusable() && m_enabled;
                if (focused && !has_custom_focus_ring()) draw_focus_ring();
                draw();

                Internal::opacity = previous_opacity;
                Internal::dimmed = previous_dimmed;
            }

            void queue_overlay() const { Internal::overlay_queue.push_back(this); }

            void render_overlay() const {
                const double previous_opacity = Internal::opacity;
                Internal::opacity = m_effective_opacity;
                draw_overlay();
                Internal::opacity = previous_opacity;
            }

            virtual void arrange(const coordinate::rect& bounds) {
                m_bounds = bounds;
                m_mesh = mesh::make_rect(bounds);
            }

            virtual void update(Input_State&) {}
            virtual void draw() const = 0;

            const coordinate::tri_mesh& hit_mesh() const { return m_mesh; }
            const coordinate::rect& bounds() const { return m_bounds; }

            std::uint64_t layout_stamp() const { return m_layout_stamp; }
            void set_layout_stamp(std::uint64_t stamp) { m_layout_stamp = stamp; }

            virtual void on_focus_changed(bool) {}

            virtual void update_popup(Input_State&) {}
            virtual void close_popup() {}

            virtual void draw_overlay() const {}

            virtual bool fills_cross_axis(Axis /*stack_axis*/) const { return false; }

            template <typename Source, typename F>
            void watch(const Source& source, F&& callback) {
                m_anchors.push_back(source.anchor());
                m_connections.emplace_back(source.OnChanged().Connect(std::forward<F>(callback)));
            }

            virtual bool focusable() const { return false; }

            virtual void collect_focusables(std::vector<Widget*>& out) {
                if (focusable() && is_visible() && is_enabled()) out.push_back(this);
            }

            bool has_focus() const { return Internal::focus == this; }

            virtual void draw_focus_ring() const {
                constexpr double RING = 3.0;
                coordinate::rect ring{
                    m_bounds.x - RING, m_bounds.y - RING,
                    m_bounds.width + RING * 2.0, m_bounds.height + RING * 2.0
                };
                mesh::draw(mesh::make_rect(ring, 6.0), theme.accent);
            }

            virtual bool has_custom_focus_ring() const { return false; }

        protected:
            virtual Size measure() const = 0;

            template <typename Signal, typename F>
            void attach(Signal& signal, F&& callback) {
                m_connections.emplace_back(signal.Connect(std::forward<F>(callback)));
            }

            coordinate::rect m_bounds;
            coordinate::tri_mesh m_mesh;

        private:
            static constexpr double INERT_POSITION = -1.0e9;

            double m_width;
            double m_height;
            double m_flex;
            double m_opacity = 1.0;
            mutable double m_effective_opacity = 1.0;
            bool m_enabled = true;
            bool m_visible = true;
            std::uint64_t m_layout_stamp = 0;
            std::vector<std::shared_ptr<void>> m_anchors;
            std::list<event::ScopedConnection> m_connections;
        };
        //////////////////////////////////////////////////

    }

    // Focus //
    inline void focus_widget(widget::Widget* target) {
        if (target) Internal::focus_claimed = true;
        if (Internal::focus == target) return;

        widget::Widget* previous = Internal::focus;
        Internal::focus = target;
        if (previous) previous->on_focus_changed(false);
        if (target) target->on_focus_changed(true);
    }

    inline void clear_focus() {
        focus_widget(nullptr);
    }
    //////////////////////////////////////////////////

    // Element //
    class Element {
    public:
        Element() = default;

        template <std::derived_from<widget::Widget> T>
        Element(std::shared_ptr<T> ptr)
            : m_widget(std::move(ptr)) {
        }

        void update(Vector2 mouse, bool pressed) {
            update(mouse, pressed, IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonReleased(MOUSE_BUTTON_LEFT));
        }

        void update(Vector2 mouse, bool pressed, bool down, bool released) {
            if (!m_widget) return;

            Internal::mapper.update();
            animator.tick(GetTime());
            relayout();

            Input_State input;
            input.mouse = Internal::mapper.screen_to_logical(mouse);
            input.pressed = pressed;
            input.down = down;
            input.released = released;
            input.right_pressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
            input.wheel = static_cast<double>(GetMouseWheelMove());
            detail::poll_keyboard(input);

            if (Internal::popup) Internal::popup->update_popup(input);

            // A popup that took the click owns it for the rest of this frame. Clearing
            // the button flags here prevents a widget that opens as a side effect of
            // the click (e.g. a modal raised by a context menu item) from also seeing
            // that same press.
            if (input.consumed) {
                input.pressed = false;
                input.released = false;
            }

            Internal::focus_claimed = false;

            widget::Widget* target = Internal::modal ? Internal::modal : m_widget.get();
            target->dispatch(input);

            if (pressed && !Internal::focus_claimed) clear_focus();

            if (input.keys.tab) {
                std::vector<widget::Widget*> focusables;
                target->collect_focusables(focusables);
                if (!focusables.empty()) {
                    widget::Widget* current = Internal::focus;
                    int index = -1;
                    for (size_t i = 0; i < focusables.size(); ++i) {
                        if (focusables[i] == current) { index = static_cast<int>(i); break; }
                    }
                    const int count = static_cast<int>(focusables.size());
                    const int next = input.keys.shift
                        ? (index <= 0 ? count - 1 : index - 1)
                        : (index < 0 || index + 1 >= count ? 0 : index + 1);
                    focus_widget(focusables[static_cast<size_t>(next)]);
                }
                input.keys.tab = false;
            }

            if (!Internal::hotkeys.empty()) {
                auto snapshot = Internal::hotkeys;
                const int mods = Internal::current_hotkey_mods();
                for (const auto& h : snapshot) {
                    if (!IsKeyPressed(h.key)) continue;
                    if (mods != h.mods) continue;

                    bool suppressed = false;
                    for (int k : input.suppressed_keys) if (k == h.key) { suppressed = true; break; }
                    if (suppressed) continue;

                    h.callback();
                }
            }

            relayout();
        }

        void draw() const {
            if (!m_widget) return;

            const size_t first = Internal::overlay_queue.size();
            m_widget->render();

            for (size_t i = first; i < Internal::overlay_queue.size(); ++i) Internal::overlay_queue[i]->render_overlay();
            Internal::overlay_queue.resize(first);
        }

        widget::Widget* get() const { return m_widget.get(); }
        explicit operator bool() const { return m_widget != nullptr; }

    protected:
        std::shared_ptr<widget::Widget> m_widget;

    private:
        void relayout() const {
            if (m_widget->layout_stamp() == Internal::layout_generation) return;

            m_widget->set_layout_stamp(Internal::layout_generation);
            m_widget->arrange(coordinate::rect{ 0.0, 0.0, Internal::mapper.get_width(), Internal::mapper.get_height() });
        }
    };

    template <typename T>
    class Ref : public Element {
    public:
        Ref() = default;
        explicit Ref(std::shared_ptr<T> ptr) : Element(std::move(ptr)) {}

        T* operator->() const { return static_cast<T*>(m_widget.get()); }
        T& operator*() const { return *static_cast<T*>(m_widget.get()); }

        template <typename F> requires requires(T& t) { t.OnClick; }
        Ref onClick(F&& callback) const {
            (*this)->OnClick.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnChanged; }
        Ref onChange(F&& callback) const {
            (*this)->OnChanged.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnHoverChanged; }
        Ref onHover(F&& callback) const {
            (*this)->OnHoverChanged.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnSubmit; }
        Ref onSubmit(F&& callback) const {
            (*this)->OnSubmit.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnFocusChanged; }
        Ref onFocus(F&& callback) const {
            (*this)->OnFocusChanged.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnClose; }
        Ref onClose(F&& callback) const {
            (*this)->OnClose.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnToggled; }
        Ref onToggled(F&& callback) const {
            (*this)->OnToggled.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnSelect; }
        Ref onSelect(F&& callback) const {
            (*this)->OnSelect.Connect(std::forward<F>(callback));
            return *this;
        }

        template <typename F> requires requires(T& t) { t.OnRowClick; }
        Ref onRowClick(F&& callback) const {
            (*this)->OnRowClick.Connect(std::forward<F>(callback));
            return *this;
        }

        Ref enabled(bool value) const {
            (*this)->set_enabled(value);
            return *this;
        }

        Ref enabled(const State<bool>& state) const {
            T* w = operator->();
            w->set_enabled(state.get());
            w->watch(state, [w](const bool& value) { w->set_enabled(value); });
            return *this;
        }

        Ref visible(bool value) const {
            (*this)->set_visible(value);
            return *this;
        }

        Ref visible(const State<bool>& state) const {
            T* w = operator->();
            w->set_visible(state.get());
            w->watch(state, [w](const bool& value) { w->set_visible(value); });
            return *this;
        }

        Ref opacity(double value) const {
            (*this)->set_opacity(value);
            return *this;
        }

        template <typename U> requires std::is_arithmetic_v<U>
        Ref opacity(const State<U>& state) const {
            T* w = operator->();
            w->set_opacity(static_cast<double>(state.get()));
            w->watch(state, [w](const U& value) { w->set_opacity(static_cast<double>(value)); });
            return *this;
        }
    };

    namespace detail {
        template <typename T, typename... Args>
        inline Ref<T> make_ref(Args&&... args) {
            return Ref<T>(std::make_shared<T>(std::forward<Args>(args)...));
        }
    }

    // Props //
    struct Panel_Props {
        double padding = 0.0;
        Color backgroundColor = theme.panel;
        double cornerRadius = 0.0;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Stack_Props {
        double spacing = 0.0;
        Align alignment = Align::Start;
        Align justify = Align::Start;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Label_Props {
        double fontSize = 40.0;
        Color color = theme.text;
        Align textAlign = Align::Start;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Button_Props {
        double padding = 16.0;
        double fontSize = 40.0;
        double cornerRadius = 12.0;
        Color textColor = theme.text;
        Color backgroundColor = theme.button;
        Color hoverColor = theme.button_hover;
        Color pressedColor = theme.button_pressed;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double fadeTime = 0.12;
    };

    struct Slider_Props {
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double trackHeight = 12.0;
        double knobRadius = 24.0;
        Color trackColor = theme.track;
        Color fillColor = theme.accent;
        Color knobColor = theme.knob;
    };

    struct Checkbox_Props {
        double fontSize = 40.0;
        Color textColor = theme.text;
        Color boxColor = theme.track;
        Color checkColor = theme.accent;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Textbox_Props {
        Text placeholder = {};
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double fontSize = 40.0;
        double padding = 16.0;
        double cornerRadius = 12.0;
        int maxLength = 0;
        Color textColor = theme.text;
        Color placeholderColor = theme.placeholder;
        Color backgroundColor = theme.input;
        Color focusColor = theme.accent;
        Color caretColor = theme.text;
        Color selectionColor = theme.selection;
    };

    struct Number_Input_Props {
        double width = 260.0;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double fontSize = 34.0;
        double padding = 12.0;
        double cornerRadius = 10.0;
        double step = 1.0;      // 0 = no rounding, > 0 = snap typed values to multiples
        double min = -std::numeric_limits<double>::infinity();
        double max = std::numeric_limits<double>::infinity();
        int decimals = 0;
        Color textColor = theme.text;
        Color backgroundColor = theme.input;
        Color focusColor = theme.accent;
    };

    struct Divider_Props {
        Axis axis = Axis::Horizontal;
        double thickness = 2.0;
        Color color = theme.divider;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Toggle_Props {
        double trackWidth = 84.0;
        double trackHeight = 44.0;
        Color offColor = theme.track;
        Color onColor = theme.accent;
        Color knobColor = theme.knob;
        double animateTime = 0.15;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Radio_Props {
        double fontSize = 40.0;
        Color textColor = theme.text;
        Color ringColor = theme.track;
        Color dotColor = theme.accent;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Tabs_Props {
        double fontSize = 40.0;
        double tabPadding = 28.0;
        double indicatorHeight = 5.0;
        double gap = 24.0;
        bool stretch = false;
        Color textColor = theme.tab_inactive;
        Color activeColor = theme.text;
        Color indicatorColor = theme.accent;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Column_Props {
        double width = AUTO_SIZE;
        double flex = 1.0;
        Align align = Align::Start;
    };

    struct Table_Column {
        Text header;
        double width = AUTO_SIZE;
        double flex = 1.0;
        Align align = Align::Start;
    };

    struct Table_Row {
        std::vector<std::string> cells;
    };

    struct Table_Props {
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double fontSize = 28.0;
        double headerFontSize = 30.0;
        double rowPadding = 10.0;
        double cellPadding = 14.0;
        double cornerRadius = 8.0;
        bool striped = true;
        bool stickyHeader = true;
        double scrollbarWidth = 8.0;
        double scrollbarMinWidth = 3.0;
        double scrollbarFadeTime = 0.4;
        double scrollbarIdleTime = 0.8;
        double wheelStep = 60.0;
        double smoothTime = 0.12;
        Color headerBackground = theme.button;
        Color rowColor = { 0, 0, 0, 0 };
        Color altRowColor = { 255, 255, 255, 12 };
        Color hoverColor = { 255, 255, 255, 24 };
        Color selectedColor = theme.accent;
        Color textColor = theme.text;
        Color selectedTextColor = WHITE;
        Color dividerColor = theme.divider;
    };

    struct Dropdown_Props {
        Text placeholder = {};
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double fontSize = 40.0;
        double padding = 16.0;
        double cornerRadius = 12.0;
        int maxVisible = 6;
        Color textColor = theme.text;
        Color placeholderColor = theme.placeholder;
        Color backgroundColor = theme.input;
        Color listColor = theme.popup;
        Color hoverColor = theme.button_hover;
        Color activeColor = theme.accent;
        Color focusColor = theme.accent;
    };

    struct Tooltip_Props {
        double delay = 0.5;
        double fontSize = 28.0;
        double padding = 12.0;
        double cornerRadius = 8.0;
        double offsetX = 16.0;
        double offsetY = 28.0;
        Color backgroundColor = theme.tooltip;
        Color textColor = theme.text;
    };

    struct Image_Props {
        Fit fit = Fit::Contain;
        Color tint = WHITE;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Text_Display_Props {
        double fontSize = 40.0;
        Color color = theme.text;
        Align textAlign = Align::Start;
        double lineHeight = 1.25;
        double maxWidth = AUTO_SIZE;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Text_Line_Props {
        double fontSize = 40.0;
        Color color = theme.text;
        Align textAlign = Align::Start;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double padding = 0.0;
        double flex = 0.0;
        double scrollbarHeight = 6.0;
        double scrollbarMinHeight = 2.0;
        double scrollbarFadeTime = 0.4;
        double scrollbarIdleTime = 0.8;
        double wheelStep = 60.0;
        double smoothTime = 0.12;
        Color scrollbarThumbColor = { 255, 255, 255, 110 };
        Color scrollbarTrackColor = { 0, 0, 0, 0 };
    };

    struct Text_Field_Props {
        Text placeholder = {};
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double fontSize = 40.0;
        double lineHeight = 1.25;
        double padding = 16.0;
        double cornerRadius = 12.0;
        int minLines = 3;
        int maxLength = 0;
        Color textColor = theme.text;
        Color placeholderColor = theme.placeholder;
        Color backgroundColor = theme.input;
        Color focusColor = theme.accent;
        Color caretColor = theme.text;
        Color selectionColor = theme.selection;
    };

    struct Progress_Props {
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double cornerRadius = 8.0;
        double animateTime = 0.25;
        Color trackColor = theme.track;
        Color fillColor = theme.accent;
    };

    struct Scroll_Props {
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        double scrollbarWidth = 10.0;
        double scrollbarMinWidth = 3.0;
        double scrollbarFadeTime = 0.4;
        double scrollbarIdleTime = 0.8;
        double wheelStep = 90.0;
        double smoothTime = 0.15;
        Color trackColor = { 0, 0, 0, 60 };
        Color thumbColor = { 255, 255, 255, 110 };
    };

    struct Positioned_Props {
        Anchor anchor = Anchor::TopLeft;
        double x = 0.0;
        double y = 0.0;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
    };

    struct Spacer_Props {
        double width = 0.0;
        double height = 0.0;
        double flex = 1.0;
    };

    struct Badge_Props {
        double fontSize = 28.0;
        double paddingX = 14.0;
        double paddingY = 6.0;
        double cornerRadius = 999.0;
        Color textColor = theme.text;
        Color backgroundColor = theme.accent;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Chip_Props {
        double fontSize = 32.0;
        double paddingX = 18.0;
        double paddingY = 8.0;
        double cornerRadius = 999.0;
        bool closeable = true;
        double fadeTime = 0.12;
        Color textColor = theme.text;
        Color backgroundColor = theme.button;
        Color hoverColor = theme.button_hover;
        Color closeColor = theme.placeholder;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Segmented_Props {
        double fontSize = 32.0;
        double padding = 20.0;
        double cornerRadius = 999.0;
        double animateTime = 0.2;
        Color textColor = theme.tab_inactive;
        Color activeTextColor = theme.text;
        Color trackColor = theme.track;
        Color thumbColor = theme.button;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Collapsible_Props {
        double fontSize = 40.0;
        double headerPadding = 20.0;
        double spacing = 12.0;
        double cornerRadius = 12.0;
        double animateTime = 0.2;
        bool defaultOpen = false;
        Color headerColor = theme.button;
        Color headerHoverColor = theme.button_hover;
        Color contentColor = theme.panel;
        Color textColor = theme.text;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Link_Props {
        double fontSize = 40.0;
        bool underlineOnHover = true;
        Color color = theme.accent;
        Color hoverColor = { 150, 180, 255, 255 };
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Rating_Props {
        int max = 5;
        double starSize = 48.0;
        double spacing = 8.0;
        Color filledColor = { 255, 200, 70, 255 };
        Color emptyColor = theme.track;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Grid_Props {
        int columns = 2;
        double spacingX = 12.0;
        double spacingY = 12.0;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Tree_Node_Data {
        Text label;
        std::vector<Tree_Node_Data> children;
        Color color = theme.text;
    };

    struct Tree_View_Props {
        double fontSize = 30.0;
        double rowPadding = 8.0;
        double indentWidth = 28.0;
        double arrowSize = 9.0;
        double arrowGap = 10.0;
        double sidePadding = 16.0;
        double cornerRadius = 6.0;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
        bool defaultExpanded = true;
        bool showGuides = true;
        double scrollbarWidth = 8.0;
        double scrollbarMinWidth = 3.0;
        double scrollbarFadeTime = 0.4;
        double scrollbarIdleTime = 0.8;
        double wheelStep = 90.0;
        double smoothTime = 0.15;
        Color hoverColor = { 255, 255, 255, 20 };
        Color selectedColor = theme.accent;
        Color selectedTextColor = WHITE;
        Color arrowColor = theme.tab_inactive;
        Color guideColor = { 120, 120, 130, 90 };
    };

    struct Shape_Props {
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Reveal_Props {
        double enterTime = 0.2;
        double exitTime = 0.15;
        double enterOffsetY = 12.0;
        double exitOffsetY = -12.0;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Popover_Props {
        Side side = Side::Bottom;
        double gap = 4.0;
        double cornerRadius = 12.0;
        double padding = 8.0;
        bool closeOnOutsideClick = true;
        bool closeOnEscape = true;
        Color backgroundColor = theme.popup;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
    };

    struct Modal_Props {
        bool dismissOnBackdrop = true;
        bool dismissOnEscape = true;
        double padding = 24.0;
        double cornerRadius = 16.0;
        Color backdropColor = theme.backdrop;
        Color contentColor = theme.panel;
        double contentWidth = 600.0;
        double contentHeight = AUTO_SIZE;
        double fadeTime = 0.15;
    };

    struct ContextMenu_Props {
        double fontSize = 32.0;
        double padding = 16.0;
        double rowPadding = 12.0;
        double cornerRadius = 10.0;
        double animateTime = 0.12;
        double scaleFrom = 0.9;
        Color textColor = theme.text;
        Color backgroundColor = theme.popup;
        Color hoverColor = theme.button_hover;
    };

    struct Wrap_Props {
        double spacingX = 8.0;
        double spacingY = 8.0;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Accordion_Props {
        double fontSize = 40.0;
        double gap = 8.0;
        double animateTime = 0.2;
        Color headerColor = theme.button;
        Color headerHoverColor = theme.button_hover;
        Color contentColor = theme.panel;
        Color textColor = theme.text;
        double width = AUTO_SIZE;
        double height = AUTO_SIZE;
        double flex = 0.0;
    };

    struct Accordion_Item {
        Text title;
        Element content;
    };

    struct Toast_Props {
        Toast_Level level = Toast_Level::Info;
        Color color = BLANK;   // alpha 0 = use the level's default
        double duration = 3.0;
        double fontSize = 30.0;
        double padding = 10.0;
        double lineHeight = 1.25;
        double cornerRadius = 10.0;
        double maxWidth = 480.0;
        Color textColor = theme.text;
        Color infoColor = { 60, 100, 180, 255 };
        Color successColor = { 60, 150, 90, 255 };
        Color warningColor = { 200, 150, 60, 255 };
        Color errorColor = { 190, 70, 70, 255 };
    };

    struct Toast_Host_Props {
        Anchor anchor = Anchor::TopRight;
        double marginX = 24.0;
        double marginY = 24.0;
        double gap = 12.0;
        double fadeInTime = 0.18;
        double fadeOutTime = 0.18;
    };
    //////////////////////////////////////////////////

    namespace widget {

        // Containers //
        class Panel : public Widget {
        public:
            Panel(const Panel_Props& props, std::vector<Element> children)
                : Widget(props.width, props.height, props.flex)
                , m_padding(props.padding)
                , m_background(props.backgroundColor)
                , m_corner_radius(props.cornerRadius)
                , m_children(std::move(children)) {
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);

                coordinate::rect inner{
                    bounds.x + m_padding,
                    bounds.y + m_padding,
                    std::max(0.0, bounds.width - m_padding * 2.0),
                    std::max(0.0, bounds.height - m_padding * 2.0)
                };

                for (const Element& child : m_children) {
                    if (child) child.get()->arrange(inner);
                }
            }

            void update(Input_State& input) override {
                for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
                    if (*it) it->get()->dispatch(input);
                }
            }

            void draw() const override {
                if (m_background.a > 0) mesh::draw(m_mesh, m_background);

                for (const Element& child : m_children) {
                    if (child) child.get()->render();
                }
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                for (const Element& child : m_children) if (child) child.get()->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};

                const double inner = std::max(0.0, width - m_padding * 2.0);
                double height = 0.0;
                for (const Element& child : m_children) {
                    if (child) height = std::max(height, child.get()->desired_for_width(inner).height);
                }
                return { width, fixed_height() >= 0.0 ? fixed_height() : height + m_padding * 2.0 };
            }

        protected:
            Size measure() const override {
                Size result;
                for (const Element& child : m_children) {
                    if (!child) continue;
                    Size s = child.get()->desired();
                    result.width = std::max(result.width, s.width);
                    result.height = std::max(result.height, s.height);
                }
                return { result.width + m_padding * 2.0, result.height + m_padding * 2.0 };
            }

        private:
            double m_padding;
            Color m_background;
            double m_corner_radius;
            std::vector<Element> m_children;
        };

        class Stack : public Widget {
        public:
            Stack(Axis axis, const Stack_Props& props, std::vector<Element> children)
                : Widget(props.width, props.height, props.flex)
                , m_children(std::move(children))
                , m_axis(axis)
                , m_spacing(props.spacing)
                , m_alignment(props.alignment)
                , m_justify(props.justify) {
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                std::vector<Widget*> live;
                std::vector<Size> sizes;
                double total = 0.0;
                double flex_sum = 0.0;

                const double cross_avail = cross_of({ bounds.width, bounds.height });
                const bool width_aware = m_axis == Axis::Vertical && m_alignment == Align::Stretch;

                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    live.push_back(child.get());
                    sizes.push_back(width_aware ? live.back()->desired_for_width(cross_avail) : live.back()->desired());
                    total += main_of(sizes.back());
                    flex_sum += live.back()->flex_weight();
                }

                if (live.empty()) return;
                total += m_spacing * static_cast<double>(live.size() - 1);

                const double main_avail = main_of({ bounds.width, bounds.height });
                const double extra = std::max(0.0, main_avail - total);

                double cursor = 0.0;
                if (flex_sum <= 0.0) {
                    if (m_justify == Align::Center) cursor = extra * 0.5;
                    else if (m_justify == Align::End) cursor = extra;
                }

                for (size_t i = 0; i < live.size(); ++i) {
                    double main_size = main_of(sizes[i]);
                    if (flex_sum > 0.0) main_size += extra * live[i]->flex_weight() / flex_sum;

                    double cross_size = cross_of(sizes[i]);
                    double cross_offset = 0.0;

                    switch (live[i]->fills_cross_axis(m_axis) ? Align::Stretch : m_alignment) {
                    case Align::Stretch: cross_size = cross_avail; break;
                    case Align::Center: cross_offset = (cross_avail - cross_size) * 0.5; break;
                    case Align::End: cross_offset = cross_avail - cross_size; break;
                    case Align::Start: break;
                    }

                    if (m_axis == Axis::Vertical) {
                        live[i]->arrange({ bounds.x + cross_offset, bounds.y + cursor, cross_size, main_size });
                    }
                    else {
                        live[i]->arrange({ bounds.x + cursor, bounds.y + cross_offset, main_size, cross_size });
                    }

                    cursor += main_size + m_spacing;
                }
            }

            void update(Input_State& input) override {
                for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
                    if (*it) it->get()->dispatch(input);
                }
            }

            void draw() const override {
                for (const Element& child : m_children) {
                    if (child) child.get()->render();
                }
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                if (m_axis == Axis::Horizontal) return desired();

                double main = 0.0;
                size_t count = 0;
                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    Size s = m_alignment == Align::Stretch ? child.get()->desired_for_width(width) : child.get()->desired();
                    main += s.height;
                    ++count;
                }
                if (count > 1) main += m_spacing * static_cast<double>(count - 1);

                return { fixed_width() >= 0.0 ? fixed_width() : width, fixed_height() >= 0.0 ? fixed_height() : main };
            }

        protected:
            Size measure() const override {
                double main = 0.0;
                double cross = 0.0;
                size_t count = 0;

                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    Size s = child.get()->desired();
                    main += main_of(s);
                    cross = std::max(cross, cross_of(s));
                    ++count;
                }

                if (count > 1) main += m_spacing * static_cast<double>(count - 1);
                return m_axis == Axis::Vertical ? Size{ cross, main } : Size{ main, cross };
            }

        protected:
            void set_children(std::vector<Element> children) {
                m_children = std::move(children);
                request_layout();
            }

            std::vector<Element> m_children;

            void collect_focusables(std::vector<Widget*>& out) override {
                for (const Element& child : m_children) {
                    if (child) child.get()->collect_focusables(out);
                }
            }

        private:
            double main_of(Size s) const { return m_axis == Axis::Vertical ? s.height : s.width; }
            double cross_of(Size s) const { return m_axis == Axis::Vertical ? s.width : s.height; }

            Axis m_axis;
            double m_spacing;
            Align m_alignment;
            Align m_justify;
        };

        // ForEach //
        class ForEach_View : public Stack {
        public:
            template <typename T, typename Builder>
            ForEach_View(State<std::vector<T>> source, Builder builder, const Stack_Props& props)
                : Stack(Axis::Vertical, props, std::vector<Element>{}) {

                auto rebuild = [this, source, builder]() {
                    std::vector<Element> children;
                    const auto& items = source.get();
                    children.reserve(items.size());
                    for (const auto& item : items) children.push_back(builder(item));
                    set_children(std::move(children));
                    };

                rebuild();
                watch(source, [rebuild](const std::vector<T>&) { rebuild(); });
            }
        };
        //////////////////////////////////////////////////

        class Cond : public Widget {
        public:
            Cond(Element when_true, Element when_false)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_when_true(std::move(when_true))
                , m_when_false(std::move(when_false)) {
            }

            void set_active(bool active) {
                if (active == m_active) return;
                if (current()) current().get()->go_inert();
                m_active = active;
                request_layout();
            }

            double flex_weight() const override {
                const Element& child = current();
                return child ? child.get()->flex_weight() : 0.0;
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (current()) current().get()->arrange(bounds);
            }

            void update(Input_State& input) override {
                if (current()) current().get()->dispatch(input);
            }

            void draw() const override {
                if (current()) current().get()->render();
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (current()) current().get()->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                return current() ? current().get()->desired_for_width(width) : Size{};
            }

        protected:
            Size measure() const override {
                return current() ? current().get()->desired() : Size{};
            }

        private:
            const Element& current() const { return m_active ? m_when_true : m_when_false; }

            Element m_when_true;
            Element m_when_false;
            bool m_active = false;
        };

        // Wrap //
        class Wrap : public Widget {
        public:
            Wrap(const Wrap_Props& props, std::vector<Element> children)
                : Widget(props.width, props.height, props.flex)
                , m_spacing_x(props.spacingX)
                , m_spacing_y(props.spacingY)
                , m_children(std::move(children)) {
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                double x = 0.0;
                double y = 0.0;
                double row_height = 0.0;
                bool any = false;

                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    Size s = child.get()->desired();

                    if (any && x + s.width > bounds.width) {
                        x = 0.0;
                        y += row_height + m_spacing_y;
                        row_height = 0.0;
                    }
                    child.get()->arrange({ bounds.x + x, bounds.y + y, s.width, s.height });
                    x += s.width + m_spacing_x;
                    row_height = std::max(row_height, s.height);
                    any = true;
                }
            }

            void update(Input_State& input) override {
                for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
                    if (*it) it->get()->dispatch(input);
                }
            }

            void draw() const override {
                for (const Element& child : m_children) {
                    if (child) child.get()->render();
                }
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                for (const Element& child : m_children) if (child) child.get()->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};

                double x = 0.0;
                double y = 0.0;
                double row_height = 0.0;
                bool any = false;

                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    Size s = child.get()->desired_for_width(width);

                    if (any && x + s.width > width) {
                        x = 0.0;
                        y += row_height + m_spacing_y;
                        row_height = 0.0;
                    }
                    x += s.width + m_spacing_x;
                    row_height = std::max(row_height, s.height);
                    any = true;
                }
                return { fixed_width() >= 0.0 ? fixed_width() : width,
                         fixed_height() >= 0.0 ? fixed_height() : y + row_height };
            }

        protected:
            Size measure() const override {
                double total = 0.0;
                double max_h = 0.0;
                size_t count = 0;
                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    Size s = child.get()->desired();
                    total += s.width + (count > 0 ? m_spacing_x : 0.0);
                    max_h = std::max(max_h, s.height);
                    ++count;
                }
                return { total, max_h };
            }

        private:
            double m_spacing_x;
            double m_spacing_y;
            std::vector<Element> m_children;
        };
        //////////////////////////////////////////////////

        // Leaves //
        class Label : public Widget {
        public:
            Label(Text text, const Label_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_color(props.color)
                , m_text_align(props.textAlign) {
                watch(m_text, [this](const std::string& value) { set_text(value); });
            }

            void set_text(std::string text) {
                if (m_run.set_text(std::move(text))) request_layout();
            }

            const std::string& text() const { return m_run.text(); }

            void draw() const override {
                const Size& extent = m_run.measured();
                double x = m_bounds.x;

                if (m_text_align == Align::Center) x += (m_bounds.width - extent.width) * 0.5;
                else if (m_text_align == Align::End) x += m_bounds.width - extent.width;

                m_run.draw({ x, m_bounds.y + (m_bounds.height - extent.height) * 0.5 }, m_color);
            }

        protected:
            Size measure() const override { return m_run.measured(); }

        private:
            Text m_text;
            detail::Text_Run m_run;
            Color m_color;
            Align m_text_align;
        };

        class Button : public Widget {
        public:
            event::Signal<> OnClick;
            event::Signal<bool> OnHoverChanged;

            Button(Text text, const Button_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_text_color(props.textColor)
                , m_background(props.backgroundColor)
                , m_hover(props.hoverColor)
                , m_pressed_color(props.pressedColor)
                , m_fade_time(props.fadeTime) {
                watch(m_text, [this](const std::string& value) { set_text(value); });
            }

            void set_text(std::string text) {
                if (m_run.set_text(std::move(text))) request_layout();
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    OnClick.Fire();
                    return;
                }

                bool over = !input.consumed && m_mesh.contains(input.mouse);

                if (over != m_hovered) {
                    m_hovered = over;
                    m_highlight.animate_to(over ? 1.0 : 0.0, ease(m_fade_time));
                    OnHoverChanged.Fire(over);
                }

                if (over && input.pressed) {
                    m_pressed = true;
                    input.consumed = true;
                }

                if (input.released) {
                    bool clicked = m_pressed && over;
                    m_pressed = false;
                    if (clicked) OnClick.Fire();
                }
            }

            void draw() const override {
                Color fill = m_pressed ? m_pressed_color : detail::mix(m_background, m_hover, m_highlight.get());
                mesh::draw(m_mesh, fill);

                const Size& extent = m_run.measured();
                m_run.draw({
                    m_bounds.x + (m_bounds.width - extent.width) * 0.5,
                    m_bounds.y + (m_bounds.height - extent.height) * 0.5
                    }, m_text_color);
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                const Size& extent = m_run.measured();
                return { extent.width + m_padding * 2.0, extent.height + m_padding * 2.0 };
            }

        private:
            Text m_text;
            detail::Text_Run m_run;
            double m_padding;
            double m_corner_radius;
            Color m_text_color;
            Color m_background;
            Color m_hover;
            Color m_pressed_color;
            double m_fade_time;
            Animated m_highlight{ 0.0 };
            bool m_hovered = false;
            bool m_pressed = false;
        };

        class Slider : public Widget {
        public:
            event::Signal<double> OnChanged;

            Slider(std::function<double()> getter, std::function<void(double)> setter,
                double min_value, double max_value, const Slider_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_get(std::move(getter))
                , m_set(std::move(setter))
                , m_min(min_value)
                , m_max(max_value)
                , m_track_height(props.trackHeight)
                , m_knob_radius(props.knobRadius)
                , m_track_color(props.trackColor)
                , m_fill_color(props.fillColor)
                , m_knob_color(props.knobColor) {
            }

            void refresh() { rebuild(); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                rebuild();
            }

            void update(Input_State& input) override {
                if (has_focus()) {
                    const double step = (m_max - m_min) / 20.0;
                    if (input.keys.left) { input.keys.left = false; m_set(std::clamp(m_get() - step, m_min, m_max)); OnChanged.Fire(m_get()); }
                    if (input.keys.right) { input.keys.right = false; m_set(std::clamp(m_get() + step, m_min, m_max)); OnChanged.Fire(m_get()); }
                }

                bool over = !input.consumed && m_mesh.contains(input.mouse);
                if (over && input.pressed) m_dragging = true;

                if (m_dragging) {
                    input.consumed = true;
                    if (input.down) apply_mouse(input.mouse.x);
                    else m_dragging = false;
                }
            }

            void draw() const override {
                mesh::draw(m_track, m_track_color);
                mesh::draw(m_fill, m_fill_color);
                mesh::draw(m_knob, m_knob_color);
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override { return { DEFAULT_WIDTH, m_knob_radius * 2.0 }; }

        private:
            static constexpr double DEFAULT_WIDTH = 320.0;

            double left() const { return m_bounds.x + m_knob_radius; }
            double right() const { return std::max(left(), m_bounds.x + m_bounds.width - m_knob_radius); }

            double fraction() const {
                if (m_max == m_min) return 0.0;
                return std::clamp((m_get() - m_min) / (m_max - m_min), 0.0, 1.0);
            }

            void apply_mouse(double x) {
                double span = right() - left();
                double t = span > 0.0 ? std::clamp((x - left()) / span, 0.0, 1.0) : 0.0;

                double before = m_get();
                m_set(m_min + t * (m_max - m_min));
                double after = m_get();

                if (after != before) OnChanged.Fire(after);
            }

            void rebuild() {
                double cy = m_bounds.y + m_bounds.height * 0.5;
                double knob_x = left() + (right() - left()) * fraction();
                double top = cy - m_track_height * 0.5;

                m_track = mesh::make_rect({ left(), top, right() - left(), m_track_height }, m_track_height * 0.5);
                m_fill = mesh::make_rect({ left(), top, knob_x - left(), m_track_height }, m_track_height * 0.5);
                m_knob = mesh::make_circle({ knob_x, cy }, m_knob_radius);
            }

            std::function<double()> m_get;
            std::function<void(double)> m_set;
            double m_min;
            double m_max;
            double m_track_height;
            double m_knob_radius;
            Color m_track_color;
            Color m_fill_color;
            Color m_knob_color;
            coordinate::tri_mesh m_track;
            coordinate::tri_mesh m_fill;
            coordinate::tri_mesh m_knob;
            bool m_dragging = false;
        };

        // Tree view //
        class Tree_View : public Widget {
        public:
            event::Signal<std::vector<int>> OnSelect;

            Tree_View(std::vector<Tree_Node_Data> nodes, const Tree_View_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_nodes(std::move(nodes))
                , m_font_size(props.fontSize)
                , m_row_padding(props.rowPadding)
                , m_indent_width(props.indentWidth)
                , m_arrow_size(props.arrowSize)
                , m_arrow_gap(props.arrowGap)
                , m_side_padding(props.sidePadding)
                , m_corner_radius(props.cornerRadius)
                , m_show_guides(props.showGuides)
                , m_scrollbar_width(props.scrollbarWidth)
                , m_scrollbar_min_width(props.scrollbarMinWidth)
                , m_scrollbar_fade_time(props.scrollbarFadeTime)
                , m_scrollbar_idle_time(props.scrollbarIdleTime)
                , m_wheel_step(props.wheelStep)
                , m_smooth_time(props.smoothTime)
                , m_hover_color(props.hoverColor)
                , m_selected_color(props.selectedColor)
                , m_selected_text_color(props.selectedTextColor)
                , m_arrow_color(props.arrowColor)
                , m_guide_color(props.guideColor) {
                watch(m_scroll, [](double) { request_layout(); });

                if (props.defaultExpanded) expand_all_recursive(m_nodes, {});
            }

            void set_expanded(const std::vector<int>& path, bool expanded) {
                if (expanded) m_expanded.insert(path);
                else m_expanded.erase(path);
                m_flat.clear();
                request_layout();
            }

            void expand_all() {
                m_expanded.clear();
                expand_all_recursive(m_nodes, {});
                m_flat.clear();
                request_layout();
            }

            void collapse_all() {
                m_expanded.clear();
                m_flat.clear();
                request_layout();
            }

            const std::vector<int>& selected_path() const { return m_selected_path; }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);

                m_flat.clear();
                flatten(m_nodes, {}, 0);

                m_row_height = m_font_size + m_row_padding * 2.0;
                m_content_height = static_cast<double>(m_flat.size()) * m_row_height;
                m_overflow = m_content_height > bounds.height + 0.5;

                const double limit = max_scroll();
                if (m_scroll.get() > limit) m_scroll.set(limit);
                m_target = std::clamp(m_target, 0.0, limit);

                const double content_width = bounds.width - (m_overflow ? m_scrollbar_width : 0.0);
                const double scroll = m_scroll.get();
                for (size_t i = 0; i < m_flat.size(); ++i) {
                    m_flat[i].bounds = {
                        bounds.x,
                        bounds.y + static_cast<double>(i) * m_row_height - scroll,
                        content_width,
                        m_row_height
                    };
                }
            }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);

                if (over && m_overflow && input.wheel != 0.0) {
                    scroll_to(m_target - input.wheel * m_wheel_step);
                    input.wheel = 0.0;
                }

                if (m_overflow) {
                    const double now = GetTime();
                    if (over) m_last_bar_activity = now;
                    const bool bar_visible = over || (now - m_last_bar_activity < m_scrollbar_idle_time);
                    m_bar_visibility.animate_to(bar_visible ? 1.0 : 0.0,
                        ease(bar_visible ? 0.15 : m_scrollbar_fade_time));
                }

                int hovered = -1;
                if (over) {
                    for (size_t i = 0; i < m_flat.size(); ++i) {
                        if (m_flat[i].bounds.contains(input.mouse)) { hovered = static_cast<int>(i); break; }
                    }
                }
                m_hovered = hovered;

                if (hovered < 0 || !input.pressed) return;
                input.consumed = true;

                const Row& row = m_flat[static_cast<size_t>(hovered)];

                if (row.has_children && in_arrow(input.mouse, row)) {
                    set_expanded(row.path, !row.expanded);
                    return;
                }

                m_selected_path = row.path;
                OnSelect.Fire(row.path);
            }

            void draw() const override {
                const double limit = max_scroll();
                const bool scrolling = m_overflow && limit > 0.0;
                const double view_top = m_bounds.y;
                const double view_bottom = m_bounds.y + m_bounds.height;

                detail::push_clip(m_mesh.bounds);

                if (m_show_guides) {
                    for (const Row& row : m_flat) {
                        if (row.bounds.y + row.bounds.height < view_top) continue;
                        if (row.bounds.y > view_bottom) break;
                        for (int d = 0; d < row.depth; ++d) {
                            const double gx = row.bounds.x + m_side_padding
                                + static_cast<double>(d) * m_indent_width
                                + m_arrow_size * 0.5;
                            mesh::draw(mesh::make_rect({ gx, row.bounds.y, 1.0, row.bounds.height }), m_guide_color);
                        }
                    }
                }

                for (size_t i = 0; i < m_flat.size(); ++i) {
                    const Row& row = m_flat[i];
                    if (row.bounds.y + row.bounds.height < view_top) continue;
                    if (row.bounds.y > view_bottom) break;

                    const bool selected = (row.path == m_selected_path);
                    if (selected) mesh::draw(mesh::make_rect(row.bounds, m_corner_radius), m_selected_color);
                    else if (static_cast<int>(i) == m_hovered) mesh::draw(mesh::make_rect(row.bounds, m_corner_radius), m_hover_color);

                    const double x = row.bounds.x + m_side_padding + static_cast<double>(row.depth) * m_indent_width;

                    if (row.has_children) {
                        const double cy = row.bounds.y + row.bounds.height * 0.5;
                        const double cx = x + m_arrow_size * 0.5;
                        coordinate::tri_mesh arrow;
                        if (row.expanded) {
                            arrow.tris.push_back({
                                { cx - m_arrow_size * 0.6, cy - m_arrow_size * 0.3 },
                                { cx + m_arrow_size * 0.6, cy - m_arrow_size * 0.3 },
                                { cx, cy + m_arrow_size * 0.5 } });
                        }
                        else {
                            arrow.tris.push_back({
                                { cx - m_arrow_size * 0.3, cy - m_arrow_size * 0.6 },
                                { cx - m_arrow_size * 0.3, cy + m_arrow_size * 0.6 },
                                { cx + m_arrow_size * 0.5, cy } });
                        }
                        arrow.update_bounds();
                        mesh::draw(arrow, m_arrow_color);
                    }

                    const Color color = selected ? m_selected_text_color : row.node->color;
                    font.draw(row.node->label.get(),
                        { x + m_arrow_size + m_arrow_gap,
                          row.bounds.y + (row.bounds.height - m_font_size) * 0.5 },
                        m_font_size, color);
                }

                detail::pop_clip();

                if (scrolling) {
                    const double vis = m_bar_visibility.get();
                    const double w = m_scrollbar_min_width + (m_scrollbar_width - m_scrollbar_min_width) * vis;
                    const double alpha = 0.4 + 0.6 * vis;

                    Color track = { 0, 0, 0, 60 };
                    track.a = static_cast<unsigned char>(track.a * alpha);
                    Color thumb = { 255, 255, 255, 110 };
                    thumb.a = static_cast<unsigned char>(thumb.a * alpha);

                    const double track_x = m_bounds.x + m_bounds.width - w;
                    const double thumb_h = std::max(30.0, m_bounds.height * m_bounds.height / m_content_height);
                    const double t = m_scroll.get() / limit;
                    const double thumb_y = m_bounds.y + t * (m_bounds.height - thumb_h);

                    if (track.a > 0) {
                        mesh::draw(mesh::make_rect({ track_x, m_bounds.y, w, m_bounds.height }, w * 0.5), track);
                    }
                    mesh::draw(mesh::make_rect({ track_x, thumb_y, w, thumb_h }, w * 0.5), thumb);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                if (m_flat.empty()) flatten(m_nodes, {}, 0);

                double max_width = 0.0;
                for (const Row& row : m_flat) {
                    const double w = m_side_padding * 2.0
                        + static_cast<double>(row.depth) * m_indent_width
                        + m_arrow_size + m_arrow_gap
                        + font.measure(row.node->label.get(), m_font_size).width;
                    max_width = std::max(max_width, w);
                }
                const double row_height = m_font_size + m_row_padding * 2.0;
                return { max_width, static_cast<double>(m_flat.size()) * row_height };
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                if (m_flat.empty()) flatten(m_nodes, {}, 0);
                const double row_height = m_font_size + m_row_padding * 2.0;
                const double height = static_cast<double>(m_flat.size()) * row_height;
                return { width, fixed_height() >= 0.0 ? fixed_height() : height };
            }

        private:
            struct Row {
                const Tree_Node_Data* node = nullptr;
                std::vector<int> path;
                int depth = 0;
                bool has_children = false;
                bool expanded = false;
                coordinate::rect bounds;
            };

            void expand_all_recursive(const std::vector<Tree_Node_Data>& nodes, const std::vector<int>& parent_path) {
                for (size_t i = 0; i < nodes.size(); ++i) {
                    std::vector<int> path = parent_path;
                    path.push_back(static_cast<int>(i));
                    if (!nodes[i].children.empty()) {
                        m_expanded.insert(path);
                        expand_all_recursive(nodes[i].children, path);
                    }
                }
            }

            void flatten(const std::vector<Tree_Node_Data>& nodes,
                const std::vector<int>& parent_path, int depth) const {
                for (size_t i = 0; i < nodes.size(); ++i) {
                    std::vector<int> path = parent_path;
                    path.push_back(static_cast<int>(i));

                    Row row;
                    row.node = &nodes[i];
                    row.path = path;
                    row.depth = depth;
                    row.has_children = !nodes[i].children.empty();
                    row.expanded = m_expanded.count(path) > 0;
                    m_flat.push_back(std::move(row));

                    if (m_flat.back().expanded && m_flat.back().has_children) {
                        flatten(nodes[i].children, path, depth + 1);
                    }
                }
            }

            bool in_arrow(coordinate::pos mouse, const Row& row) const {
                const double x = row.bounds.x + m_side_padding + static_cast<double>(row.depth) * m_indent_width;
                return mouse.x >= x - 4.0 && mouse.x <= x + m_arrow_size + 4.0;
            }

            double max_scroll() const { return std::max(0.0, m_content_height - m_bounds.height); }

            void scroll_to(double offset) {
                m_target = std::clamp(offset, 0.0, max_scroll());
                m_scroll.animate_to(m_target, ease(m_smooth_time));
            }

            std::vector<Tree_Node_Data> m_nodes;
            std::set<std::vector<int>> m_expanded;
            mutable std::vector<Row> m_flat;
            std::vector<int> m_selected_path;

            double m_font_size;
            double m_row_padding;
            double m_indent_width;
            double m_arrow_size;
            double m_arrow_gap;
            double m_side_padding;
            double m_corner_radius;
            bool m_show_guides;
            double m_scrollbar_width;
            double m_scrollbar_min_width;
            double m_scrollbar_fade_time;
            double m_scrollbar_idle_time;
            double m_wheel_step;
            double m_smooth_time;
            Color m_hover_color;
            Color m_selected_color;
            Color m_selected_text_color;
            Color m_arrow_color;
            Color m_guide_color;

            Animated m_scroll{ 0.0 };
            Animated m_bar_visibility{ 0.0 };
            double m_target = 0.0;
            double m_row_height = 0.0;
            double m_content_height = 0.0;
            double m_last_bar_activity = 0.0;
            bool m_overflow = false;
            int m_hovered = -1;
        };
        //////////////////////////////////////////////////

        class Checkbox : public Widget {
        public:
            event::Signal<bool> OnChanged;

            Checkbox(std::function<bool()> getter, std::function<void(bool)> setter,
                Text label, const Checkbox_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_get(std::move(getter))
                , m_set(std::move(setter))
                , m_text(std::move(label))
                , m_run(m_text.get(), props.fontSize)
                , m_text_color(props.textColor)
                , m_box_color(props.boxColor)
                , m_check_color(props.checkColor) {
                watch(m_text, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                double box = m_run.size();
                double top = bounds.y + (bounds.height - box) * 0.5;
                m_box = mesh::make_rect({ bounds.x, top, box, box }, box * 0.2);
                m_check = mesh::make_rect({ bounds.x + box * 0.2, top + box * 0.2, box * 0.6, box * 0.6 }, box * 0.1);
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    m_set(!m_get());
                    OnChanged.Fire(m_get());
                    return;
                }

                if (input.consumed || !input.pressed || !m_mesh.contains(input.mouse)) return;

                input.consumed = true;
                m_set(!m_get());
                OnChanged.Fire(m_get());
            }

            void draw() const override {
                mesh::draw(m_box, m_box_color);
                if (m_get()) mesh::draw(m_check, m_check_color);

                const Size& extent = m_run.measured();
                double box = m_run.size();
                m_run.draw({
                    m_bounds.x + box + box * GAP_RATIO,
                    m_bounds.y + (m_bounds.height - extent.height) * 0.5
                    }, m_text_color);
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                double box = m_run.size();
                double text_width = m_run.text().empty() ? 0.0 : box * GAP_RATIO + m_run.measured().width;
                return { box + text_width, box };
            }

        private:
            static constexpr double GAP_RATIO = 0.4;

            std::function<bool()> m_get;
            std::function<void(bool)> m_set;
            Text m_text;
            detail::Text_Run m_run;
            Color m_text_color;
            Color m_box_color;
            Color m_check_color;
            coordinate::tri_mesh m_box;
            coordinate::tri_mesh m_check;
        };

        class Spacer : public Widget {
        public:
            explicit Spacer(const Spacer_Props& props)
                : Widget(props.width, props.height, props.flex) {
            }

            void draw() const override {}

        protected:
            Size measure() const override { return {}; }
        };

        // Shape (builder) //
        class Shape : public Widget {
        public:
            event::Signal<> OnClick;
            event::Signal<bool> OnHoverChanged;

            Shape(const Shape_Props& props)
                : Widget(props.width, props.height, props.flex) {
            }

            void set_measure(std::function<Size()> fn) { m_measure_fn = std::move(fn); }
            void set_update(std::function<void(Input_State&)> fn) { m_update_fn = std::move(fn); }
            void set_draw(std::function<void(const coordinate::rect&)> fn) { m_draw_fn = std::move(fn); }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);
                if (over != m_hovered) {
                    m_hovered = over;
                    OnHoverChanged.Fire(over);
                }

                if (over && input.pressed) {
                    input.consumed = true;
                    m_pressed = true;
                }
                if (input.released) {
                    const bool clicked = m_pressed && over;
                    m_pressed = false;
                    if (clicked) OnClick.Fire();
                }

                if (m_update_fn) m_update_fn(input);
            }

            void draw() const override {
                if (m_draw_fn) m_draw_fn(m_bounds);
            }

        protected:
            Size measure() const override {
                return m_measure_fn ? m_measure_fn() : Size{};
            }

        private:
            std::function<Size()> m_measure_fn;
            std::function<void(Input_State&)> m_update_fn;
            std::function<void(const coordinate::rect&)> m_draw_fn;
            bool m_hovered = false;
            bool m_pressed = false;
        };
        //////////////////////////////////////////////////

        // Text display //
        class Text_Display : public Widget {
        public:
            Text_Display(Text text, const Text_Display_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_font_size(props.fontSize)
                , m_line_height(props.lineHeight)
                , m_max_width(props.maxWidth)
                , m_color(props.color)
                , m_text_align(props.textAlign) {
                watch(m_text, [](const std::string&) { request_layout(); });
            }

            const std::string& text() const { return m_text.get(); }

            size_t line_count() const { return m_draw_cache.get(text(), m_bounds.width, m_font_size).size(); }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                return { width, fixed_height() >= 0.0 ? fixed_height() : height_at(width) };
            }

            void draw() const override {
                const auto& lines = m_draw_cache.get(text(), m_bounds.width, m_font_size);
                const double pitch = m_font_size * m_line_height;

                for (size_t i = 0; i < lines.size(); ++i) {
                    const detail::Text_Line& line = lines[i];
                    std::string piece = detail::trim_right(text().substr(line.start, line.end - line.start));
                    if (piece.empty()) continue;

                    double x = m_bounds.x;
                    if (m_text_align == Align::Center || m_text_align == Align::End) {
                        double width = font.measure(piece, m_font_size).width;
                        x += m_text_align == Align::Center ? (m_bounds.width - width) * 0.5 : m_bounds.width - width;
                    }

                    double y = m_bounds.y + static_cast<double>(i) * pitch + (pitch - m_font_size) * 0.5;
                    font.draw(piece, { x, y }, m_font_size, m_color);
                }
            }

        protected:
            Size measure() const override {
                double natural = 0.0;
                for (const auto& line : m_measure_cache.get(text(), std::numeric_limits<double>::infinity(), m_font_size)) {
                    natural = std::max(natural, line.width);
                }

                double width = m_max_width >= 0.0 ? std::min(natural, m_max_width) : natural;
                if (fixed_width() >= 0.0) width = fixed_width();
                return { width, height_at(width) };
            }

        private:
            double height_at(double width) const {
                return static_cast<double>(m_measure_cache.get(text(), width, m_font_size).size()) * m_font_size * m_line_height;
            }

            Text m_text;
            double m_font_size;
            double m_line_height;
            double m_max_width;
            Color m_color;
            Align m_text_align;
            detail::Wrap_Cache m_draw_cache;
            detail::Wrap_Cache m_measure_cache;
        };
        //////////////////////////////////////////////////

        // Text line //
        class Text_Line : public Widget {
        public:
            Text_Line(Text text, const Text_Line_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_font_size(props.fontSize)
                , m_color(props.color)
                , m_align(props.textAlign)
                , m_padding(props.padding)
                , m_scrollbar_height(props.scrollbarHeight)
                , m_scrollbar_min_height(props.scrollbarMinHeight)
                , m_scrollbar_fade_time(props.scrollbarFadeTime)
                , m_scrollbar_idle_time(props.scrollbarIdleTime)
                , m_wheel_step(props.wheelStep)
                , m_smooth_time(props.smoothTime)
                , m_thumb_color(props.scrollbarThumbColor)
                , m_track_color(props.scrollbarTrackColor) {
                watch(m_text, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
            }

            const std::string& text() const { return m_run.text(); }

            void scroll_to(double offset) {
                m_target = std::clamp(offset, 0.0, max_scroll());
                m_scroll.animate_to(m_target, ease(m_smooth_time));
            }

            void scroll_to_start() { scroll_to(0.0); }
            void scroll_to_end() { scroll_to(max_scroll()); }
            double scroll_offset() const { return m_scroll.get(); }
            bool overflowing() const { return max_scroll() > 0.0; }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_text_width = font.pen(m_run.text(), m_font_size);
                m_view_width = std::max(0.0, bounds.width - m_padding * 2.0);

                const double limit = max_scroll();
                if (m_scroll.get() > limit) m_scroll.set(limit);
                m_target = std::clamp(m_target, 0.0, limit);
            }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);
                if (!over || max_scroll() <= 0.0 || input.wheel == 0.0) {
                    if (max_scroll() > 0.0) {
                        const double now = GetTime();
                        const bool bar_visible = over || (now - m_last_bar_activity < m_scrollbar_idle_time);
                        m_bar_visibility.animate_to(bar_visible ? 1.0 : 0.0,
                            ease(bar_visible ? 0.15 : m_scrollbar_fade_time));
                    }
                    return;
                }

                scroll_to(m_target - input.wheel * m_wheel_step);
                input.wheel = 0.0;

                const double now = GetTime();
                m_last_bar_activity = now;
                m_bar_visibility.animate_to(1.0, ease(0.15));
            }

            void draw() const override {
                if (m_run.text().empty()) return;

                const double offset = m_scroll.get();
                const double max = max_scroll();

                coordinate::rect inner{
                    m_bounds.x + m_padding,
                    m_bounds.y,
                    m_view_width,
                    m_bounds.height
                };

                detail::push_clip(inner);

                double x = inner.x - offset;
                if (max <= 0.0) {
                    double free = m_view_width - m_text_width;
                    if (m_align == Align::Center) x += free * 0.5;
                    else if (m_align == Align::End) x += free;
                }

                double y = m_bounds.y + (m_bounds.height - m_font_size) * 0.5;
                m_run.draw({ x, y }, m_color);

                detail::pop_clip();

                if (m_scrollbar_height > 0.0 && max > 0.0) {
                    const double vis = m_bar_visibility.get();
                    const double h = m_scrollbar_min_height + (m_scrollbar_height - m_scrollbar_min_height) * vis;
                    const double alpha = 0.4 + 0.6 * vis;

                    const double track_x = m_bounds.x + m_padding;
                    const double track_y = m_bounds.y + m_bounds.height - h;
                    const double track_w = m_view_width;

                    const double thumb_w = std::max(30.0, track_w * m_view_width / m_text_width);
                    const double t = offset / max;
                    const double thumb_x = track_x + t * (track_w - thumb_w);

                    Color track = m_track_color;
                    track.a = static_cast<unsigned char>(track.a * alpha);
                    Color thumb = m_thumb_color;
                    thumb.a = static_cast<unsigned char>(thumb.a * alpha);

                    if (track.a > 0) {
                        mesh::draw(mesh::make_rect({ track_x, track_y, track_w, h }, h * 0.5), track);
                    }
                    mesh::draw(mesh::make_rect({ thumb_x, track_y, thumb_w, h }, h * 0.5), thumb);
                }
            }

        protected:
            Size measure() const override {
                return { font.pen(m_run.text(), m_font_size) + m_padding * 2.0, m_font_size };
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                return { width, fixed_height() >= 0.0 ? fixed_height() : m_font_size };
            }

        private:
            double max_scroll() const {
                return std::max(0.0, m_text_width - m_view_width);
            }

            Text m_text;
            detail::Text_Run m_run;
            double m_font_size;
            Color m_color;
            Align m_align;
            double m_padding;
            double m_scrollbar_height;
            double m_scrollbar_min_height;
            double m_scrollbar_fade_time;
            double m_scrollbar_idle_time;
            double m_wheel_step;
            double m_smooth_time;
            Color m_thumb_color;
            Color m_track_color;
            Animated m_scroll{ 0.0 };
            Animated m_bar_visibility{ 0.0 };
            double m_target = 0.0;
            double m_text_width = 0.0;
            double m_view_width = 0.0;
            double m_last_bar_activity = 0.0;
        };
        //////////////////////////////////////////////////

        // Text field //
        class Text_Field : public Widget {
        public:
            event::Signal<std::string> OnChanged;
            event::Signal<std::string> OnSubmit;
            event::Signal<bool> OnFocusChanged;

            Text_Field(State<std::string> state, const Text_Field_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_state(std::move(state))
                , m_placeholder(props.placeholder)
                , m_font_size(props.fontSize)
                , m_line_height(props.lineHeight)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_min_lines(props.minLines)
                , m_max_length(props.maxLength)
                , m_text_color(props.textColor)
                , m_placeholder_color(props.placeholderColor)
                , m_background(props.backgroundColor)
                , m_focus_color(props.focusColor)
                , m_caret_color(props.caretColor)
                , m_selection_color(props.selectionColor) {
                watch(m_state, [this](const std::string&) { clamp_selection(); });
            }

            const std::string& text() const { return m_state.get(); }
            bool focused() const { return m_focused; }

            void on_focus_changed(bool focused) override {
                m_focused = focused;
                m_selecting = false;
                m_anchor = m_cursor;
                m_goal_valid = false;
                restart_blink();
                OnFocusChanged.Fire(focused);
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                return { width, fixed_height() >= 0.0 ? fixed_height() : height_at(width) };
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);
                ensure_caret_visible();
            }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);

                if (over && input.pressed) {
                    focus_widget(this);
                    input.consumed = true;
                    m_cursor = pointer_index(input.mouse);
                    if (!input.keys.shift) m_anchor = m_cursor;
                    m_goal_valid = false;
                    m_selecting = true;
                    restart_blink();
                }

                if (m_selecting) {
                    input.consumed = true;
                    if (input.down) m_cursor = pointer_index(input.mouse);
                    else m_selecting = false;
                    ensure_caret_visible();
                }

                if (over && input.wheel != 0.0 && max_scroll() > 0.0) {
                    m_scroll_y = std::clamp(m_scroll_y - input.wheel * pitch() * WHEEL_LINES, 0.0, max_scroll());
                    input.wheel = 0.0;
                }

                if (m_focused) handle_keys(input);
            }

            void draw() const override {
                if (m_focused) {
                    coordinate::rect ring{
                        m_bounds.x - FOCUS_RING,
                        m_bounds.y - FOCUS_RING,
                        m_bounds.width + FOCUS_RING * 2.0,
                        m_bounds.height + FOCUS_RING * 2.0
                    };
                    mesh::draw(mesh::make_rect(ring, m_corner_radius + FOCUS_RING), m_focus_color);
                }
                mesh::draw(m_mesh, m_background);

                coordinate::rect inner{
                    m_bounds.x + m_padding,
                    m_bounds.y + m_padding,
                    std::max(0.0, m_bounds.width - m_padding * 2.0),
                    std::max(0.0, m_bounds.height - m_padding * 2.0)
                };
                const double step = pitch();
                const double origin_y = inner.y - m_scroll_y;
                const double glyph_offset = (step - m_font_size) * 0.5;
                const auto& rows = lines();

                detail::push_clip(inner);

                if (text().empty()) {
                    font.draw(m_placeholder.get(), { inner.x, origin_y + glyph_offset }, m_font_size, m_placeholder_color);
                }
                else {
                    size_t first = static_cast<size_t>(std::max(0.0, std::floor(m_scroll_y / step)));
                    size_t last = std::min(rows.size(), static_cast<size_t>(std::ceil((m_scroll_y + inner.height) / step)) + 1);

                    for (size_t row = first; row < last; ++row) {
                        const detail::Text_Line& line = rows[row];
                        double y = origin_y + static_cast<double>(row) * step;

                        if (m_focused && has_selection()) {
                            size_t a = std::max(selection_start(), line.start);
                            size_t b = std::min(selection_end(), line.end);
                            if (a < b) {
                                double x1 = x_at(row, a);
                                double x2 = x_at(row, b);
                                mesh::draw(mesh::make_rect({ inner.x + x1, y, x2 - x1, step }), m_selection_color);
                            }
                        }

                        font.draw(text().substr(line.start, line.end - line.start), { inner.x, y + glyph_offset },
                            m_font_size, m_text_color);
                    }
                }

                if (m_focused && caret_visible()) {
                    size_t row = line_of(m_cursor);
                    double cx = inner.x + x_at(row, m_cursor);
                    double cy = origin_y + static_cast<double>(row) * step + glyph_offset;
                    mesh::draw(mesh::make_rect({ cx, cy, CARET_WIDTH, m_font_size }), m_caret_color);
                }

                detail::pop_clip();
            }

            bool focusable() const override { return true; }
            bool has_custom_focus_ring() const override { return true; }

        protected:
            Size measure() const override {
                double width = fixed_width() >= 0.0 ? fixed_width() : DEFAULT_WIDTH;
                return { width, height_at(width) };
            }

        private:
            static constexpr double DEFAULT_WIDTH = 640.0;
            static constexpr double CARET_WIDTH = 3.0;
            static constexpr double FOCUS_RING = 3.0;
            static constexpr double BLINK_PERIOD = 1.06;
            static constexpr double WHEEL_LINES = 3.0;

            double pitch() const { return m_font_size * m_line_height; }
            double wrap_width_for(double outer) const { return std::max(1.0, outer - m_padding * 2.0 - CARET_WIDTH); }
            double inner_height() const { return std::max(0.0, m_bounds.height - m_padding * 2.0); }

            double height_at(double outer_width) const {
                size_t rows = m_measure_cache.get(text(), wrap_width_for(outer_width), m_font_size).size();
                rows = std::max(rows, static_cast<size_t>(std::max(m_min_lines, 1)));
                return static_cast<double>(rows) * pitch() + m_padding * 2.0;
            }

            const std::vector<detail::Text_Line>& lines() const {
                return m_cache.get(text(), wrap_width_for(m_bounds.width), m_font_size);
            }

            double max_scroll() const {
                return std::max(0.0, static_cast<double>(lines().size()) * pitch() - inner_height());
            }

            size_t selection_start() const { return std::min(m_anchor, m_cursor); }
            size_t selection_end() const { return std::max(m_anchor, m_cursor); }
            bool has_selection() const { return m_anchor != m_cursor; }

            size_t line_of(size_t index) const {
                const auto& rows = lines();
                size_t found = 0;
                for (size_t i = 0; i < rows.size(); ++i) {
                    if (rows[i].start <= index) found = i;
                    else break;
                }
                return found;
            }

            size_t line_limit(size_t row) const {
                const auto& rows = lines();
                const detail::Text_Line& line = rows[row];
                if (row + 1 < rows.size() && rows[row + 1].start == line.end && line.end > line.start) {
                    return detail::utf8_prev(text(), line.end);
                }
                return line.end;
            }

            double x_at(size_t row, size_t index) const {
                const detail::Text_Line& line = lines()[row];
                index = std::clamp(index, line.start, line.end);
                return font.pen(text().substr(line.start, index - line.start), m_font_size);
            }

            size_t index_in_line(size_t row, double x) const {
                const detail::Text_Line& line = lines()[row];
                const size_t limit = line_limit(row);

                size_t best = line.start;
                double best_distance = std::abs(x);
                for (size_t i = line.start; i < limit;) {
                    i = detail::utf8_next(text(), i);
                    double w = x_at(row, i);
                    double distance = std::abs(x - w);
                    if (distance < best_distance) {
                        best = i;
                        best_distance = distance;
                    }
                    else if (w > x) {
                        break;
                    }
                }
                return best;
            }

            size_t pointer_index(coordinate::pos mouse) const {
                double x = mouse.x - (m_bounds.x + m_padding);
                double y = mouse.y - (m_bounds.y + m_padding) + m_scroll_y;
                long row = static_cast<long>(std::floor(y / pitch()));
                row = std::clamp(row, 0L, static_cast<long>(lines().size()) - 1);
                return index_in_line(static_cast<size_t>(row), x);
            }

            void clamp_selection() {
                m_cursor = std::min(m_cursor, text().size());
                m_anchor = std::min(m_anchor, text().size());
            }

            void ensure_caret_visible() {
                double top = static_cast<double>(line_of(m_cursor)) * pitch();
                double view = inner_height();

                if (top < m_scroll_y) m_scroll_y = top;
                if (top + pitch() > m_scroll_y + view) m_scroll_y = top + pitch() - view;
                m_scroll_y = std::clamp(m_scroll_y, 0.0, max_scroll());
            }

            void restart_blink() { m_blink_start = GetTime(); }

            bool caret_visible() const {
                return std::fmod(GetTime() - m_blink_start, BLINK_PERIOD) < BLINK_PERIOD * 0.5;
            }

            void move_cursor(size_t position, bool extend, bool keep_goal = false) {
                m_cursor = position;
                if (!extend) m_anchor = position;
                if (!keep_goal) m_goal_valid = false;
                restart_blink();
            }

            void move_vertical(int direction, bool extend) {
                const auto& rows = lines();
                const size_t row = line_of(m_cursor);
                if (!m_goal_valid) {
                    m_goal_x = x_at(row, m_cursor);
                    m_goal_valid = true;
                }

                const long target = static_cast<long>(row) + direction;
                if (target < 0) move_cursor(0, extend, true);
                else if (target >= static_cast<long>(rows.size())) move_cursor(text().size(), extend, true);
                else move_cursor(index_in_line(static_cast<size_t>(target), m_goal_x), extend, true);
            }

            void commit(std::string updated) {
                m_state.set(std::move(updated));
                OnChanged.Fire(text());
                restart_blink();
            }

            void replace_range(size_t from, size_t to, const std::string& with) {
                std::string updated = text();
                updated.replace(from, to - from, with);
                m_cursor = m_anchor = from + with.size();
                m_goal_valid = false;
                commit(std::move(updated));
            }

            void erase_selection() {
                if (has_selection()) replace_range(selection_start(), selection_end(), "");
            }

            void insert(const std::string& raw) {
                std::string filtered;
                filtered.reserve(raw.size());
                for (char c : raw) {
                    unsigned char u = static_cast<unsigned char>(c);
                    if (c == '\n' || (u >= 32 && u != 127)) filtered.push_back(c);
                }
                if (filtered.empty()) return;

                size_t from = selection_start();
                size_t to = selection_end();

                if (m_max_length > 0) {
                    size_t limit = static_cast<size_t>(m_max_length);
                    size_t used = detail::utf8_count(text()) - detail::utf8_count(text().substr(from, to - from));
                    size_t room = used < limit ? limit - used : 0;

                    size_t end = 0;
                    size_t taken = 0;
                    while (end < filtered.size() && taken < room) {
                        end = detail::utf8_next(filtered, end);
                        ++taken;
                    }
                    filtered.resize(end);
                    if (filtered.empty()) return;
                }

                replace_range(from, to, filtered);
            }

            void handle_keys(Input_State& input) {
                const Key_State& k = input.keys;

                if (k.escape) {
                    input.suppressed_keys.push_back(KEY_ESCAPE);
                    clear_focus();
                    return;
                }

                if (k.select_all) {
                    m_anchor = 0;
                    m_cursor = text().size();
                    m_goal_valid = false;
                    restart_blink();
                }

                if ((k.copy || k.cut) && has_selection()) {
                    SetClipboardText(text().substr(selection_start(), selection_end() - selection_start()).c_str());
                    if (k.cut) erase_selection();
                }

                if (k.paste) {
                    const char* clip = GetClipboardText();
                    if (clip) insert(clip);
                }

                if (!input.typed.empty()) insert(input.typed);

                if (k.enter) {
                    if (k.ctrl) OnSubmit.Fire(text());
                    else insert("\n");
                }

                if (k.backspace) {
                    if (has_selection()) erase_selection();
                    else if (m_cursor > 0) replace_range(detail::utf8_prev(text(), m_cursor), m_cursor, "");
                }

                if (k.del) {
                    if (has_selection()) erase_selection();
                    else if (m_cursor < text().size()) replace_range(m_cursor, detail::utf8_next(text(), m_cursor), "");
                }

                if (k.left) {
                    if (has_selection() && !k.shift) move_cursor(selection_start(), false);
                    else move_cursor(detail::utf8_prev(text(), m_cursor), k.shift);
                }

                if (k.right) {
                    if (has_selection() && !k.shift) move_cursor(selection_end(), false);
                    else move_cursor(detail::utf8_next(text(), m_cursor), k.shift);
                }

                if (k.up) move_vertical(-1, k.shift);
                if (k.down) move_vertical(1, k.shift);

                if (k.home) move_cursor(k.ctrl ? 0 : lines()[line_of(m_cursor)].start, k.shift);
                if (k.end) move_cursor(k.ctrl ? text().size() : line_limit(line_of(m_cursor)), k.shift);

                ensure_caret_visible();
            }

            State<std::string> m_state;
            Text m_placeholder;
            double m_font_size;
            double m_line_height;
            double m_padding;
            double m_corner_radius;
            int m_min_lines;
            int m_max_length;
            Color m_text_color;
            Color m_placeholder_color;
            Color m_background;
            Color m_focus_color;
            Color m_caret_color;
            Color m_selection_color;
            detail::Wrap_Cache m_cache;
            detail::Wrap_Cache m_measure_cache;
            size_t m_cursor = 0;
            size_t m_anchor = 0;
            double m_scroll_y = 0.0;
            double m_goal_x = 0.0;
            double m_blink_start = 0.0;
            bool m_goal_valid = false;
            bool m_focused = false;
            bool m_selecting = false;
        };
        //////////////////////////////////////////////////

        // Divider //
        class Divider : public Widget {
        public:
            explicit Divider(const Divider_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_axis(props.axis)
                , m_thickness(props.thickness)
                , m_color(props.color) {
            }

            bool fills_cross_axis(Axis stack_axis) const override {
                return (m_axis == Axis::Horizontal) == (stack_axis == Axis::Vertical);
            }

            void draw() const override {
                coordinate::rect line = m_axis == Axis::Horizontal
                    ? coordinate::rect{ m_bounds.x, m_bounds.y + (m_bounds.height - m_thickness) * 0.5, m_bounds.width, m_thickness }
                : coordinate::rect{ m_bounds.x + (m_bounds.width - m_thickness) * 0.5, m_bounds.y, m_thickness, m_bounds.height };
                mesh::draw(mesh::make_rect(line), m_color);
            }

        protected:
            Size measure() const override {
                return m_axis == Axis::Horizontal ? Size{ 0.0, m_thickness } : Size{ m_thickness, 0.0 };
            }

        private:
            Axis m_axis;
            double m_thickness;
            Color m_color;
        };
        //////////////////////////////////////////////////

        // Toggle //
        class Toggle : public Widget {
        public:
            event::Signal<bool> OnChanged;

            Toggle(std::function<bool()> getter, std::function<void(bool)> setter, const Toggle_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_get(std::move(getter))
                , m_set(std::move(setter))
                , m_track_width(props.trackWidth)
                , m_track_height(props.trackHeight)
                , m_animate_time(props.animateTime)
                , m_off_color(props.offColor)
                , m_on_color(props.onColor)
                , m_knob_color(props.knobColor)
                , m_position(m_get() ? 1.0 : 0.0) {
            }

            void sync() { m_position.animate_to(m_get() ? 1.0 : 0.0, ease(m_animate_time)); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_track = { bounds.x, bounds.y + (bounds.height - m_track_height) * 0.5, m_track_width, m_track_height };
                m_mesh = mesh::make_rect(m_track, m_track_height * 0.5);
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    m_set(!m_get());
                    OnChanged.Fire(m_get());
                    return;
                }

                if (input.consumed || !input.pressed || !m_mesh.contains(input.mouse)) return;

                input.consumed = true;
                m_set(!m_get());
                OnChanged.Fire(m_get());
            }

            void draw() const override {
                const double position = m_position.get();
                mesh::draw(m_mesh, detail::mix(m_off_color, m_on_color, position));

                const double radius = m_track_height * 0.5 - KNOB_INSET;
                const double travel = m_track_width - m_track_height;
                const double cx = m_track.x + m_track_height * 0.5 + travel * position;
                mesh::draw(mesh::make_circle({ cx, m_track.y + m_track_height * 0.5 }, radius), m_knob_color);
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override { return { m_track_width, m_track_height }; }

        private:
            static constexpr double KNOB_INSET = 5.0;

            std::function<bool()> m_get;
            std::function<void(bool)> m_set;
            double m_track_width;
            double m_track_height;
            double m_animate_time;
            Color m_off_color;
            Color m_on_color;
            Color m_knob_color;
            Animated m_position;
            coordinate::rect m_track;
        };
        //////////////////////////////////////////////////

        // Radio //
        class Radio : public Widget {
        public:
            event::Signal<> OnClick;

            Radio(std::function<bool()> selected, std::function<void()> select, Text label, const Radio_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_selected(std::move(selected))
                , m_select(std::move(select))
                , m_text(std::move(label))
                , m_run(m_text.get(), props.fontSize)
                , m_text_color(props.textColor)
                , m_ring_color(props.ringColor)
                , m_dot_color(props.dotColor) {
                watch(m_text, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    m_select();
                    OnClick.Fire();
                    return;
                }

                if (input.consumed || !input.pressed || !m_mesh.contains(input.mouse)) return;

                input.consumed = true;
                m_select();
                OnClick.Fire();
            }

            void draw() const override {
                const double size = m_run.size();
                const coordinate::pos center{ m_bounds.x + size * 0.5, m_bounds.y + m_bounds.height * 0.5 };

                mesh::draw(mesh::make_circle(center, size * 0.5), m_ring_color);
                if (m_selected()) mesh::draw(mesh::make_circle(center, size * DOT_RATIO), m_dot_color);

                if (m_run.text().empty()) return;
                const Size& extent = m_run.measured();
                m_run.draw({ m_bounds.x + size + size * GAP_RATIO, m_bounds.y + (m_bounds.height - extent.height) * 0.5 }, m_text_color);
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                const double size = m_run.size();
                const double text_width = m_run.text().empty() ? 0.0 : size * GAP_RATIO + m_run.measured().width;
                return { size + text_width, size };
            }

        private:
            static constexpr double GAP_RATIO = 0.4;
            static constexpr double DOT_RATIO = 0.28;

            std::function<bool()> m_selected;
            std::function<void()> m_select;
            Text m_text;
            detail::Text_Run m_run;
            Color m_text_color;
            Color m_ring_color;
            Color m_dot_color;
        };
        //////////////////////////////////////////////////

        // Switch //
        class Switch : public Widget {
        public:
            Switch(std::vector<Element> pages, double flex)
                : Widget(AUTO_SIZE, AUTO_SIZE, flex)
                , m_pages(std::move(pages)) {
            }

            void set_index(int index) {
                if (index == m_index) return;
                if (current()) current().get()->go_inert();
                m_index = index;
                request_layout();
            }

            Size desired_for_width(double width) const override {
                return current() ? current().get()->desired_for_width(width) : Size{};
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (current()) current().get()->arrange(bounds);
            }

            void update(Input_State& input) override {
                if (current()) current().get()->dispatch(input);
            }

            void draw() const override {
                if (current()) current().get()->render();
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (current()) current().get()->collect_focusables(out);
            }

        protected:
            Size measure() const override {
                return current() ? current().get()->desired() : Size{};
            }

        private:
            const Element& current() const {
                return m_index >= 0 && m_index < static_cast<int>(m_pages.size()) ? m_pages[static_cast<size_t>(m_index)] : m_none;
            }

            std::vector<Element> m_pages;
            Element m_none;
            int m_index = 0;
        };
        //////////////////////////////////////////////////

        // Tab bar //
        class Tab_Bar : public Widget {
        public:
            Tab_Bar(State<int> selected, std::vector<Text> titles, const Tabs_Props& props)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_selected(std::move(selected))
                , m_titles(std::move(titles))
                , m_font_size(props.fontSize)
                , m_padding(props.tabPadding)
                , m_indicator_height(props.indicatorHeight)
                , m_stretch(props.stretch)
                , m_text_color(props.textColor)
                , m_active_color(props.activeColor)
                , m_indicator_color(props.indicatorColor) {
                for (size_t i = 0; i < m_titles.size(); ++i) {
                    m_runs.emplace_back(m_titles[i].get(), m_font_size);
                    m_hover.emplace_back(0.0);
                    m_hovered.push_back(0);
                    watch(m_titles[i], [this, i](const std::string& value) {
                        if (m_runs[i].set_text(value)) request_layout();
                        });
                }
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                m_rects.clear();
                const size_t count = m_runs.size();
                const double slot = count > 0 ? bounds.width / static_cast<double>(count) : 0.0;
                const double height = std::max(0.0, bounds.height - m_indicator_height);

                double x = bounds.x;
                for (size_t i = 0; i < count; ++i) {
                    double width = m_stretch ? slot : m_runs[i].measured().width + m_padding * 2.0;
                    m_rects.push_back({ x, bounds.y, width, height });
                    x += width;
                }

                const int current = selected_index();
                if (current < 0) return;

                const coordinate::rect& target = m_rects[static_cast<size_t>(current)];
                if (!m_placed) {
                    m_indicator_x.set(target.x);
                    m_indicator_width.set(target.width);
                    m_placed = true;
                }
                else {
                    if (target.x != m_target_x) m_indicator_x.animate_to(target.x, ease(INDICATOR_TIME));
                    if (target.width != m_target_width) m_indicator_width.animate_to(target.width, ease(INDICATOR_TIME));
                }
                m_target_x = target.x;
                m_target_width = target.width;
            }

            void update(Input_State& input) override {
                if (has_focus()) {
                    if (input.keys.left) {
                        input.keys.left = false;
                        m_selected.set(std::max(selected_index() - 1, 0));
                    }
                    if (input.keys.right) {
                        input.keys.right = false;
                        m_selected.set(std::min(selected_index() + 1, static_cast<int>(m_runs.size()) - 1));
                    }
                }

                int hovered = -1;
                if (!input.consumed) {
                    for (size_t i = 0; i < m_rects.size(); ++i) {
                        if (m_rects[i].contains(input.mouse)) hovered = static_cast<int>(i);
                    }
                }

                for (size_t i = 0; i < m_hover.size(); ++i) {
                    const bool over = static_cast<int>(i) == hovered;
                    if (over != (m_hovered[i] != 0)) {
                        m_hovered[i] = over ? 1 : 0;
                        m_hover[i].animate_to(over ? 1.0 : 0.0, ease(HOVER_TIME));
                    }
                }

                if (hovered >= 0 && input.pressed) {
                    input.consumed = true;
                    m_selected.set(hovered);
                }
            }

            void draw() const override {
                constexpr double BASELINE = 2.0;
                mesh::draw(mesh::make_rect({ m_bounds.x, m_bounds.y + m_bounds.height - BASELINE, m_bounds.width, BASELINE }), theme.divider);

                const int current = selected_index();
                for (size_t i = 0; i < m_rects.size(); ++i) {
                    const coordinate::rect& r = m_rects[i];
                    const Color color = static_cast<int>(i) == current
                        ? m_active_color
                        : detail::mix(m_text_color, m_active_color, m_hover[i].get());
                    const double text_width = m_runs[i].measured().width;
                    m_runs[i].draw({ r.x + (r.width - text_width) * 0.5, r.y + (r.height - m_font_size) * 0.5 }, color);
                }

                if (current >= 0) {
                    mesh::draw(mesh::make_rect({
                        m_indicator_x.get(),
                        m_bounds.y + m_bounds.height - m_indicator_height,
                        m_indicator_width.get(),
                        m_indicator_height
                        }, m_indicator_height * 0.5), m_indicator_color);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                double width = 0.0;
                for (const detail::Text_Run& run : m_runs) width += run.measured().width + m_padding * 2.0;
                return { width, m_font_size + m_padding + m_indicator_height };
            }

        private:
            static constexpr double INDICATOR_TIME = 0.2;
            static constexpr double HOVER_TIME = 0.12;

            int selected_index() const {
                if (m_runs.empty()) return -1;
                return std::clamp(m_selected.get(), 0, static_cast<int>(m_runs.size()) - 1);
            }

            State<int> m_selected;
            std::vector<Text> m_titles;
            double m_font_size;
            double m_padding;
            double m_indicator_height;
            bool m_stretch;
            Color m_text_color;
            Color m_active_color;
            Color m_indicator_color;
            std::vector<detail::Text_Run> m_runs;
            std::vector<Animated> m_hover;
            std::vector<char> m_hovered;
            std::vector<coordinate::rect> m_rects;
            Animated m_indicator_x{ 0.0 };
            Animated m_indicator_width{ 0.0 };
            double m_target_x = 0.0;
            double m_target_width = 0.0;
            bool m_placed = false;
        };
        //////////////////////////////////////////////////

        // Dropdown //
        class Dropdown : public Widget {
        public:
            event::Signal<int> OnChanged;

            Dropdown(State<int> state, std::vector<Text> options, const Dropdown_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_state(std::move(state))
                , m_options(std::move(options))
                , m_placeholder(props.placeholder)
                , m_placeholder_run(m_placeholder.get(), props.fontSize)
                , m_font_size(props.fontSize)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_max_visible(std::max(props.maxVisible, 1))
                , m_text_color(props.textColor)
                , m_placeholder_color(props.placeholderColor)
                , m_background(props.backgroundColor)
                , m_list_color(props.listColor)
                , m_hover_color(props.hoverColor)
                , m_active_color(props.activeColor)
                , m_focus_color(props.focusColor) {
                for (size_t i = 0; i < m_options.size(); ++i) {
                    m_runs.emplace_back(m_options[i].get(), m_font_size);
                    watch(m_options[i], [this, i](const std::string& value) {
                        if (m_runs[i].set_text(value)) request_layout();
                        });
                }
                watch(m_placeholder, [this](const std::string& value) {
                    if (m_placeholder_run.set_text(value)) request_layout();
                    });
            }

            bool is_open() const { return m_open; }
            int hovered_index() const { return m_hover_index; }
            const coordinate::rect& popup_bounds() const { return m_list; }

            void close_popup() override { close(); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);
                layout_list();
            }

            void update(Input_State& input) override {
                if (input.inactive) { close(); return; }

                if (has_focus() && !m_open) {
                    if (input.keys.enter || input.keys.space) {
                        input.keys.enter = input.keys.space = false;
                        open();
                        return;
                    }
                }

                const bool over = !input.consumed && m_mesh.contains(input.mouse);
                if (over && input.pressed) {
                    input.consumed = true;
                    if (m_open) close();
                    else open();
                }
            }

            void update_popup(Input_State& input) override {
                if (!m_open) return;
                layout_list();
                Key_State& keys = input.keys;

                if (m_list.contains(input.mouse)) {
                    input.consumed = true;

                    const double local = input.mouse.y - m_list.y - LIST_PAD;
                    long row = -1;
                    if (local >= 0.0) {
                        row = static_cast<long>(m_first) + static_cast<long>(std::floor(local / item_height()));
                        if (row >= static_cast<long>(m_first + visible_count()) || row >= static_cast<long>(m_options.size())) row = -1;
                    }
                    if (row >= 0) m_hover_index = static_cast<int>(row);

                    if (input.wheel != 0.0) {
                        scroll_items(input.wheel > 0.0 ? -1 : 1);
                        input.wheel = 0.0;
                    }
                    if (input.pressed && row >= 0) {
                        choose(static_cast<int>(row));
                        return;
                    }
                }
                else if (input.pressed) {
                    input.consumed = true;
                    close();
                    return;
                }

                const int count = static_cast<int>(m_options.size());
                if (keys.escape) { keys.escape = false; close(); return; }
                if (keys.down && count > 0) {
                    keys.down = false;
                    m_hover_index = std::min(m_hover_index + 1, count - 1);
                    keep_hover_visible();
                }
                if (keys.up && count > 0) {
                    keys.up = false;
                    m_hover_index = std::max(m_hover_index - 1, 0);
                    keep_hover_visible();
                }
                if (keys.enter && m_hover_index >= 0) {
                    keys.enter = false;
                    choose(m_hover_index);
                }
            }

            void draw() const override {
                if (m_open) {
                    constexpr double RING = 3.0;
                    coordinate::rect ring{ m_bounds.x - RING, m_bounds.y - RING, m_bounds.width + RING * 2.0, m_bounds.height + RING * 2.0 };
                    mesh::draw(mesh::make_rect(ring, m_corner_radius + RING), m_focus_color);
                }
                mesh::draw(m_mesh, m_background);

                const double chevron = m_font_size * 0.5;
                const coordinate::rect text_area{
                    m_bounds.x + m_padding,
                    m_bounds.y,
                    std::max(0.0, m_bounds.width - m_padding * 3.0 - chevron),
                    m_bounds.height
                };
                const double text_y = m_bounds.y + (m_bounds.height - m_font_size) * 0.5;
                const int selected = selected_index();

                detail::push_clip(text_area);
                if (selected >= 0) m_runs[static_cast<size_t>(selected)].draw({ text_area.x, text_y }, m_text_color);
                else m_placeholder_run.draw({ text_area.x, text_y }, m_placeholder_color);
                detail::pop_clip();

                const double cx = m_bounds.x + m_bounds.width - m_padding - chevron * 0.5;
                const double cy = m_bounds.y + m_bounds.height * 0.5;
                const double half = chevron * 0.25;
                coordinate::tri_mesh arrow;
                if (m_open) arrow.tris.push_back({ { cx - chevron * 0.5, cy + half },{ cx + chevron * 0.5, cy + half },{ cx, cy - half } });
                else arrow.tris.push_back({ { cx - chevron * 0.5, cy - half },{ cx + chevron * 0.5, cy - half },{ cx, cy + half } });
                arrow.update_bounds();
                mesh::draw(arrow, m_text_color);

                if (m_open) queue_overlay();
            }

            void draw_overlay() const override {
                mesh::drop_shadow(m_list, m_corner_radius, 12.0, 0.35);
                mesh::draw(mesh::make_rect(m_list, m_corner_radius), m_list_color);

                const double step = item_height();
                const size_t visible = visible_count();
                const int selected = selected_index();

                detail::push_clip(m_list);
                for (size_t n = 0; n < visible; ++n) {
                    const size_t i = m_first + n;
                    if (i >= m_options.size()) break;

                    const coordinate::rect row{
                        m_list.x + LIST_PAD,
                        m_list.y + LIST_PAD + step * static_cast<double>(n),
                        m_list.width - LIST_PAD * 2.0,
                        step
                    };
                    if (static_cast<int>(i) == m_hover_index) mesh::draw(mesh::make_rect(row, m_corner_radius * 0.6), m_hover_color);

                    const Color color = static_cast<int>(i) == selected ? m_active_color : m_text_color;
                    m_runs[i].draw({ row.x + m_padding * 0.75, row.y + (step - m_font_size) * 0.5 }, color);
                }
                detail::pop_clip();

                if (m_options.size() > visible) {
                    const double area = step * static_cast<double>(visible);
                    const double thumb = std::max(24.0, area * static_cast<double>(visible) / static_cast<double>(m_options.size()));
                    const double span = static_cast<double>(m_options.size() - visible);
                    const double top = m_list.y + LIST_PAD + (area - thumb) * (static_cast<double>(m_first) / span);
                    mesh::draw(mesh::make_rect({ m_list.x + m_list.width - 8.0, top, 4.0, thumb }, 2.0), theme.tab_inactive);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                double widest = m_placeholder_run.measured().width;
                for (const detail::Text_Run& run : m_runs) widest = std::max(widest, run.measured().width);
                return { widest + m_padding * 3.0 + m_font_size * 0.5, m_font_size + m_padding * 2.0 };
            }

        private:
            static constexpr double LIST_PAD = 6.0;
            static constexpr double LIST_GAP = 4.0;

            double item_height() const { return m_font_size + m_padding; }
            size_t visible_count() const { return std::min(m_options.size(), static_cast<size_t>(m_max_visible)); }

            int selected_index() const {
                const int value = m_state.get();
                return value >= 0 && value < static_cast<int>(m_options.size()) ? value : -1;
            }

            void layout_list() {
                const double height = static_cast<double>(visible_count()) * item_height() + LIST_PAD * 2.0;
                double y = m_bounds.y + m_bounds.height + LIST_GAP;
                if (y + height > Internal::mapper.get_height() && m_bounds.y - LIST_GAP - height >= 0.0) {
                    y = m_bounds.y - LIST_GAP - height;
                }
                m_list = { m_bounds.x, y, m_bounds.width, height };
            }

            void open() {
                if (m_options.empty()) return;
                if (Internal::popup && Internal::popup != this) Internal::popup->close_popup();

                m_open = true;
                Internal::popup = this;
                m_hover_index = selected_index();
                m_first = 0;
                keep_hover_visible();
                layout_list();
            }

            void close() {
                m_open = false;
                if (Internal::popup == this) Internal::popup = nullptr;
            }

            void choose(int index) {
                m_state.set(index);
                OnChanged.Fire(index);
                close();
            }

            void scroll_items(int delta) {
                const long limit = static_cast<long>(m_options.size()) - static_cast<long>(visible_count());
                m_first = static_cast<size_t>(std::clamp(static_cast<long>(m_first) + delta, 0L, std::max(limit, 0L)));
            }

            void keep_hover_visible() {
                if (m_hover_index < 0) return;
                const size_t hover = static_cast<size_t>(m_hover_index);
                const size_t visible = visible_count();
                if (hover < m_first) m_first = hover;
                if (hover >= m_first + visible) m_first = hover + 1 - visible;
            }

            State<int> m_state;
            std::vector<Text> m_options;
            Text m_placeholder;
            detail::Text_Run m_placeholder_run;
            double m_font_size;
            double m_padding;
            double m_corner_radius;
            int m_max_visible;
            Color m_text_color;
            Color m_placeholder_color;
            Color m_background;
            Color m_list_color;
            Color m_hover_color;
            Color m_active_color;
            Color m_focus_color;
            std::vector<detail::Text_Run> m_runs;
            coordinate::rect m_list;
            size_t m_first = 0;
            int m_hover_index = -1;
            bool m_open = false;
        };
        //////////////////////////////////////////////////

        // Tooltip //
        class Tooltip : public Widget {
        public:
            Tooltip(Text text, Element child, const Tooltip_Props& props)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_child(std::move(child))
                , m_delay(props.delay)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_offset_x(props.offsetX)
                , m_offset_y(props.offsetY)
                , m_background(props.backgroundColor)
                , m_text_color(props.textColor) {
                watch(m_text, [this](const std::string& value) { m_run.set_text(value); });
            }

            bool showing() const { return m_showing; }

            double flex_weight() const override { return m_child ? m_child.get()->flex_weight() : 0.0; }

            Size desired_for_width(double width) const override {
                return m_child ? m_child.get()->desired_for_width(width) : Size{};
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (m_child) m_child.get()->arrange(bounds);
            }

            void update(Input_State& input) override {
                const bool blocked = input.consumed || input.inactive;
                if (m_child) m_child.get()->dispatch(input);

                const bool inside = !blocked && m_bounds.contains(input.mouse);
                if (!inside) {
                    m_hovering = false;
                    m_dismissed = false;
                    m_showing = false;
                    return;
                }

                if (input.pressed || input.down) {
                    m_dismissed = true;
                    m_showing = false;
                    return;
                }
                if (m_dismissed) return;

                m_mouse = input.mouse;
                if (!m_hovering) {
                    m_hovering = true;
                    m_hover_start = GetTime();
                }
                if (!m_showing && GetTime() - m_hover_start >= m_delay) {
                    m_showing = true;
                    m_fade.set(0.0);
                    m_fade.animate_to(1.0, ease(FADE_TIME));
                }
            }

            void draw() const override {
                if (m_child) m_child.get()->render();
                if (m_showing) queue_overlay();
            }

            void draw_overlay() const override {
                const Size& extent = m_run.measured();
                const double width = extent.width + m_padding * 2.0;
                const double height = extent.height + m_padding * 2.0;
                const double canvas_w = Internal::mapper.get_width();
                const double canvas_h = Internal::mapper.get_height();

                double x = std::min(m_mouse.x + m_offset_x, canvas_w - width);
                double y = m_mouse.y + m_offset_y;
                if (y + height > canvas_h) y = m_mouse.y - height - m_offset_y * 0.4;
                x = std::max(x, 0.0);
                y = std::max(y, 0.0);

                const double previous = Internal::opacity;
                Internal::opacity *= m_fade.get();
                mesh::drop_shadow({ x, y, width, height }, m_corner_radius, 10.0, 0.3);
                mesh::draw(mesh::make_rect({ x, y, width, height }, m_corner_radius), m_background);
                m_run.draw({ x + m_padding, y + m_padding }, m_text_color);
                Internal::opacity = previous;
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (m_child) m_child.get()->collect_focusables(out);
            }

        protected:
            Size measure() const override { return m_child ? m_child.get()->desired() : Size{}; }

        private:
            static constexpr double FADE_TIME = 0.12;

            Text m_text;
            detail::Text_Run m_run;
            Element m_child;
            double m_delay;
            double m_padding;
            double m_corner_radius;
            double m_offset_x;
            double m_offset_y;
            Color m_background;
            Color m_text_color;
            Animated m_fade{ 0.0 };
            coordinate::pos m_mouse;
            double m_hover_start = 0.0;
            bool m_hovering = false;
            bool m_dismissed = false;
            bool m_showing = false;
        };
        //////////////////////////////////////////////////

        // Image //
        class Image_View : public Widget {
        public:
            Image_View(Texture2D texture, const Image_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_texture(texture)
                , m_fit(props.fit)
                , m_tint(props.tint) {
            }

            void draw() const override {
                const double tw = static_cast<double>(m_texture.width);
                const double th = static_cast<double>(m_texture.height);
                if (m_texture.id == 0 || tw <= 0.0 || th <= 0.0 || m_bounds.width <= 0.0 || m_bounds.height <= 0.0) return;

                const Color tint = Internal::faded(m_tint);
                if (tint.a == 0) return;

                Rectangle source{ 0.0f, 0.0f, static_cast<float>(tw), static_cast<float>(th) };
                coordinate::rect target = m_bounds;

                if (m_fit == Fit::Contain) {
                    const double scale = std::min(m_bounds.width / tw, m_bounds.height / th);
                    const double width = tw * scale;
                    const double height = th * scale;
                    target = { m_bounds.x + (m_bounds.width - width) * 0.5, m_bounds.y + (m_bounds.height - height) * 0.5, width, height };
                }
                else if (m_fit == Fit::Cover) {
                    const double scale = std::max(m_bounds.width / tw, m_bounds.height / th);
                    const double visible_w = m_bounds.width / scale;
                    const double visible_h = m_bounds.height / scale;
                    source = {
                        static_cast<float>((tw - visible_w) * 0.5),
                        static_cast<float>((th - visible_h) * 0.5),
                        static_cast<float>(visible_w),
                        static_cast<float>(visible_h)
                    };
                }

                Vector2 top_left = Internal::mapper.logical_to_screen({ target.x, target.y });
                Vector2 bottom_right = Internal::mapper.logical_to_screen({ target.x + target.width, target.y + target.height });
                DrawTexturePro(m_texture, source, { top_left.x, top_left.y, bottom_right.x - top_left.x, bottom_right.y - top_left.y },
                    { 0.0f, 0.0f }, 0.0f, tint);
            }

        protected:
            Size measure() const override {
                return { static_cast<double>(m_texture.width), static_cast<double>(m_texture.height) };
            }

        private:
            Texture2D m_texture;
            Fit m_fit;
            Color m_tint;
        };
        //////////////////////////////////////////////////

        // Progress bar //
        class Progress_Bar : public Widget {
        public:
            Progress_Bar(double fraction, const Progress_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_corner_radius(props.cornerRadius)
                , m_animate_time(props.animateTime)
                , m_track_color(props.trackColor)
                , m_fill_color(props.fillColor)
                , m_shown(std::clamp(fraction, 0.0, 1.0)) {
                watch(m_shown, [this](double) { rebuild(); });
            }

            void set_fraction(double fraction) {
                m_shown.animate_to(std::clamp(fraction, 0.0, 1.0), ease(m_animate_time));
            }

            double fraction() const { return m_shown.get(); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);
                rebuild();
            }

            void draw() const override {
                mesh::draw(m_mesh, m_track_color);
                mesh::draw(m_fill, m_fill_color);
            }

        protected:
            Size measure() const override { return { DEFAULT_WIDTH, DEFAULT_HEIGHT }; }

        private:
            static constexpr double DEFAULT_WIDTH = 320.0;
            static constexpr double DEFAULT_HEIGHT = 16.0;

            void rebuild() {
                double width = m_bounds.width * m_shown.get();
                if (width <= 0.0) { m_fill = {}; return; }
                m_fill = mesh::make_rect({ m_bounds.x, m_bounds.y, width, m_bounds.height }, m_corner_radius);
            }

            double m_corner_radius;
            double m_animate_time;
            Color m_track_color;
            Color m_fill_color;
            Animated m_shown;
            coordinate::tri_mesh m_fill;
        };
        //////////////////////////////////////////////////

        // Scroll view //
        class Scroll_View : public Widget {
        public:
            Scroll_View(const Scroll_Props& props, Element content)
                : Widget(props.width, props.height, props.flex)
                , m_content(std::move(content))
                , m_bar_width(props.scrollbarWidth)
                , m_bar_min_width(props.scrollbarMinWidth)
                , m_bar_fade_time(props.scrollbarFadeTime)
                , m_bar_idle_time(props.scrollbarIdleTime)
                , m_wheel_step(props.wheelStep)
                , m_smooth_time(props.smoothTime)
                , m_track_color(props.trackColor)
                , m_thumb_color(props.thumbColor) {
                watch(m_offset, [](double) { request_layout(); });
            }

            void scroll_to(double offset) {
                m_target = std::clamp(offset, 0.0, max_offset());
                m_offset.animate_to(m_target, ease(m_smooth_time));
            }

            double offset() const { return m_offset.get(); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                Size content_size = m_content ? m_content.get()->desired_for_width(bounds.width) : Size{};
                m_overflow = content_size.height > bounds.height + EPSILON;
                if (m_overflow && m_content) {
                    content_size = m_content.get()->desired_for_width(std::max(0.0, bounds.width - m_bar_width));
                }
                m_content_height = content_size.height;

                double limit = max_offset();
                if (m_offset.get() > limit) m_offset.set(limit);
                m_target = std::clamp(m_target, 0.0, limit);

                if (m_content) {
                    double width = bounds.width - (m_overflow ? m_bar_width : 0.0);
                    m_content.get()->arrange({
                        bounds.x,
                        bounds.y - m_offset.get(),
                        std::max(0.0, width),
                        std::max(m_content_height, bounds.height)
                        });
                }

                rebuild_bar();
            }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);

                if (over && input.pressed && m_overflow && m_thumb.contains(input.mouse)) {
                    m_dragging = true;
                    m_grab = input.mouse.y - m_thumb.bounds.y;
                }

                if (m_dragging) {
                    input.consumed = true;
                    if (input.down) drag_to(input.mouse.y);
                    else m_dragging = false;
                }
                else if (m_content) {
                    Input_State inner = input;
                    if (!over) {
                        inner.mouse = { FAR_AWAY, FAR_AWAY };
                        inner.pressed = false;
                    }
                    m_content.get()->dispatch(inner);
                    input.consumed = inner.consumed;
                    input.wheel = inner.wheel;
                }

                if (over && m_overflow && input.wheel != 0.0) {
                    scroll_to(m_target - input.wheel * m_wheel_step);
                    input.wheel = 0.0;
                }

                if (m_overflow) {
                    const double now = GetTime();
                    if (over || m_dragging) m_last_bar_activity = now;
                    const bool bar_visible = over || m_dragging || (now - m_last_bar_activity < m_bar_idle_time);
                    m_bar_visibility.animate_to(bar_visible ? 1.0 : 0.0,
                        ease(bar_visible ? 0.15 : m_bar_fade_time));
                }
            }

            void draw() const override {
                if (m_content) {
                    detail::push_clip(m_bounds);
                    m_content.get()->render();
                    detail::pop_clip();
                }

                if (m_overflow) {
                    const double vis = m_bar_visibility.get();
                    const double w = m_bar_min_width + (m_bar_width - m_bar_min_width) * vis;
                    const double alpha = 0.4 + 0.6 * vis;

                    const double x = m_bounds.x + m_bounds.width - w;

                    Color track = m_track_color;
                    track.a = static_cast<unsigned char>(track.a * alpha);
                    Color thumb = m_thumb_color;
                    thumb.a = static_cast<unsigned char>(thumb.a * alpha);

                    const double thumb_h = std::max(MIN_THUMB, m_bounds.height * m_bounds.height / m_content_height);
                    const double limit = max_offset();
                    const double t = limit > 0.0 ? std::clamp(m_offset.get() / limit, 0.0, 1.0) : 0.0;
                    const double y = m_bounds.y + t * (m_bounds.height - thumb_h);

                    if (track.a > 0) {
                        mesh::draw(mesh::make_rect({ x, m_bounds.y, w, m_bounds.height }, w * 0.5), track);
                    }
                    mesh::draw(mesh::make_rect({ x, y, w, thumb_h }, w * 0.5), thumb);
                }
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (m_content) m_content.get()->collect_focusables(out);
            }

        protected:
            Size measure() const override {
                Size content_size = m_content ? m_content.get()->desired() : Size{};
                return { content_size.width + m_bar_width, content_size.height };
            }

        private:
            static constexpr double EPSILON = 0.5;
            static constexpr double MIN_THUMB = 40.0;
            static constexpr double FAR_AWAY = -1.0e9;

            double max_offset() const { return std::max(0.0, m_content_height - m_bounds.height); }

            void drag_to(double mouse_y) {
                double travel = m_bounds.height - m_thumb.bounds.height;
                if (travel <= 0.0) return;

                double t = std::clamp((mouse_y - m_grab - m_bounds.y) / travel, 0.0, 1.0);
                m_target = t * max_offset();
                m_offset.set(m_target);
            }

            void rebuild_bar() {
                if (!m_overflow) { m_track = {}; m_thumb = {}; return; }

                double x = m_bounds.x + m_bounds.width - m_bar_width;
                double thumb_height = std::clamp(m_bounds.height * m_bounds.height / m_content_height,
                    MIN_THUMB, m_bounds.height);
                double limit = max_offset();
                double t = limit > 0.0 ? std::clamp(m_offset.get() / limit, 0.0, 1.0) : 0.0;
                double y = m_bounds.y + t * (m_bounds.height - thumb_height);

                m_track = mesh::make_rect({ x, m_bounds.y, m_bar_width, m_bounds.height }, m_bar_width * 0.5);
                m_thumb = mesh::make_rect({ x, y, m_bar_width, thumb_height }, m_bar_width * 0.5);
            }

            Element m_content;
            double m_bar_width;
            double m_bar_min_width;
            double m_bar_fade_time;
            double m_bar_idle_time;
            double m_wheel_step;
            double m_smooth_time;
            Color m_track_color;
            Color m_thumb_color;
            Animated m_offset{ 0.0 };
            Animated m_bar_visibility{ 0.0 };
            double m_target = 0.0;
            double m_content_height = 0.0;
            double m_grab = 0.0;
            double m_last_bar_activity = 0.0;
            bool m_overflow = false;
            bool m_dragging = false;
            coordinate::tri_mesh m_track;
            coordinate::tri_mesh m_thumb;
        };
        //////////////////////////////////////////////////

        // Table //
        class Table : public Widget {
        public:
            event::Signal<int> OnRowClick;

            Table(std::vector<Table_Column> columns, std::vector<Table_Row> rows,
                const Table_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_columns(std::move(columns))
                , m_rows(std::move(rows))
                , m_font_size(props.fontSize)
                , m_header_font_size(props.headerFontSize)
                , m_row_padding(props.rowPadding)
                , m_cell_padding(props.cellPadding)
                , m_corner_radius(props.cornerRadius)
                , m_striped(props.striped)
                , m_sticky_header(props.stickyHeader)
                , m_scrollbar_width(props.scrollbarWidth)
                , m_scrollbar_min_width(props.scrollbarMinWidth)
                , m_scrollbar_fade_time(props.scrollbarFadeTime)
                , m_scrollbar_idle_time(props.scrollbarIdleTime)
                , m_wheel_step(props.wheelStep)
                , m_smooth_time(props.smoothTime)
                , m_header_bg(props.headerBackground)
                , m_row_color(props.rowColor)
                , m_alt_row_color(props.altRowColor)
                , m_hover_color(props.hoverColor)
                , m_selected_color(props.selectedColor)
                , m_text_color(props.textColor)
                , m_selected_text_color(props.selectedTextColor)
                , m_divider_color(props.dividerColor) {
                watch(m_scroll, [](double) { request_layout(); });
            }

            void set_rows(std::vector<Table_Row> rows) {
                m_rows = std::move(rows);
                request_layout();
            }

            int selected_row() const { return m_selected; }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);

                m_header_height = m_header_font_size + m_row_padding * 2.0;
                m_row_height = m_font_size + m_row_padding * 2.0;
                m_body_height = bounds.height - m_header_height;
                m_content_height = static_cast<double>(m_rows.size()) * m_row_height;
                m_overflow = m_content_height > m_body_height + 0.5;

                const double limit = max_scroll();
                if (m_scroll.get() > limit) m_scroll.set(limit);
                m_target = std::clamp(m_target, 0.0, limit);

                m_col_x.clear();
                m_col_w.clear();
                double fixed_total = 0.0;
                double flex_total = 0.0;
                for (const auto& c : m_columns) {
                    if (c.width >= 0.0) { fixed_total += c.width; }
                    else { flex_total += std::max(c.flex, 0.0001); }
                }
                const double avail = bounds.width - (m_overflow ? m_scrollbar_width : 0.0);
                const double flex_pool = std::max(0.0, avail - fixed_total);
                double x = bounds.x;
                for (const auto& c : m_columns) {
                    double w = c.width >= 0.0 ? c.width
                        : (flex_total > 0.0 ? flex_pool * (std::max(c.flex, 0.0001) / flex_total) : 0.0);
                    m_col_x.push_back(x);
                    m_col_w.push_back(w);
                    x += w;
                }
            }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);
                if (over && m_overflow && input.wheel != 0.0) {
                    scroll_to(m_target - input.wheel * m_wheel_step);
                    input.wheel = 0.0;
                }

                if (m_overflow) {
                    const double now = GetTime();
                    if (over) m_last_bar_activity = now;
                    const bool bar_visible = over || (now - m_last_bar_activity < m_scrollbar_idle_time);
                    m_bar_visibility.animate_to(bar_visible ? 1.0 : 0.0,
                        ease(bar_visible ? 0.15 : m_scrollbar_fade_time));
                }

                m_hover_row = -1;
                if (over && input.mouse.y > m_bounds.y + m_header_height) {
                    const double local_y = input.mouse.y - (m_bounds.y + m_header_height) + m_scroll.get();
                    const long r = static_cast<long>(std::floor(local_y / m_row_height));
                    if (r >= 0 && r < static_cast<long>(m_rows.size())) m_hover_row = static_cast<int>(r);
                }

                if (over && input.pressed && m_hover_row >= 0) {
                    input.consumed = true;
                    m_selected = m_hover_row;
                    OnRowClick.Fire(m_selected);
                }
            }

            void draw() const override {
                mesh::draw(m_mesh, m_row_color);

                const double body_y = m_bounds.y + m_header_height;
                const double body_h = m_bounds.height - m_header_height;

                detail::push_clip({ m_bounds.x, body_y, m_bounds.width, body_h });
                const double scroll = m_scroll.get();
                for (size_t r = 0; r < m_rows.size(); ++r) {
                    const double ry = body_y + static_cast<double>(r) * m_row_height - scroll;
                    if (ry + m_row_height < body_y) continue;
                    if (ry > body_y + body_h) break;

                    coordinate::rect row_rect{ m_bounds.x, ry, m_bounds.width, m_row_height };
                    const bool selected = static_cast<int>(r) == m_selected;
                    const bool hovered = static_cast<int>(r) == m_hover_row;

                    if (selected) mesh::draw(mesh::make_rect(row_rect), m_selected_color);
                    else if (hovered) mesh::draw(mesh::make_rect(row_rect), m_hover_color);
                    else if (m_striped && (r % 2 == 1)) mesh::draw(mesh::make_rect(row_rect), m_alt_row_color);

                    const Color color = selected ? m_selected_text_color : m_text_color;
                    for (size_t c = 0; c < m_columns.size() && c < m_rows[r].cells.size(); ++c) {
                        const std::string& text = m_rows[r].cells[c];
                        const double tw = font.measure(text, m_font_size).width;
                        double tx = m_col_x[c] + m_cell_padding;
                        if (m_columns[c].align == Align::Center) tx = m_col_x[c] + (m_col_w[c] - tw) * 0.5;
                        else if (m_columns[c].align == Align::End) tx = m_col_x[c] + m_col_w[c] - tw - m_cell_padding;
                        const double ty = ry + (m_row_height - m_font_size) * 0.5;
                        font.draw(text, { tx, ty }, m_font_size, color);
                    }
                }
                detail::pop_clip();

                coordinate::rect header_rect{ m_bounds.x, m_bounds.y, m_bounds.width, m_header_height };
                mesh::draw(mesh::make_rect(header_rect), m_header_bg);
                mesh::draw(mesh::make_rect({ m_bounds.x, m_bounds.y + m_header_height - 1.0, m_bounds.width, 1.0 }),
                    m_divider_color);

                for (size_t c = 0; c < m_columns.size(); ++c) {
                    const std::string& text = m_columns[c].header.get();
                    const double tw = font.measure(text, m_header_font_size).width;
                    double tx = m_col_x[c] + m_cell_padding;
                    if (m_columns[c].align == Align::Center) tx = m_col_x[c] + (m_col_w[c] - tw) * 0.5;
                    else if (m_columns[c].align == Align::End) tx = m_col_x[c] + m_col_w[c] - tw - m_cell_padding;
                    const double ty = m_bounds.y + (m_header_height - m_header_font_size) * 0.5;
                    font.draw(text, { tx, ty }, m_header_font_size, m_text_color);
                }

                if (m_overflow) {
                    const double vis = m_bar_visibility.get();
                    const double w = m_scrollbar_min_width + (m_scrollbar_width - m_scrollbar_min_width) * vis;
                    const double alpha = 0.4 + 0.6 * vis;

                    Color track = { 0, 0, 0, 60 };
                    track.a = static_cast<unsigned char>(track.a * alpha);
                    Color thumb = { 255, 255, 255, 110 };
                    thumb.a = static_cast<unsigned char>(thumb.a * alpha);

                    const double track_x = m_bounds.x + m_bounds.width - w;
                    const double thumb_h = std::max(30.0, body_h * body_h / m_content_height);
                    const double t = m_scroll.get() / max_scroll();
                    const double thumb_y = body_y + t * (body_h - thumb_h);

                    if (track.a > 0) {
                        mesh::draw(mesh::make_rect({ track_x, body_y, w, body_h }, w * 0.5), track);
                    }
                    mesh::draw(mesh::make_rect({ track_x, thumb_y, w, thumb_h }, w * 0.5), thumb);
                }
            }

        protected:
            Size measure() const override {
                double w = 0.0;
                for (const auto& c : m_columns) {
                    if (c.width >= 0.0) w += c.width;
                    else w += 120.0;
                }
                const double h = m_header_font_size + m_row_padding * 2.0
                    + static_cast<double>(m_rows.size()) * (m_font_size + m_row_padding * 2.0);
                return { w, h };
            }

        private:
            double max_scroll() const { return std::max(0.0, m_content_height - m_body_height); }

            void scroll_to(double offset) {
                m_target = std::clamp(offset, 0.0, max_scroll());
                m_scroll.animate_to(m_target, ease(m_smooth_time));
            }

            std::vector<Table_Column> m_columns;
            std::vector<Table_Row> m_rows;
            std::vector<double> m_col_x;
            std::vector<double> m_col_w;
            double m_font_size;
            double m_header_font_size;
            double m_row_padding;
            double m_cell_padding;
            double m_corner_radius;
            bool m_striped;
            bool m_sticky_header;
            double m_scrollbar_width;
            double m_scrollbar_min_width;
            double m_scrollbar_fade_time;
            double m_scrollbar_idle_time;
            double m_wheel_step;
            double m_smooth_time;
            Color m_header_bg;
            Color m_row_color;
            Color m_alt_row_color;
            Color m_hover_color;
            Color m_selected_color;
            Color m_text_color;
            Color m_selected_text_color;
            Color m_divider_color;
            double m_header_height = 0.0;
            double m_row_height = 0.0;
            double m_body_height = 0.0;
            double m_content_height = 0.0;
            double m_last_bar_activity = 0.0;
            bool m_overflow = false;
            int m_hover_row = -1;
            int m_selected = -1;
            Animated m_scroll{ 0.0 };
            Animated m_bar_visibility{ 0.0 };
            double m_target = 0.0;
        };
        //////////////////////////////////////////////////

        // Textbox //
        class Textbox : public Widget {
        public:
            event::Signal<std::string> OnChanged;
            event::Signal<std::string> OnSubmit;
            event::Signal<bool> OnFocusChanged;

            Textbox(State<std::string> state, const Textbox_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_state(std::move(state))
                , m_placeholder(props.placeholder)
                , m_font_size(props.fontSize)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_max_length(props.maxLength)
                , m_text_color(props.textColor)
                , m_placeholder_color(props.placeholderColor)
                , m_background(props.backgroundColor)
                , m_focus_color(props.focusColor)
                , m_caret_color(props.caretColor)
                , m_selection_color(props.selectionColor) {
                watch(m_state, [this](const std::string&) { clamp_selection(); });
            }

            const std::string& text() const { return m_state.get(); }
            bool focused() const { return m_focused; }

            void on_focus_changed(bool focused) override {
                m_focused = focused;
                m_selecting = false;
                m_anchor = m_cursor;
                restart_blink();
                OnFocusChanged.Fire(focused);
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);
                ensure_caret_visible();
            }

            void update(Input_State& input) override {
                const bool over = !input.consumed && m_mesh.contains(input.mouse);

                if (over && input.pressed) {
                    focus_widget(this);
                    input.consumed = true;
                    m_cursor = index_at(input.mouse.x);
                    if (!input.keys.shift) m_anchor = m_cursor;
                    m_selecting = true;
                    restart_blink();
                }

                if (m_selecting) {
                    input.consumed = true;
                    if (input.down) m_cursor = index_at(input.mouse.x);
                    else m_selecting = false;
                    ensure_caret_visible();
                }

                if (m_focused) handle_keys(input);
            }

            void draw() const override {
                if (m_focused) {
                    coordinate::rect ring{
                        m_bounds.x - FOCUS_RING,
                        m_bounds.y - FOCUS_RING,
                        m_bounds.width + FOCUS_RING * 2.0,
                        m_bounds.height + FOCUS_RING * 2.0
                    };
                    mesh::draw(mesh::make_rect(ring, m_corner_radius + FOCUS_RING), m_focus_color);
                }
                mesh::draw(m_mesh, m_background);

                coordinate::rect inner{
                    m_bounds.x + m_padding,
                    m_bounds.y + m_padding,
                    std::max(0.0, m_bounds.width - m_padding * 2.0),
                    std::max(0.0, m_bounds.height - m_padding * 2.0)
                };
                const double text_x = inner.x - m_scroll;
                const double text_y = inner.y + (inner.height - m_font_size) * 0.5;

                detail::push_clip(inner);

                if (text().empty()) {
                    font.draw(m_placeholder.get(), { text_x, text_y }, m_font_size, m_placeholder_color);
                }
                else {
                    if (m_focused && has_selection()) {
                        double x1 = text_x + width_to(selection_start());
                        double x2 = text_x + width_to(selection_end());
                        mesh::draw(mesh::make_rect({ x1, text_y, x2 - x1, m_font_size }), m_selection_color);
                    }
                    font.draw(text(), { text_x, text_y }, m_font_size, m_text_color);
                }

                if (m_focused && caret_visible()) {
                    double caret_x = text_x + width_to(m_cursor);
                    mesh::draw(mesh::make_rect({ caret_x, text_y, CARET_WIDTH, m_font_size }), m_caret_color);
                }

                detail::pop_clip();
            }

            bool focusable() const override { return true; }
            bool has_custom_focus_ring() const override { return true; }

        protected:
            Size measure() const override { return { DEFAULT_WIDTH, m_font_size + m_padding * 2.0 }; }

        private:
            static constexpr double DEFAULT_WIDTH = 480.0;
            static constexpr double CARET_WIDTH = 3.0;
            static constexpr double FOCUS_RING = 3.0;
            static constexpr double BLINK_PERIOD = 1.06;

            size_t selection_start() const { return std::min(m_anchor, m_cursor); }
            size_t selection_end() const { return std::max(m_anchor, m_cursor); }
            bool has_selection() const { return m_anchor != m_cursor; }

            double width_to(size_t index) const {
                return font.pen(text().substr(0, index), m_font_size);
            }

            size_t index_at(double x) const {
                const std::string& t = text();
                double local = x - (m_bounds.x + m_padding) + m_scroll;

                size_t best = 0;
                double best_distance = std::abs(local);
                for (size_t i = 0; i < t.size();) {
                    i = detail::utf8_next(t, i);
                    double w = width_to(i);
                    double distance = std::abs(local - w);
                    if (distance < best_distance) {
                        best = i;
                        best_distance = distance;
                    }
                    else if (w > local) {
                        break;
                    }
                }
                return best;
            }

            void clamp_selection() {
                m_cursor = std::min(m_cursor, text().size());
                m_anchor = std::min(m_anchor, text().size());
            }

            void ensure_caret_visible() {
                double inner = std::max(0.0, m_bounds.width - m_padding * 2.0);
                double caret = width_to(m_cursor);

                if (caret - m_scroll > inner - CARET_WIDTH) m_scroll = caret - (inner - CARET_WIDTH);
                if (caret - m_scroll < 0.0) m_scroll = caret;

                double total = width_to(text().size());
                m_scroll = std::clamp(m_scroll, 0.0, std::max(0.0, total + CARET_WIDTH - inner));
            }

            void restart_blink() { m_blink_start = GetTime(); }

            bool caret_visible() const {
                return std::fmod(GetTime() - m_blink_start, BLINK_PERIOD) < BLINK_PERIOD * 0.5;
            }

            void move_cursor(size_t position, bool extend) {
                m_cursor = position;
                if (!extend) m_anchor = position;
                restart_blink();
            }

            void commit(std::string updated) {
                m_state.set(std::move(updated));
                OnChanged.Fire(text());
                restart_blink();
            }

            void replace_range(size_t from, size_t to, const std::string& with) {
                std::string updated = text();
                updated.replace(from, to - from, with);
                m_cursor = m_anchor = from + with.size();
                commit(std::move(updated));
            }

            void erase_selection() {
                if (has_selection()) replace_range(selection_start(), selection_end(), "");
            }

            void insert(const std::string& raw) {
                std::string filtered;
                filtered.reserve(raw.size());
                for (char c : raw) {
                    unsigned char u = static_cast<unsigned char>(c);
                    if (u >= 32 && u != 127) filtered.push_back(c);
                }
                if (filtered.empty()) return;

                size_t from = selection_start();
                size_t to = selection_end();

                if (m_max_length > 0) {
                    size_t limit = static_cast<size_t>(m_max_length);
                    size_t used = detail::utf8_count(text()) - detail::utf8_count(text().substr(from, to - from));
                    size_t room = used < limit ? limit - used : 0;

                    size_t end = 0;
                    size_t taken = 0;
                    while (end < filtered.size() && taken < room) {
                        end = detail::utf8_next(filtered, end);
                        ++taken;
                    }
                    filtered.resize(end);
                    if (filtered.empty()) return;
                }

                replace_range(from, to, filtered);
            }

            void handle_keys(Input_State& input) {
                const Key_State& k = input.keys;

                if (k.escape) {
                    input.suppressed_keys.push_back(KEY_ESCAPE);
                    clear_focus();
                    return;
                }

                if (k.select_all) {
                    m_anchor = 0;
                    m_cursor = text().size();
                    restart_blink();
                }

                if ((k.copy || k.cut) && has_selection()) {
                    SetClipboardText(text().substr(selection_start(), selection_end() - selection_start()).c_str());
                    if (k.cut) erase_selection();
                }

                if (k.paste) {
                    const char* clip = GetClipboardText();
                    if (clip) insert(clip);
                }

                if (!input.typed.empty()) insert(input.typed);

                if (k.backspace) {
                    if (has_selection()) erase_selection();
                    else if (m_cursor > 0) replace_range(detail::utf8_prev(text(), m_cursor), m_cursor, "");
                }

                if (k.del) {
                    if (has_selection()) erase_selection();
                    else if (m_cursor < text().size()) replace_range(m_cursor, detail::utf8_next(text(), m_cursor), "");
                }

                if (k.left) {
                    if (has_selection() && !k.shift) move_cursor(selection_start(), false);
                    else move_cursor(detail::utf8_prev(text(), m_cursor), k.shift);
                }

                if (k.right) {
                    if (has_selection() && !k.shift) move_cursor(selection_end(), false);
                    else move_cursor(detail::utf8_next(text(), m_cursor), k.shift);
                }

                if (k.home) move_cursor(0, k.shift);
                if (k.end) move_cursor(text().size(), k.shift);
                if (k.enter) OnSubmit.Fire(text());

                ensure_caret_visible();
            }

            State<std::string> m_state;
            Text m_placeholder;
            double m_font_size;
            double m_padding;
            double m_corner_radius;
            int m_max_length;
            Color m_text_color;
            Color m_placeholder_color;
            Color m_background;
            Color m_focus_color;
            Color m_caret_color;
            Color m_selection_color;
            size_t m_cursor = 0;
            size_t m_anchor = 0;
            double m_scroll = 0.0;
            double m_blink_start = 0.0;
            bool m_focused = false;
            bool m_selecting = false;
        };
        //////////////////////////////////////////////////

        // Number input //
        class Number_Input : public Widget {
        public:
            event::Signal<double> OnChanged;
            event::Signal<bool> OnFocusChanged;

            Number_Input(std::function<double()> get, std::function<void(double)> set,
                const Number_Input_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_get(std::move(get))
                , m_set(std::move(set))
                , m_step(props.step)
                , m_min(props.min)
                , m_max(props.max)
                , m_decimals(std::max(props.decimals, 0)) {

                m_text_state = State<std::string>(format_value(m_get()));

                Textbox_Props box_props;
                box_props.fontSize = props.fontSize;
                box_props.padding = props.padding;
                box_props.cornerRadius = props.cornerRadius;
                box_props.backgroundColor = props.backgroundColor;
                box_props.focusColor = props.focusColor;
                box_props.textColor = props.textColor;

                m_box = detail::make_ref<Textbox>(m_text_state, box_props);
                m_box->OnSubmit.Connect([this](const std::string& v) { parse_and_set(v); });
                m_box->OnFocusChanged.Connect([this](bool f) {
                    if (!f) parse_and_set(m_text_state.get());
                    OnFocusChanged.Fire(f);
                    });
            }

            double value() const { return m_get(); }
            void sync() { m_text_state.set(format_value(m_get())); }
            bool focused() const { return m_box->focused(); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_box->arrange(bounds);
            }

            void update(Input_State& input) override {
                m_box->dispatch(input);
            }

            void draw() const override {
                m_box->render();
            }

            bool focusable() const override { return false; }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (!is_visible() || !is_enabled()) return;
                if (m_box) m_box->collect_focusables(out);
            }

        protected:
            Size measure() const override {
                const Size natural = m_box->desired();
                return { fixed_width() >= 0.0 ? fixed_width() : natural.width,
                         fixed_height() >= 0.0 ? fixed_height() : natural.height };
            }

        private:
            std::string format_value(double v) const {
                if (m_decimals <= 0) return std::to_string(static_cast<long long>(std::llround(v)));
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(m_decimals) << v;
                return ss.str();
            }

            void parse_and_set(const std::string& text) {
                try {
                    double parsed = std::stod(text);
                    if (m_step > 0.0) parsed = std::round(parsed / m_step) * m_step;
                    apply(parsed);
                }
                catch (...) {
                    m_text_state.set(format_value(m_get()));
                }
            }

            void apply(double raw) {
                const double clamped = std::clamp(raw, m_min, m_max);
                const double before = m_get();
                m_set(clamped);
                m_text_state.set(format_value(m_get()));
                if (m_get() != before) OnChanged.Fire(m_get());
            }

            std::function<double()> m_get;
            std::function<void(double)> m_set;
            double m_step;
            double m_min;
            double m_max;
            int m_decimals;

            State<std::string> m_text_state;
            Ref<Textbox> m_box;
        };
        //////////////////////////////////////////////////

        // Positioned //
        class Positioned : public Widget {
        public:
            Positioned(const Positioned_Props& props, Element child)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_child(std::move(child))
                , m_anchor(props.anchor)
                , m_x(props.x)
                , m_y(props.y)
                , m_child_width(props.width)
                , m_child_height(props.height) {
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (!m_child) return;

                Size wanted = m_child.get()->desired();
                double width = m_child_width >= 0.0 ? m_child_width : wanted.width;
                double height = m_child_height >= 0.0 ? m_child_height : m_child.get()->desired_for_width(width).height;
                (void)wanted;

                auto [fx, fy] = fractions(m_anchor);
                m_child.get()->arrange({
                    bounds.x + fx * (bounds.width - width) + m_x,
                    bounds.y + fy * (bounds.height - height) + m_y,
                    width,
                    height
                    });
            }

            void update(Input_State& input) override {
                if (m_child) m_child.get()->dispatch(input);
            }

            void draw() const override {
                if (m_child) m_child.get()->render();
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (m_child) m_child.get()->collect_focusables(out);
            }

        protected:
            Size measure() const override { return {}; }

        private:
            static std::pair<double, double> fractions(Anchor anchor) {
                switch (anchor) {
                case Anchor::TopLeft: return { 0.0, 0.0 };
                case Anchor::Top: return { 0.5, 0.0 };
                case Anchor::TopRight: return { 1.0, 0.0 };
                case Anchor::Left: return { 0.0, 0.5 };
                case Anchor::Center: return { 0.5, 0.5 };
                case Anchor::Right: return { 1.0, 0.5 };
                case Anchor::BottomLeft: return { 0.0, 1.0 };
                case Anchor::Bottom: return { 0.5, 1.0 };
                case Anchor::BottomRight: return { 1.0, 1.0 };
                }
                return { 0.0, 0.0 };
            }

            Element m_child;
            Anchor m_anchor;
            double m_x;
            double m_y;
            double m_child_width;
            double m_child_height;
        };
        //////////////////////////////////////////////////

        // Badge //
        class Badge : public Widget {
        public:
            Badge(Text text, const Badge_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_padding_x(props.paddingX)
                , m_padding_y(props.paddingY)
                , m_corner_radius(props.cornerRadius)
                , m_text_color(props.textColor)
                , m_background(props.backgroundColor) {
                watch(m_text, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);
            }

            void draw() const override {
                mesh::draw(m_mesh, m_background);
                const Size& extent = m_run.measured();
                m_run.draw({
                    m_bounds.x + (m_bounds.width - extent.width) * 0.5,
                    m_bounds.y + (m_bounds.height - extent.height) * 0.5
                    }, m_text_color);
            }

        protected:
            Size measure() const override {
                const Size& extent = m_run.measured();
                return { extent.width + m_padding_x * 2.0, extent.height + m_padding_y * 2.0 };
            }

        private:
            Text m_text;
            detail::Text_Run m_run;
            double m_padding_x;
            double m_padding_y;
            double m_corner_radius;
            Color m_text_color;
            Color m_background;
        };
        //////////////////////////////////////////////////

        // Chip //
        class Chip : public Widget {
        public:
            event::Signal<> OnClick;
            event::Signal<> OnClose;

            Chip(Text text, const Chip_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_padding_x(props.paddingX)
                , m_padding_y(props.paddingY)
                , m_corner_radius(props.cornerRadius)
                , m_closeable(props.closeable)
                , m_fade_time(props.fadeTime)
                , m_text_color(props.textColor)
                , m_background(props.backgroundColor)
                , m_hover_color(props.hoverColor)
                , m_close_color(props.closeColor) {
                watch(m_text, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);

                if (m_closeable) {
                    const double icon = m_run.size() * 0.5;
                    m_close_center = {
                        bounds.x + bounds.width - m_padding_x - icon * 0.5,
                        bounds.y + bounds.height * 0.5
                    };
                    m_close_radius = icon * 0.75;
                }
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    OnClick.Fire();
                    return;
                }

                const bool over = !input.consumed && m_mesh.contains(input.mouse);
                if (over != m_hovered) {
                    m_hovered = over;
                    m_highlight.animate_to(over ? 1.0 : 0.0, ease(m_fade_time));
                }

                if (!over || !input.pressed) return;
                input.consumed = true;

                if (m_closeable) {
                    const double dx = input.mouse.x - m_close_center.x;
                    const double dy = input.mouse.y - m_close_center.y;
                    if (dx * dx + dy * dy <= m_close_radius * m_close_radius) {
                        OnClose.Fire();
                        return;
                    }
                }
                OnClick.Fire();
            }

            void draw() const override {
                mesh::draw(m_mesh, detail::mix(m_background, m_hover_color, m_highlight.get()));

                const Size& extent = m_run.measured();
                m_run.draw({
                    m_bounds.x + m_padding_x,
                    m_bounds.y + (m_bounds.height - extent.height) * 0.5
                    }, m_text_color);

                if (m_closeable) {
                    const double half = m_run.size() * 0.18;
                    coordinate::pos a{ m_close_center.x - half, m_close_center.y - half };
                    coordinate::pos b{ m_close_center.x + half, m_close_center.y + half };
                    coordinate::pos c{ m_close_center.x + half, m_close_center.y - half };
                    coordinate::pos d{ m_close_center.x - half, m_close_center.y + half };
                    mesh::draw(mesh::make_line(a, b, 2.0), m_close_color);
                    mesh::draw(mesh::make_line(c, d, 2.0), m_close_color);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                const Size& extent = m_run.measured();
                const double close = m_closeable ? m_run.size() * 0.5 + 4.0 : 0.0;
                return { extent.width + m_padding_x * 2.0 + close, extent.height + m_padding_y * 2.0 };
            }

        private:
            Text m_text;
            detail::Text_Run m_run;
            double m_padding_x;
            double m_padding_y;
            double m_corner_radius;
            bool m_closeable;
            double m_fade_time;
            Color m_text_color;
            Color m_background;
            Color m_hover_color;
            Color m_close_color;
            Animated m_highlight{ 0.0 };
            coordinate::pos m_close_center;
            double m_close_radius = 0.0;
            bool m_hovered = false;
        };
        //////////////////////////////////////////////////

        // Segmented control //
        class Segmented_Control : public Widget {
        public:
            event::Signal<int> OnChanged;

            Segmented_Control(State<int> state, std::vector<Text> options, const Segmented_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_state(std::move(state))
                , m_font_size(props.fontSize)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_animate_time(props.animateTime)
                , m_text_color(props.textColor)
                , m_active_text_color(props.activeTextColor)
                , m_track_color(props.trackColor)
                , m_thumb_color(props.thumbColor) {
                for (Text& option : options) {
                    m_runs.emplace_back(option.get(), m_font_size);
                    watch(option, [this, i = m_runs.size() - 1](const std::string& value) {
                        if (m_runs[i].set_text(value)) request_layout();
                        });
                }
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                m_mesh = mesh::make_rect(bounds, m_corner_radius);

                m_rects.clear();
                const size_t count = m_runs.size();
                const double slot = count > 0 ? bounds.width / static_cast<double>(count) : 0.0;

                double x = bounds.x;
                for (size_t i = 0; i < count; ++i) {
                    m_rects.push_back({ x, bounds.y, slot, bounds.height });
                    x += slot;
                }

                const int current = selected_index();
                if (current < 0) return;

                const coordinate::rect& target = m_rects[static_cast<size_t>(current)];
                const double tx = target.x + THUMB_INSET;
                const double tw = std::max(0.0, target.width - THUMB_INSET * 2.0);

                if (!m_placed) {
                    m_thumb_x.set(tx);
                    m_thumb_width.set(tw);
                    m_target_x = tx;
                    m_target_width = tw;
                    m_placed = true;
                }
                else {
                    if (tx != m_target_x) m_thumb_x.animate_to(tx, ease(m_animate_time));
                    if (tw != m_target_width) m_thumb_width.animate_to(tw, ease(m_animate_time));
                    m_target_x = tx;
                    m_target_width = tw;
                }
            }

            void update(Input_State& input) override {
                if (has_focus()) {
                    if (input.keys.left) {
                        input.keys.left = false;
                        m_state.set(std::max(selected_index() - 1, 0));
                    }
                    if (input.keys.right) {
                        input.keys.right = false;
                        m_state.set(std::min(selected_index() + 1, static_cast<int>(m_runs.size()) - 1));
                    }
                }

                if (input.consumed || !input.pressed) return;
                for (size_t i = 0; i < m_rects.size(); ++i) {
                    if (!m_rects[i].contains(input.mouse)) continue;
                    input.consumed = true;
                    const int index = static_cast<int>(i);
                    if (index != m_state.get()) {
                        m_state.set(index);
                        OnChanged.Fire(index);
                    }
                    return;
                }
            }

            void draw() const override {
                mesh::draw(m_mesh, m_track_color);
                mesh::draw(mesh::make_rect({
                    m_thumb_x.get(),
                    m_bounds.y + THUMB_INSET,
                    m_thumb_width.get(),
                    std::max(0.0, m_bounds.height - THUMB_INSET * 2.0)
                    }, m_corner_radius), m_thumb_color);

                const int current = selected_index();
                for (size_t i = 0; i < m_rects.size(); ++i) {
                    const coordinate::rect& r = m_rects[i];
                    const Color color = static_cast<int>(i) == current ? m_active_text_color : m_text_color;
                    const double text_width = m_runs[i].measured().width;
                    m_runs[i].draw({
                        r.x + (r.width - text_width) * 0.5,
                        r.y + (r.height - m_font_size) * 0.5
                        }, color);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                double total = 0.0;
                double height = m_font_size + m_padding;
                for (const detail::Text_Run& run : m_runs) {
                    total += run.measured().width + m_padding * 2.0;
                    height = std::max(height, run.measured().height + m_padding);
                }
                if (total <= 0.0) total = 320.0;
                return { total, height };
            }

        private:
            static constexpr double THUMB_INSET = 4.0;

            int selected_index() const {
                if (m_runs.empty()) return -1;
                return std::clamp(m_state.get(), 0, static_cast<int>(m_runs.size()) - 1);
            }

            State<int> m_state;
            std::vector<detail::Text_Run> m_runs;
            double m_font_size;
            double m_padding;
            double m_corner_radius;
            double m_animate_time;
            Color m_text_color;
            Color m_active_text_color;
            Color m_track_color;
            Color m_thumb_color;
            std::vector<coordinate::rect> m_rects;
            Animated m_thumb_x{ 0.0 };
            Animated m_thumb_width{ 0.0 };
            double m_target_x = 0.0;
            double m_target_width = 0.0;
            bool m_placed = false;
        };
        //////////////////////////////////////////////////

        // Collapsible //
        class Collapsible : public Widget {
        public:
            event::Signal<bool> OnToggled;

            Collapsible(Text title, Element content, const Collapsible_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_title(std::move(title))
                , m_run(m_title.get(), props.fontSize)
                , m_content(std::move(content))
                , m_header_padding(props.headerPadding)
                , m_spacing(props.spacing)
                , m_corner_radius(props.cornerRadius)
                , m_animate_time(props.animateTime)
                , m_header_color(props.headerColor)
                , m_header_hover_color(props.headerHoverColor)
                , m_content_color(props.contentColor)
                , m_text_color(props.textColor)
                , m_open(props.defaultOpen)
                , m_progress(props.defaultOpen ? 1.0 : 0.0) {
                watch(m_title, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
                watch(m_progress, [](double) { request_layout(); });
            }

            bool is_open() const { return m_open; }

            void set_open(bool open, bool instant = false) {
                if (open == m_open && !instant) return;
                m_open = open;

                if (instant || m_animate_time <= 0.0) m_progress.set(open ? 1.0 : 0.0);
                else m_progress.animate_to(open ? 1.0 : 0.0, ease(m_animate_time));

                request_layout();
            }

            void toggle() { set_open(!m_open); }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                m_header_height = m_run.measured().height + m_header_padding * 2.0;
                m_header_rect = { bounds.x, bounds.y, bounds.width, m_header_height };
                m_header_mesh = mesh::make_rect(m_header_rect, m_corner_radius);

                m_content_height = 0.0;
                if (m_content) {
                    m_content_height = m_content.get()->desired_for_width(bounds.width).height;
                    const double content_y = bounds.y + m_header_height + m_spacing;
                    m_content.get()->arrange({ bounds.x, content_y, bounds.width, m_content_height });
                }
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    set_open(!m_open);
                    OnToggled.Fire(m_open);
                    return;
                }

                const bool over = !input.consumed && m_header_rect.contains(input.mouse);
                if (over != m_hovered) {
                    m_hovered = over;
                    m_highlight.animate_to(over ? 1.0 : 0.0, ease(HOVER_TIME));
                }

                if (over && input.pressed) {
                    input.consumed = true;
                    set_open(!m_open);
                    OnToggled.Fire(m_open);
                    return;
                }

                if (m_content && m_progress.get() > 0.01) m_content.get()->dispatch(input);
            }

            void draw() const override {
                mesh::draw(m_header_mesh, detail::mix(m_header_color, m_header_hover_color, m_highlight.get()));

                const Size& extent = m_run.measured();
                m_run.draw({
                    m_bounds.x + m_header_padding,
                    m_bounds.y + (m_header_height - extent.height) * 0.5
                    }, m_text_color);

                const double p = m_progress.get();
                const double size = m_run.size() * 0.35;
                const double cx = m_bounds.x + m_bounds.width - m_header_padding - size * 0.5;
                const double cy = m_bounds.y + m_header_height * 0.5;

                const double left_x = cx - size;
                const double right_x = cx + size;
                const double base_y = cy - size * 0.3 + p * size * 0.6;
                const double tip_y = cy + size * 0.6 - p * size * 1.2;

                coordinate::tri_mesh chevron;
                chevron.tris.push_back({ { left_x, base_y },{ right_x, base_y },{ cx, tip_y } });
                chevron.update_bounds();
                mesh::draw(chevron, m_text_color);

                if (m_content && p > 0.001) {
                    const double content_y = m_bounds.y + m_header_height + m_spacing;
                    const double visible = p * m_content_height;
                    if (visible > 0.0) {
                        mesh::draw(mesh::make_rect({ m_bounds.x, content_y, m_bounds.width, visible },
                            m_corner_radius * 0.5), m_content_color);

                        detail::push_clip({ m_bounds.x, content_y, m_bounds.width, visible });
                        m_content.get()->render();
                        detail::pop_clip();
                    }
                }
            }

            bool focusable() const override { return true; }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (is_visible() && is_enabled()) out.push_back(this);
                if (m_open && m_content) m_content.get()->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                const double header = m_run.measured().height + m_header_padding * 2.0;
                const double p = m_progress.get();
                const double body = m_content ? m_content.get()->desired_for_width(width).height : 0.0;
                return { width, header + p * (m_spacing + body) };
            }

        protected:
            Size measure() const override {
                const double header = m_run.measured().height + m_header_padding * 2.0;
                const double p = m_progress.get();
                const double body = m_content ? m_content.get()->desired().height : 0.0;
                return { 0.0, header + p * (m_spacing + body) };
            }

        private:
            static constexpr double HOVER_TIME = 0.12;

            Text m_title;
            detail::Text_Run m_run;
            Element m_content;
            double m_header_padding;
            double m_spacing;
            double m_corner_radius;
            double m_animate_time;
            Color m_header_color;
            Color m_header_hover_color;
            Color m_content_color;
            Color m_text_color;
            bool m_open;
            Animated m_progress;
            Animated m_highlight{ 0.0 };
            coordinate::rect m_header_rect;
            double m_header_height = 0.0;
            double m_content_height = 0.0;
            coordinate::tri_mesh m_header_mesh;
            bool m_hovered = false;
        };
        //////////////////////////////////////////////////

        // Accordion //
        class Accordion : public Widget {
        public:
            event::Signal<int> OnChanged;

            Accordion(State<int> selected, std::vector<Accordion_Item> items, const Accordion_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_selected(std::move(selected))
                , m_gap(props.gap) {
                for (size_t i = 0; i < items.size(); ++i) {
                    auto ref = detail::make_ref<Collapsible>(
                        std::move(items[i].title), std::move(items[i].content),
                        Collapsible_Props{
                            .fontSize = props.fontSize,
                            .animateTime = props.animateTime,
                            .defaultOpen = false,
                            .headerColor = props.headerColor,
                            .headerHoverColor = props.headerHoverColor,
                            .contentColor = props.contentColor,
                            .textColor = props.textColor,
                        });
                    ref->set_open(static_cast<int>(i) == m_selected.get(), /*instant=*/true);
                    attach(ref->OnToggled, [this, i = static_cast<int>(i)](bool open) {
                        if (open) {
                            m_selected.set(i);
                            OnChanged.Fire(i);
                        }
                        else if (m_selected.get() == i) {
                            m_selected.set(-1);
                            OnChanged.Fire(-1);
                        }
                        });
                    m_items.push_back(std::move(ref));
                }

                watch(m_selected, [this](const int& value) {
                    for (size_t i = 0; i < m_items.size(); ++i) {
                        m_items[i]->set_open(static_cast<int>(i) == value);
                    }
                    });
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);

                double y = bounds.y;
                for (auto& item : m_items) {
                    Size s = item->desired_for_width(bounds.width);
                    item->arrange({ bounds.x, y, bounds.width, s.height });
                    y += s.height + m_gap;
                }
            }

            void update(Input_State& input) override {
                for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
                    (*it)->dispatch(input);
                }
            }

            void draw() const override {
                for (const auto& item : m_items) item->render();
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                for (auto& item : m_items) item->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                double total = 0.0;
                for (size_t i = 0; i < m_items.size(); ++i) {
                    total += m_items[i]->desired_for_width(width).height;
                    if (i + 1 < m_items.size()) total += m_gap;
                }
                return { width, fixed_height() >= 0.0 ? fixed_height() : total };
            }

        protected:
            Size measure() const override {
                double height = 0.0;
                double width = 0.0;
                for (size_t i = 0; i < m_items.size(); ++i) {
                    Size s = m_items[i]->desired();
                    height += s.height;
                    width = std::max(width, s.width);
                    if (i + 1 < m_items.size()) height += m_gap;
                }
                return { width, height };
            }

        private:
            State<int> m_selected;
            double m_gap;
            std::vector<Ref<Collapsible>> m_items;
        };
        //////////////////////////////////////////////////

        // Link //
        class Link : public Widget {
        public:
            event::Signal<> OnClick;

            Link(Text text, const Link_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_text(std::move(text))
                , m_run(m_text.get(), props.fontSize)
                , m_underline(props.underlineOnHover)
                , m_color(props.color)
                , m_hover_color(props.hoverColor) {
                watch(m_text, [this](const std::string& value) {
                    if (m_run.set_text(value)) request_layout();
                    });
            }

            void update(Input_State& input) override {
                if (has_focus() && (input.keys.enter || input.keys.space)) {
                    input.keys.enter = input.keys.space = false;
                    OnClick.Fire();
                    return;
                }

                const bool over = !input.consumed && m_mesh.contains(input.mouse);
                m_hovered = over;
                if (over) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

                if (over && input.pressed) {
                    input.consumed = true;
                    OnClick.Fire();
                }
            }

            void draw() const override {
                const Size& extent = m_run.measured();
                const Color color = m_hovered ? m_hover_color : m_color;
                const double y = m_bounds.y + (m_bounds.height - extent.height) * 0.5;

                m_run.draw({ m_bounds.x, y }, color);

                if (m_underline && m_hovered) {
                    mesh::draw(mesh::make_rect({ m_bounds.x, y + extent.height, extent.width, 2.0 }), color);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override { return m_run.measured(); }

        private:
            Text m_text;
            detail::Text_Run m_run;
            bool m_underline;
            Color m_color;
            Color m_hover_color;
            bool m_hovered = false;
        };
        //////////////////////////////////////////////////

        // Rating //
        class Rating : public Widget {
        public:
            event::Signal<int> OnChanged;

            Rating(State<int> state, const Rating_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_state(std::move(state))
                , m_max(std::max(props.max, 1))
                , m_star_size(props.starSize)
                , m_spacing(props.spacing)
                , m_filled_color(props.filledColor)
                , m_empty_color(props.emptyColor) {
            }

            void update(Input_State& input) override {
                if (has_focus()) {
                    if (input.keys.left && m_state.get() > 0) { input.keys.left = false; m_state.set(m_state.get() - 1); OnChanged.Fire(m_state.get()); }
                    if (input.keys.right && m_state.get() < m_max) { input.keys.right = false; m_state.set(m_state.get() + 1); OnChanged.Fire(m_state.get()); }
                }

                if (input.consumed || !m_mesh.contains(input.mouse)) {
                    m_hover_index = -1;
                    return;
                }

                const double step = m_star_size + m_spacing;
                int index = static_cast<int>((input.mouse.x - m_bounds.x) / step);
                index = std::clamp(index, 0, m_max - 1);
                m_hover_index = index;

                if (input.pressed) {
                    input.consumed = true;
                    const int value = index + 1;
                    if (m_state.get() != value) {
                        m_state.set(value);
                        OnChanged.Fire(value);
                    }
                }
            }

            void draw() const override {
                const int current = m_state.get();
                const int preview = m_hover_index >= 0 ? m_hover_index + 1 : current;
                const double cy = m_bounds.y + m_star_size * 0.5;

                for (int i = 0; i < m_max; ++i) {
                    const double cx = m_bounds.x + m_star_size * 0.5 + i * (m_star_size + m_spacing);
                    draw_star({ cx, cy }, m_star_size * 0.5, i < preview ? m_filled_color : m_empty_color);
                }
            }

            bool focusable() const override { return true; }

        protected:
            Size measure() const override {
                const double width = m_max * m_star_size + (m_max - 1) * m_spacing;
                return { width, m_star_size };
            }

        private:
            static constexpr double INNER_RATIO = 0.42;

            void draw_star(coordinate::pos center, double radius, Color color) const {
                constexpr int POINTS = 5;
                std::vector<coordinate::pos> outline;
                outline.reserve(POINTS * 2);
                for (int i = 0; i < POINTS * 2; ++i) {
                    const double r = (i % 2 == 0) ? radius : radius * INNER_RATIO;
                    const double angle = -std::numbers::pi * 0.5 + i * std::numbers::pi / POINTS;
                    outline.push_back({ center.x + std::cos(angle) * r, center.y + std::sin(angle) * r });
                }
                mesh::draw(mesh::from_polygon(outline, center), color);
            }

            State<int> m_state;
            int m_max;
            double m_star_size;
            double m_spacing;
            Color m_filled_color;
            Color m_empty_color;
            int m_hover_index = -1;
        };
        //////////////////////////////////////////////////

        // Grid //
        class Grid : public Widget {
        public:
            Grid(const Grid_Props& props, std::vector<Element> children)
                : Widget(props.width, props.height, props.flex)
                , m_columns(std::max(props.columns, 1))
                , m_spacing_x(props.spacingX)
                , m_spacing_y(props.spacingY)
                , m_children(std::move(children)) {
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (m_children.empty()) return;

                const int cols = m_columns;
                const int rows = (static_cast<int>(m_children.size()) + cols - 1) / cols;
                if (rows <= 0) return;

                const double cell_width = std::max(0.0, (bounds.width - m_spacing_x * (cols - 1)) / cols);
                const double cell_height = std::max(0.0, (bounds.height - m_spacing_y * (rows - 1)) / rows);

                for (size_t i = 0; i < m_children.size(); ++i) {
                    if (!m_children[i]) continue;
                    const int col = static_cast<int>(i) % cols;
                    const int row = static_cast<int>(i) / cols;
                    m_children[i].get()->arrange({
                        bounds.x + col * (cell_width + m_spacing_x),
                        bounds.y + row * (cell_height + m_spacing_y),
                        cell_width,
                        cell_height
                        });
                }
            }

            void update(Input_State& input) override {
                for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
                    if (*it) it->get()->dispatch(input);
                }
            }

            void draw() const override {
                for (const Element& child : m_children) {
                    if (child) child.get()->render();
                }
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                for (const Element& child : m_children) if (child) child.get()->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                if (!is_visible()) return {};
                const int cols = m_columns;
                const double cell_width = std::max(0.0, (width - m_spacing_x * (cols - 1)) / cols);

                double total_height = 0.0;
                int rows_used = 0;
                for (size_t i = 0; i < m_children.size(); i += static_cast<size_t>(cols)) {
                    double row_height = 0.0;
                    for (int c = 0; c < cols && i + static_cast<size_t>(c) < m_children.size(); ++c) {
                        const Element& child = m_children[i + static_cast<size_t>(c)];
                        if (!child || !child.get()->is_visible()) continue;
                        row_height = std::max(row_height, child.get()->desired_for_width(cell_width).height);
                    }
                    total_height += row_height;
                    ++rows_used;
                }
                if (rows_used > 1) total_height += m_spacing_y * (rows_used - 1);

                return { width, fixed_height() >= 0.0 ? fixed_height() : total_height };
            }

        protected:
            Size measure() const override {
                if (m_children.empty()) return {};

                const int cols = m_columns;
                const int rows = (static_cast<int>(m_children.size()) + cols - 1) / cols;

                double max_cell_w = 0.0;
                double max_cell_h = 0.0;
                for (const Element& child : m_children) {
                    if (!child || !child.get()->is_visible()) continue;
                    Size s = child.get()->desired();
                    max_cell_w = std::max(max_cell_w, s.width);
                    max_cell_h = std::max(max_cell_h, s.height);
                }

                return {
                    max_cell_w * cols + m_spacing_x * std::max(cols - 1, 0),
                    max_cell_h * rows + m_spacing_y * std::max(rows - 1, 0)
                };
            }

        private:
            int m_columns;
            double m_spacing_x;
            double m_spacing_y;
            std::vector<Element> m_children;
        };
        //////////////////////////////////////////////////

        // Reveal //
        class Reveal : public Widget {
        public:
            Reveal(State<bool> visible, Element child, const Reveal_Props& props)
                : Widget(props.width, props.height, props.flex)
                , m_visible_state(std::move(visible))
                , m_child(std::move(child))
                , m_enter_time(props.enterTime)
                , m_exit_time(props.exitTime)
                , m_enter_offset_y(props.enterOffsetY)
                , m_exit_offset_y(props.exitOffsetY)
                , m_progress(m_visible_state.get() ? 1.0 : 0.0) {
                set_opacity(m_progress.get());

                watch(m_progress, [this](double v) {
                    set_opacity(std::clamp(v, 0.0, 1.0));
                    request_layout();
                    });

                watch(m_visible_state, [this](const bool& v) {
                    m_progress.animate_to(v ? 1.0 : 0.0, ease(v ? m_enter_time : m_exit_time));
                    });
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (!m_child) return;

                const double p = m_progress.get();
                const double offset = p >= 0.5
                    ? (1.0 - p) * m_enter_offset_y
                    : (1.0 - p) * m_exit_offset_y;

                m_child.get()->arrange({ bounds.x, bounds.y + offset, bounds.width, bounds.height });
            }

            void update(Input_State& input) override {
                if (m_progress.get() > 0.5 && m_child) m_child.get()->dispatch(input);
            }

            void draw() const override {
                if (m_child) m_child.get()->render();
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (m_progress.get() > 0.5 && m_child) m_child.get()->collect_focusables(out);
            }

            Size desired_for_width(double width) const override {
                if (m_progress.get() <= 0.001) return {};
                return m_child ? m_child.get()->desired_for_width(width) : Size{};
            }

        protected:
            Size measure() const override {
                if (m_progress.get() <= 0.001) return {};
                return m_child ? m_child.get()->desired() : Size{};
            }

        private:
            State<bool> m_visible_state;
            Element m_child;
            double m_enter_time;
            double m_exit_time;
            double m_enter_offset_y;
            double m_exit_offset_y;
            Animated m_progress;
        };
        //////////////////////////////////////////////////

        // Popover //
        class Popover : public Widget {
        public:
            Popover(State<bool> open, Element trigger, Element content, const Popover_Props& props)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_open_state(std::move(open))
                , m_trigger(std::move(trigger))
                , m_content(std::move(content))
                , m_side(props.side)
                , m_gap(props.gap)
                , m_corner_radius(props.cornerRadius)
                , m_padding(props.padding)
                , m_close_on_outside(props.closeOnOutsideClick)
                , m_close_on_escape(props.closeOnEscape)
                , m_background(props.backgroundColor)
                , m_content_width(props.width)
                , m_content_height(props.height) {
                if (m_open_state.get()) Internal::popup = this;

                watch(m_open_state, [this](const bool& v) {
                    if (v) {
                        if (Internal::popup && Internal::popup != this) Internal::popup->close_popup();
                        Internal::popup = this;
                    }
                    else if (Internal::popup == this) {
                        Internal::popup = nullptr;
                    }
                    });
            }

            ~Popover() override {
                if (Internal::popup == this) Internal::popup = nullptr;
            }

            bool is_open() const { return m_open_state.get(); }

            void close_popup() override { m_open_state.set(false); }

            double flex_weight() const override { return m_trigger ? m_trigger.get()->flex_weight() : 0.0; }

            Size desired_for_width(double width) const override {
                return m_trigger ? m_trigger.get()->desired_for_width(width) : Size{};
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (m_trigger) m_trigger.get()->arrange(bounds);

                if (m_content) {
                    Size natural = m_content.get()->desired();
                    double w = m_content_width >= 0.0 ? m_content_width : natural.width + m_padding * 2.0;
                    double h = m_content_height >= 0.0 ? m_content_height : m_content.get()->desired_for_width(w - m_padding * 2.0).height + m_padding * 2.0;

                    double x = bounds.x;
                    double y = bounds.y + bounds.height + m_gap;
                    switch (m_side) {
                    case Side::Top: y = bounds.y - h - m_gap; break;
                    case Side::Bottom: y = bounds.y + bounds.height + m_gap; break;
                    case Side::Left: x = bounds.x - w - m_gap; y = bounds.y; break;
                    case Side::Right: x = bounds.x + bounds.width + m_gap; y = bounds.y; break;
                    }

                    const double canvas_w = Internal::mapper.get_width();
                    const double canvas_h = Internal::mapper.get_height();
                    if (x < 0.0) x = 0.0;
                    if (x + w > canvas_w) x = canvas_w - w;
                    if (y < 0.0) y = 0.0;
                    if (y + h > canvas_h) y = canvas_h - h;

                    m_popup_bounds = { x, y, w, h };
                    m_content.get()->arrange({
                        x + m_padding,
                        y + m_padding,
                        std::max(0.0, w - m_padding * 2.0),
                        std::max(0.0, h - m_padding * 2.0)
                        });
                }
            }

            void update(Input_State& input) override {
                if (input.inactive) { close_popup(); return; }
                if (m_trigger) m_trigger.get()->dispatch(input);
            }

            void update_popup(Input_State& input) override {
                if (!is_open()) return;

                const bool inside = m_popup_bounds.contains(input.mouse);

                if (!inside && m_close_on_outside && input.pressed) {
                    input.consumed = true;
                    close_popup();
                    return;
                }
                if (m_close_on_escape && input.keys.escape) {
                    input.keys.escape = false;
                    close_popup();
                    return;
                }

                if (m_content) {
                    Input_State inner = input;
                    inner.consumed = false;
                    if (!inside) { inner.mouse = { -1.0e9, -1.0e9 }; }
                    m_content.get()->dispatch(inner);
                    input.consumed = true;
                    input.wheel = 0.0;
                }
            }

            void draw() const override {
                if (m_trigger) m_trigger.get()->render();
                if (is_open()) queue_overlay();
            }

            void draw_overlay() const override {
                if (!is_open()) return;

                mesh::drop_shadow(m_popup_bounds, m_corner_radius, 14.0, 0.4);
                mesh::draw(mesh::make_rect(m_popup_bounds, m_corner_radius), m_background);

                if (m_content) {
                    detail::push_clip(m_popup_bounds);
                    m_content.get()->render();
                    detail::pop_clip();
                }
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (m_trigger) m_trigger.get()->collect_focusables(out);
                if (is_open() && m_content) m_content.get()->collect_focusables(out);
            }

        protected:
            Size measure() const override {
                return m_trigger ? m_trigger.get()->desired() : Size{};
            }

        private:
            State<bool> m_open_state;
            Element m_trigger;
            Element m_content;
            Side m_side;
            double m_gap;
            double m_corner_radius;
            double m_padding;
            bool m_close_on_outside;
            bool m_close_on_escape;
            Color m_background;
            double m_content_width;
            double m_content_height;
            coordinate::rect m_popup_bounds;
        };
        //////////////////////////////////////////////////

        // Modal //
        class Modal : public Widget {
        public:
            Modal(State<bool> open, Element content, const Modal_Props& props)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_open_state(std::move(open))
                , m_content(std::move(content))
                , m_dismiss_on_backdrop(props.dismissOnBackdrop)
                , m_dismiss_on_escape(props.dismissOnEscape)
                , m_padding(props.padding)
                , m_corner_radius(props.cornerRadius)
                , m_backdrop_color(props.backdropColor)
                , m_content_color(props.contentColor)
                , m_content_width(props.contentWidth)
                , m_content_height(props.contentHeight)
                , m_fade_time(props.fadeTime)
                , m_fade(m_open_state.get() ? 1.0 : 0.0) {
                set_visible(m_open_state.get());
                set_opacity(m_fade.get());
                if (m_open_state.get()) Internal::modal = this;

                watch(m_fade, [this](double v) {
                    set_opacity(std::clamp(v, 0.0, 1.0));
                    request_layout();
                    });

                watch(m_open_state, [this](const bool& v) {
                    if (v) {
                        set_visible(true);
                        Internal::modal = this;
                        m_fade.animate_to(1.0, ease(m_fade_time));
                    }
                    else {
                        if (Internal::modal == this) Internal::modal = nullptr;
                        m_fade.animate_to(0.0, ease(m_fade_time));
                    }
                    });

                m_fade.OnFinished().Connect([this]() {
                    if (!m_open_state.get()) {
                        set_visible(false);
                        if (Internal::modal == this) Internal::modal = nullptr;
                    }
                    });
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (!m_content) return;

                const double canvas_w = Internal::mapper.get_width();
                const double canvas_h = Internal::mapper.get_height();

                double w = m_content_width >= 0.0 ? m_content_width : canvas_w * 0.6;
                double h;
                if (m_content_height >= 0.0) h = m_content_height;
                else h = m_content.get()->desired_for_width(std::max(0.0, w - m_padding * 2.0)).height + m_padding * 2.0;

                m_panel_bounds = {
                    (canvas_w - w) * 0.5,
                    (canvas_h - h) * 0.5,
                    w, h
                };
                m_content.get()->arrange({
                    m_panel_bounds.x + m_padding,
                    m_panel_bounds.y + m_padding,
                    std::max(0.0, w - m_padding * 2.0),
                    std::max(0.0, h - m_padding * 2.0)
                    });
            }

            void update(Input_State& input) override {
                if (!is_visible()) return;

                if (m_dismiss_on_escape && input.keys.escape) {
                    input.keys.escape = false;
                    m_open_state.set(false);
                    input.consumed = true;
                    return;
                }

                if (m_dismiss_on_backdrop && input.pressed && !input.consumed && !m_panel_bounds.contains(input.mouse)) {
                    m_open_state.set(false);
                    input.consumed = true;
                    return;
                }

                if (m_content) {
                    input.consumed = false;
                    m_content.get()->dispatch(input);
                }

                input.consumed = true;
                input.wheel = 0.0;
            }

            void draw() const override {
                if (is_visible()) queue_overlay();
            }

            void draw_overlay() const override {
                if (!is_visible()) return;

                const double canvas_w = Internal::mapper.get_width();
                const double canvas_h = Internal::mapper.get_height();

                mesh::draw(mesh::make_rect({ 0.0, 0.0, canvas_w, canvas_h }), m_backdrop_color);
                mesh::drop_shadow(m_panel_bounds, m_corner_radius, 28.0, 0.7);
                mesh::draw(mesh::make_rect(m_panel_bounds, m_corner_radius), m_content_color);

                if (m_content) {
                    detail::push_clip(m_panel_bounds);
                    m_content.get()->render();
                    detail::pop_clip();
                }
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (is_visible() && m_content) m_content.get()->collect_focusables(out);
            }

        protected:
            Size measure() const override { return {}; }

        private:
            State<bool> m_open_state;
            Element m_content;
            bool m_dismiss_on_backdrop;
            bool m_dismiss_on_escape;
            double m_padding;
            double m_corner_radius;
            Color m_backdrop_color;
            Color m_content_color;
            double m_content_width;
            double m_content_height;
            double m_fade_time;
            Animated m_fade;
            coordinate::rect m_panel_bounds;
        };
        //////////////////////////////////////////////////

        // ContextMenu //
        class ContextMenu : public Widget {
        public:
            event::Signal<int> OnSelect;

            ContextMenu(State<int> selected, std::vector<Text> items, Element trigger, const ContextMenu_Props& props)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_selected(std::move(selected))
                , m_items(std::move(items))
                , m_trigger(std::move(trigger))
                , m_font_size(props.fontSize)
                , m_padding(props.padding)
                , m_row_padding(props.rowPadding)
                , m_corner_radius(props.cornerRadius)
                , m_animate_time(props.animateTime)
                , m_scale_from(std::clamp(props.scaleFrom, 0.0, 1.0))
                , m_text_color(props.textColor)
                , m_background(props.backgroundColor)
                , m_hover_color(props.hoverColor) {
                for (Text& item : m_items) {
                    m_runs.emplace_back(item.get(), m_font_size);
                    watch(item, [this, i = m_runs.size() - 1](const std::string& value) {
                        if (m_runs[i].set_text(value)) request_layout();
                        });
                }

                attach(m_fade.OnFinished(), [this]() {
                    if (!m_open) {
                        m_active = false;
                        if (Internal::popup == this) Internal::popup = nullptr;
                    }
                    });
            }

            ~ContextMenu() override {
                if (Internal::popup == this) Internal::popup = nullptr;
            }

            bool is_open() const { return m_open; }
            void close_popup() override { close(); }

            double flex_weight() const override { return m_trigger ? m_trigger.get()->flex_weight() : 0.0; }

            Size desired_for_width(double width) const override {
                return m_trigger ? m_trigger.get()->desired_for_width(width) : Size{};
            }

            void arrange(const coordinate::rect& bounds) override {
                Widget::arrange(bounds);
                if (m_trigger) m_trigger.get()->arrange(bounds);
                if (m_active) layout_menu();
            }

            void update(Input_State& input) override {
                if (input.inactive) { close(true); return; }

                if (m_trigger) m_trigger.get()->dispatch(input);

                if (m_open) return;

                const bool over = !input.consumed && m_bounds.contains(input.mouse);
                if (over && input.right_pressed) {
                    input.consumed = true;
                    open_at(input.mouse);
                }
            }

            void update_popup(Input_State& input) override {
                if (!m_active) return;
                if (!m_open) return;

                if (m_menu_bounds.contains(input.mouse)) {
                    input.consumed = true;

                    const double local = input.mouse.y - m_menu_bounds.y - outer_pad();
                    long row = -1;
                    if (local >= 0.0) {
                        row = static_cast<long>(std::floor(local / row_height()));
                        if (row < 0 || row >= static_cast<long>(m_items.size())) row = -1;
                    }
                    m_hover_index = static_cast<int>(row);

                    if (input.pressed && row >= 0) {
                        m_selected.set(static_cast<int>(row));
                        OnSelect.Fire(static_cast<int>(row));
                        close();
                        return;
                    }
                }
                else if (input.pressed || input.right_pressed) {
                    input.consumed = true;
                    close();
                    return;
                }

                if (input.keys.escape) {
                    input.keys.escape = false;
                    close();
                }
            }

            void draw() const override {
                if (m_trigger) m_trigger.get()->render();
                if (m_active) queue_overlay();
            }

            void draw_overlay() const override {
                if (!m_active) return;
                const double p = m_fade.get();
                if (p <= 0.001) return;

                const double scale = m_scale_from + (1.0 - m_scale_from) * p;
                const coordinate::rect scaled{
                    m_menu_bounds.x,
                    m_menu_bounds.y,
                    m_menu_bounds.width * scale,
                    m_menu_bounds.height * scale
                };

                const double previous = Internal::opacity;
                Internal::opacity *= p;

                mesh::drop_shadow(scaled, m_corner_radius * scale, 12.0, 0.4);
                mesh::draw(mesh::make_rect(scaled, m_corner_radius * scale), m_background);

                detail::push_clip(scaled);
                for (size_t i = 0; i < m_runs.size(); ++i) {
                    const double row_top = scaled.y + outer_pad() * scale + static_cast<double>(i) * row_height() * scale - 0.5;
                    if (static_cast<int>(i) == m_hover_index) {
                        mesh::draw(mesh::make_rect({
                            scaled.x + 4.0 * scale,
                            row_top,
                            scaled.width - 8.0 * scale,
                            row_height() * scale
                            }, m_corner_radius * 0.6 * scale), m_hover_color);
                    }

                    if (p > 0.35) {
                        const double text_y = m_menu_bounds.y + outer_pad() + static_cast<double>(i) * row_height()
                            + (row_height() - m_font_size) * 0.5;
                        m_runs[i].draw({ m_menu_bounds.x + m_padding, text_y }, m_text_color);
                    }
                }
                detail::pop_clip();

                Internal::opacity = previous;
            }

            void collect_focusables(std::vector<Widget*>& out) override {
                if (m_trigger) m_trigger.get()->collect_focusables(out);
            }

        protected:
            Size measure() const override {
                return m_trigger ? m_trigger.get()->desired() : Size{};
            }

        private:
            double row_height() const { return m_font_size + m_row_padding * 2.0; }

            double outer_pad() const { return std::max(0.0, m_padding - m_row_padding); }

            void layout_menu() {
                double width = 0.0;
                for (const auto& run : m_runs) width = std::max(width, run.measured().width);
                width += m_padding * 2.0;

                double height = static_cast<double>(m_items.size()) * row_height() + outer_pad() * 2.0;

                double x = m_open_pos.x;
                double y = m_open_pos.y;

                const double canvas_w = Internal::mapper.get_width();
                const double canvas_h = Internal::mapper.get_height();
                if (x + width > canvas_w) x = canvas_w - width;
                if (y + height > canvas_h) y = canvas_h - height;
                x = std::max(x, 0.0);
                y = std::max(y, 0.0);

                m_menu_bounds = { x, y, width, height };
            }

            void open_at(coordinate::pos mouse) {
                if (Internal::popup && Internal::popup != this) Internal::popup->close_popup();

                m_open = true;
                m_active = true;
                m_open_pos = mouse;
                m_hover_index = -1;
                Internal::popup = this;
                layout_menu();
                m_fade.animate_to(1.0, ease(m_animate_time));
            }

            void close(bool instant = false) {
                if (!m_open && !m_active) return;
                m_open = false;

                if (instant || m_animate_time <= 0.0) {
                    m_fade.set(0.0);
                    m_active = false;
                    if (Internal::popup == this) Internal::popup = nullptr;
                    return;
                }

                m_fade.animate_to(0.0, ease(m_animate_time));
            }

            State<int> m_selected;
            std::vector<Text> m_items;
            std::vector<detail::Text_Run> m_runs;
            Element m_trigger;
            double m_font_size;
            double m_padding;
            double m_row_padding;
            double m_corner_radius;
            double m_animate_time;
            double m_scale_from;
            Color m_text_color;
            Color m_background;
            Color m_hover_color;
            coordinate::rect m_menu_bounds;
            coordinate::pos m_open_pos;
            int m_hover_index = -1;
            bool m_open = false;
            bool m_active = false;
            Animated m_fade{ 0.0 };
        };
        //////////////////////////////////////////////////

        // Toast host //
        class Toast_Host : public Widget {
        public:
            Toast_Host(const Toast_Host_Props& props)
                : Widget(AUTO_SIZE, AUTO_SIZE, 0.0)
                , m_props(props) {
            }

            void update(Input_State&) override {
                const double dt = GetFrameTime();

                for (auto& data : detail::toasts) {
                    if (data->age == 0.0) data->fade.animate_to(1.0, ease(m_props.fadeInTime));
                    data->age += dt;
                }

                detail::toasts.erase(
                    std::remove_if(detail::toasts.begin(), detail::toasts.end(),
                        [this](const std::shared_ptr<detail::Toast_Data>& d) {
                            if (d->age > d->duration) {
                                if (!d->fade.is_animating() && d->fade.get() > 0.5) {
                                    d->fade.animate_to(0.0, ease(m_props.fadeOutTime));
                                }
                                return d->age > d->duration + m_props.fadeOutTime + 0.05;
                            }
                            return false;
                        }),
                    detail::toasts.end());
            }

            void draw() const override {
                if (!detail::toasts.empty()) queue_overlay();
            }

            void draw_overlay() const override {
                if (detail::toasts.empty()) return;

                const double canvas_w = Internal::mapper.get_width();
                const double canvas_h = Internal::mapper.get_height();

                struct Entry {
                    std::string text;
                    Color color;
                    double width;
                    double height;
                    double padding;
                    double fontSize;
                    double lineHeight;
                    double cornerRadius;
                    double fade;
                    std::vector<detail::Text_Line> lines;
                };
                std::vector<Entry> entries;
                entries.reserve(detail::toasts.size());

                for (const auto& d : detail::toasts) {
                    // Wrap once, at the max width. Short toasts wrap to their natural size.
                    auto lines = detail::wrap_text(d->text, d->maxWidth - d->padding * 2.0, d->fontSize);

                    double w = 0.0;
                    for (const auto& line : lines) w = std::max(w, line.width);
                    w += d->padding * 2.0;

                    double h = static_cast<double>(lines.size()) * d->fontSize * d->lineHeight + d->padding * 2.0;

                    entries.push_back({
                        d->text, d->color, w, h,
                        d->padding, d->fontSize, d->lineHeight, d->cornerRadius,
                        d->fade.get(), std::move(lines)
                        });
                }

                double total = 0.0;
                for (const auto& e : entries) total += e.height + m_props.gap;
                if (!entries.empty()) total -= m_props.gap;

                double base_x = 0.0;
                double base_y = 0.0;
                switch (m_props.anchor) {
                case Anchor::TopLeft: base_x = m_props.marginX; base_y = m_props.marginY; break;
                case Anchor::Top: base_x = (canvas_w - entries[0].width) * 0.5; base_y = m_props.marginY; break;
                case Anchor::TopRight: base_x = canvas_w - m_props.marginX; base_y = m_props.marginY; break;
                case Anchor::Left: base_x = m_props.marginX; base_y = (canvas_h - total) * 0.5; break;
                case Anchor::Center: base_x = (canvas_w - entries[0].width) * 0.5; base_y = (canvas_h - total) * 0.5; break;
                case Anchor::Right: base_x = canvas_w - m_props.marginX; base_y = (canvas_h - total) * 0.5; break;
                case Anchor::BottomLeft: base_x = m_props.marginX; base_y = canvas_h - m_props.marginY - total; break;
                case Anchor::Bottom: base_x = (canvas_w - entries[0].width) * 0.5; base_y = canvas_h - m_props.marginY - total; break;
                case Anchor::BottomRight: base_x = canvas_w - m_props.marginX; base_y = canvas_h - m_props.marginY - total; break;
                }

                double cursor_y = base_y;
                for (const auto& e : entries) {
                    double x = base_x;
                    if (m_props.anchor == Anchor::TopRight || m_props.anchor == Anchor::Right || m_props.anchor == Anchor::BottomRight) {
                        x -= e.width;
                    }

                    const double previous = Internal::opacity;
                    Internal::opacity *= e.fade;

                    mesh::drop_shadow({ x, cursor_y, e.width, e.height }, e.cornerRadius, 12.0, 0.4);
                    mesh::draw(mesh::make_rect({ x, cursor_y, e.width, e.height }, e.cornerRadius), e.color);

                    const double step = e.fontSize * e.lineHeight;
                    for (size_t i = 0; i < e.lines.size(); ++i) {
                        std::string piece = e.text.substr(e.lines[i].start, e.lines[i].end - e.lines[i].start);
                        const double visual = font.measure(piece, e.fontSize).height;
                        const double glyph_offset = (step - visual) * 0.5;

                        font.draw(
                            piece,
                            { x + e.padding,
                              cursor_y + e.padding + static_cast<double>(i) * step + glyph_offset },
                            e.fontSize, WHITE);
                    }

                    Internal::opacity = previous;
                    cursor_y += e.height + m_props.gap;
                }
            }

        protected:
            Size measure() const override { return {}; }

        private:
            Toast_Host_Props m_props;
        };
        //////////////////////////////////////////////////

    }

}