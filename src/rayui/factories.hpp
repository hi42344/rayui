#pragma once

#include "widgets.hpp"

namespace rayui {

    inline void toast(const std::string& text, const Toast_Props& props = {}) {
        auto data = std::make_shared<detail::Toast_Data>();
        data->text = text;
        data->level = props.level;
        data->color = (props.color.a > 0) ? props.color
            : (props.level == Toast_Level::Success ? props.successColor
                : props.level == Toast_Level::Warning ? props.warningColor
                : props.level == Toast_Level::Error ? props.errorColor
                : props.infoColor);
        data->duration = props.duration;
        data->fontSize = props.fontSize;
        data->padding = props.padding;
        data->lineHeight = props.lineHeight;
        data->cornerRadius = props.cornerRadius;
        data->maxWidth = props.maxWidth;
        detail::toasts.push_back(std::move(data));
    }

    inline Table_Column Column(Text header, const Column_Props& props = {}) {
        Table_Column c;
        c.header = std::move(header);
        c.width = props.width;
        c.flex = props.flex;
        c.align = props.align;
        return c;
    }

    inline Ref<widget::Table> Table(std::vector<Table_Column> columns, std::vector<Table_Row> rows,
        const Table_Props& props = {}) {
        return detail::make_ref<widget::Table>(std::move(columns), std::move(rows), props);
    }

    inline Ref<widget::Panel> Panel(const Panel_Props& props, std::initializer_list<Element> children) {
        return detail::make_ref<widget::Panel>(props, std::vector<Element>(children));
    }

    inline Ref<widget::Stack> VStack(const Stack_Props& props, std::initializer_list<Element> children) {
        return detail::make_ref<widget::Stack>(Axis::Vertical, props, std::vector<Element>(children));
    }

    inline Ref<widget::Stack> HStack(const Stack_Props& props, std::initializer_list<Element> children) {
        return detail::make_ref<widget::Stack>(Axis::Horizontal, props, std::vector<Element>(children));
    }

    template <typename T, typename Builder>
        requires std::invocable<Builder&, const T&>&&
    std::convertible_to<std::invoke_result_t<Builder&, const T&>, Element>
        inline Ref<widget::ForEach_View> ForEach(State<std::vector<T>> source, Builder builder,
            const Stack_Props& props = { .spacing = 8.0 }) {
        return detail::make_ref<widget::ForEach_View>(std::move(source), std::move(builder), props);
    }

    inline Ref<widget::Panel> Panel(const Panel_Props& props, std::vector<Element> children) {
        return detail::make_ref<widget::Panel>(props, std::move(children));
    }

    inline Ref<widget::Stack> VStack(const Stack_Props& props, std::vector<Element> children) {
        return detail::make_ref<widget::Stack>(Axis::Vertical, props, std::move(children));
    }

    inline Ref<widget::Stack> HStack(const Stack_Props& props, std::vector<Element> children) {
        return detail::make_ref<widget::Stack>(Axis::Horizontal, props, std::move(children));
    }

    inline Ref<widget::Wrap> Wrap(const Wrap_Props& props, std::vector<Element> children) {
        return detail::make_ref<widget::Wrap>(props, std::move(children));
    }

    inline Ref<widget::Wrap> Wrap(const Wrap_Props& props, std::initializer_list<Element> children) {
        return detail::make_ref<widget::Wrap>(props, std::vector<Element>(children));
    }

    inline Ref<widget::Spacer> Spacer(const Spacer_Props& props = {}) {
        return detail::make_ref<widget::Spacer>(props);
    }

    inline Ref<widget::Label> Label(Text text, const Label_Props& props = {}) {
        return detail::make_ref<widget::Label>(std::move(text), props);
    }

    template <typename T, typename Format>
        requires std::invocable<Format&, const T&>&&
    std::convertible_to<std::invoke_result_t<Format&, const T&>, std::string>
        inline Ref<widget::Label> Label(State<T> state, Format format, const Label_Props& props = {}) {
        return Label(Text::from(std::move(state), std::move(format)), props);
    }

    inline Ref<widget::Button> Button(Text text, const Button_Props& props = {}) {
        return detail::make_ref<widget::Button>(std::move(text), props);
    }

    template <typename T> requires std::is_arithmetic_v<T>
    inline Ref<widget::Slider> Slider(State<T> state, std::type_identity_t<T> min_value,
        std::type_identity_t<T> max_value, const Slider_Props& props = {}) {
        auto getter = [state]() { return static_cast<double>(state.get()); };
        auto setter = [state](double value) {
            if constexpr (std::is_integral_v<T>) state.set(static_cast<T>(std::llround(value)));
            else state.set(static_cast<T>(value));
            };

        auto ref = detail::make_ref<widget::Slider>(getter, setter, static_cast<double>(min_value),
            static_cast<double>(max_value), props);
        ref->watch(state, [w = ref.operator->()](const T&) { w->refresh(); });
        return ref;
    }

    inline Ref<widget::Checkbox> Checkbox(State<bool> state, Text label = {}, const Checkbox_Props& props = {}) {
        auto getter = [state]() { return state.get(); };
        auto setter = [state](bool value) { state.set(value); };
        return detail::make_ref<widget::Checkbox>(getter, setter, std::move(label), props);
    }

    inline Ref<widget::Positioned> Positioned(const Positioned_Props& props, Element child) {
        return detail::make_ref<widget::Positioned>(props, std::move(child));
    }

    inline Ref<widget::Textbox> Textbox(State<std::string> state, const Textbox_Props& props = {}) {
        return detail::make_ref<widget::Textbox>(std::move(state), props);
    }

    template <typename T> requires std::is_arithmetic_v<T>
    inline Ref<widget::Number_Input> NumberInput(State<T> state,
        const Number_Input_Props& props = {}) {
        auto getter = [state]() { return static_cast<double>(state.get()); };
        auto setter = [state](double v) {
            if constexpr (std::is_integral_v<T>) state.set(static_cast<T>(std::llround(v)));
            else state.set(static_cast<T>(v));
            };
        auto ref = detail::make_ref<widget::Number_Input>(getter, setter, props);
        ref->watch(state, [w = ref.operator->()](const T&) { w->sync(); });
        return ref;
    }

    inline Ref<widget::Divider> Divider(const Divider_Props& props = {}) {
        return detail::make_ref<widget::Divider>(props);
    }

    inline Ref<widget::Toggle> Toggle(State<bool> state, const Toggle_Props& props = {}) {
        auto getter = [state]() { return state.get(); };
        auto setter = [state](bool value) { state.set(value); };

        auto ref = detail::make_ref<widget::Toggle>(getter, setter, props);
        ref->watch(state, [w = ref.operator->()](const bool&) { w->sync(); });
        return ref;
    }

    inline Ref<widget::Radio> Radio(State<int> state, int value, Text label = {}, const Radio_Props& props = {}) {
        auto selected = [state, value]() { return state.get() == value; };
        auto select = [state, value]() { state.set(value); };
        return detail::make_ref<widget::Radio>(selected, select, std::move(label), props);
    }

    inline Ref<widget::Stack> RadioGroup(State<int> state, std::vector<Text> labels,
        const Stack_Props& layout = { .spacing = 16.0 }, const Radio_Props& props = {}) {
        std::vector<Element> rows;
        rows.reserve(labels.size());
        for (size_t i = 0; i < labels.size(); ++i) rows.push_back(Radio(state, static_cast<int>(i), labels[i], props));
        return VStack(layout, std::move(rows));
    }

    inline Ref<widget::Switch> Switch(State<int> state, std::vector<Element> pages) {
        auto ref = detail::make_ref<widget::Switch>(std::move(pages), 0.0);
        ref->set_index(state.get());
        ref->watch(state, [w = ref.operator->()](const int& value) { w->set_index(value); });
        return ref;
    }

    inline Ref<widget::Switch> Switch(State<int> state, std::initializer_list<Element> pages) {
        return Switch(std::move(state), std::vector<Element>(pages));
    }

    struct Tab_Page {
        Text title;
        Element content;
    };

    inline Tab_Page Tab(Text title, Element content) {
        return Tab_Page{ std::move(title), std::move(content) };
    }

    inline Ref<widget::Stack> Tabs(State<int> selected, std::vector<Tab_Page> tabs, const Tabs_Props& props = {}) {
        std::vector<Text> titles;
        std::vector<Element> pages;
        for (Tab_Page& tab : tabs) {
            titles.push_back(std::move(tab.title));
            pages.push_back(std::move(tab.content));
        }

        auto bar = detail::make_ref<widget::Tab_Bar>(selected, std::move(titles), props);
        auto body = detail::make_ref<widget::Switch>(std::move(pages), 1.0);
        body->set_index(selected.get());
        body->watch(selected, [w = body.operator->()](const int& value) { w->set_index(value); });

        return VStack({ .spacing = props.gap, .alignment = Align::Stretch, .width = props.width, .height = props.height, .flex = props.flex },
            std::vector<Element>{ bar, body });
    }

    inline Ref<widget::Dropdown> Dropdown(State<int> state, std::vector<Text> options, const Dropdown_Props& props = {}) {
        return detail::make_ref<widget::Dropdown>(std::move(state), std::move(options), props);
    }

    inline Ref<widget::Tooltip> Tooltip(Text text, Element child, const Tooltip_Props& props = {}) {
        return detail::make_ref<widget::Tooltip>(std::move(text), std::move(child), props);
    }

    inline Ref<widget::Image_View> Image(Texture2D texture, const Image_Props& props = {}) {
        return detail::make_ref<widget::Image_View>(texture, props);
    }

    inline Ref<widget::Text_Display> TextDisplay(Text text, const Text_Display_Props& props = {}) {
        return detail::make_ref<widget::Text_Display>(std::move(text), props);
    }

    inline Ref<widget::Text_Field> TextField(State<std::string> state, const Text_Field_Props& props = {}) {
        return detail::make_ref<widget::Text_Field>(std::move(state), props);
    }

    template <typename T> requires std::is_arithmetic_v<T>
    inline Ref<widget::Progress_Bar> ProgressBar(State<T> state, std::type_identity_t<T> min_value,
        std::type_identity_t<T> max_value, const Progress_Props& props = {}) {
        const double low = static_cast<double>(min_value);
        const double high = static_cast<double>(max_value);
        auto to_fraction = [low, high](T value) {
            return high == low ? 0.0 : (static_cast<double>(value) - low) / (high - low);
            };

        auto ref = detail::make_ref<widget::Progress_Bar>(to_fraction(state.get()), props);
        ref->watch(state, [w = ref.operator->(), to_fraction](const T& value) { w->set_fraction(to_fraction(value)); });
        return ref;
    }

    inline Ref<widget::Progress_Bar> ProgressBar(double fraction, const Progress_Props& props = {}) {
        return detail::make_ref<widget::Progress_Bar>(fraction, props);
    }

    inline Ref<widget::Scroll_View> ScrollView(const Scroll_Props& props, Element content) {
        return detail::make_ref<widget::Scroll_View>(props, std::move(content));
    }

    inline Ref<widget::Cond> Cond(State<bool> state, Element when_true, Element when_false = {}) {
        auto ref = detail::make_ref<widget::Cond>(std::move(when_true), std::move(when_false));
        ref->set_active(state.get());
        ref->watch(state, [w = ref.operator->()](const bool& value) { w->set_active(value); });
        return ref;
    }

    template <typename T, typename Pred> requires std::predicate<Pred&, const T&>
    inline Ref<widget::Cond> Cond(State<T> state, Pred pred, Element when_true, Element when_false = {}) {
        auto ref = detail::make_ref<widget::Cond>(std::move(when_true), std::move(when_false));
        ref->set_active(pred(state.get()));
        ref->watch(state, [w = ref.operator->(), pred](const T& value) { w->set_active(pred(value)); });
        return ref;
    }

    inline Ref<widget::Badge> Badge(Text text, const Badge_Props& props = {}) {
        return detail::make_ref<widget::Badge>(std::move(text), props);
    }

    inline Ref<widget::Chip> Chip(Text text, const Chip_Props& props = {}) {
        return detail::make_ref<widget::Chip>(std::move(text), props);
    }

    inline Ref<widget::Segmented_Control> SegmentedControl(State<int> state, std::vector<Text> options,
        const Segmented_Props& props = {}) {
        return detail::make_ref<widget::Segmented_Control>(std::move(state), std::move(options), props);
    }

    inline Ref<widget::Collapsible> Collapsible(Text title, Element content, const Collapsible_Props& props = {}) {
        return detail::make_ref<widget::Collapsible>(std::move(title), std::move(content), props);
    }

    inline Accordion_Item AccordionItem(Text title, Element content) {
        return Accordion_Item{ std::move(title), std::move(content) };
    }

    inline Ref<widget::Accordion> Accordion(State<int> selected, std::vector<Accordion_Item> items,
        const Accordion_Props& props = {}) {
        return detail::make_ref<widget::Accordion>(std::move(selected), std::move(items), props);
    }

    inline Ref<widget::Link> Link(Text text, const Link_Props& props = {}) {
        return detail::make_ref<widget::Link>(std::move(text), props);
    }

    inline Ref<widget::Rating> Rating(State<int> state, const Rating_Props& props = {}) {
        return detail::make_ref<widget::Rating>(std::move(state), props);
    }

    inline Ref<widget::Grid> Grid(const Grid_Props& props, std::vector<Element> children) {
        return detail::make_ref<widget::Grid>(props, std::move(children));
    }

    inline Ref<widget::Grid> Grid(const Grid_Props& props, std::initializer_list<Element> children) {
        return detail::make_ref<widget::Grid>(props, std::vector<Element>(children));
    }

    // ---- Builder / Shape ----

    inline Ref<widget::Shape> Custom(const Shape_Props& props, std::function<void(const coordinate::rect&)> draw) {
        auto ref = detail::make_ref<widget::Shape>(props);
        ref->set_draw(std::move(draw));
        return ref;
    }

    inline Ref<widget::Shape> Rect(double width, double height, Color color, double radius = 0.0) {
        return Custom({ .width = width, .height = height },
            [color, radius](const coordinate::rect& r) {
                mesh::draw(mesh::make_rect(r, radius), color);
            });
    }

    inline Ref<widget::Shape> Circle(double radius, Color color) {
        return Custom({ .width = radius * 2.0, .height = radius * 2.0 },
            [color](const coordinate::rect& r) {
                const double rad = std::min(r.width, r.height) * 0.5;
                mesh::draw(mesh::make_circle({ r.x + r.width * 0.5, r.y + r.height * 0.5 }, rad), color);
            });
    }

    inline Ref<widget::Text_Display> TextBlock(Text text, const Text_Display_Props& props = {}) {
        return TextDisplay(std::move(text), props);
    }

    inline Ref<widget::Text_Line> TextLine(Text text, const Text_Line_Props& props = {}) {
        return detail::make_ref<widget::Text_Line>(std::move(text), props);
    }

    inline Tree_Node_Data TreeNode(Text label, std::vector<Tree_Node_Data> children = {}) {
        Tree_Node_Data node;
        node.label = std::move(label);
        node.children = std::move(children);
        return node;
    }

    inline Ref<widget::Tree_View> TreeView(std::vector<Tree_Node_Data> nodes, const Tree_View_Props& props = {}) {
        return detail::make_ref<widget::Tree_View>(std::move(nodes), props);
    }

    inline Ref<widget::Shape> Outline(double width, double height, Color color, double thickness = 2.0, double radius = 0.0) {
        return Custom({ .width = width, .height = height },
            [color, thickness, radius](const coordinate::rect& r) {
                const double t = thickness;
                const double rad = std::clamp(radius, 0.0, std::min(r.width, r.height) * 0.5);
                if (rad <= 0.0) {
                    mesh::draw(mesh::make_rect({ r.x, r.y, r.width, t }), color);
                    mesh::draw(mesh::make_rect({ r.x, r.y + r.height - t, r.width, t }), color);
                    mesh::draw(mesh::make_rect({ r.x, r.y + t, t, r.height - t * 2.0 }), color);
                    mesh::draw(mesh::make_rect({ r.x + r.width - t, r.y + t, t, r.height - t * 2.0 }), color);
                }
                else {
                    auto outer = mesh::make_rect(r, rad);
                    mesh::draw(outer, color);
                }
            });
    }

    // ---- Reveal / Popover / Modal / ContextMenu / Toast ----

    inline Ref<widget::Reveal> Reveal(State<bool> visible, Element child, const Reveal_Props& props = {}) {
        return detail::make_ref<widget::Reveal>(std::move(visible), std::move(child), props);
    }

    inline Ref<widget::Popover> Popover(State<bool> open, Element trigger, Element content,
        const Popover_Props& props = {}) {
        return detail::make_ref<widget::Popover>(std::move(open), std::move(trigger), std::move(content), props);
    }

    inline Ref<widget::Modal> Modal(State<bool> open, Element content, const Modal_Props& props = {}) {
        return detail::make_ref<widget::Modal>(std::move(open), std::move(content), props);
    }

    inline Ref<widget::ContextMenu> ContextMenu(State<int> selected, std::vector<Text> items,
        Element trigger, const ContextMenu_Props& props = {}) {
        return detail::make_ref<widget::ContextMenu>(std::move(selected), std::move(items), std::move(trigger), props);
    }

    inline Ref<widget::Toast_Host> ToastHost(const Toast_Host_Props& props = {}) {
        return detail::make_ref<widget::Toast_Host>(props);
    }
    //////////////////////////////////////////////////

}