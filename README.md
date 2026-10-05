# rayui

**1080p virtual resolution, letterboxed**

*A header-only declarative UI library for raylib. Build UI's by composing widgets, bind them to reactive `State<T>` values, and let the library handle layout, animation, focus, and input*

**Everything lives in `namespace rayui`. Include `rayui.hpp` and that's it**

```cpp
#include "raylib.h"
#include "rayui.hpp"

int main() {
    InitWindow(1280, 720, "rayui demo");
    SetExitKey(KEY_NULL);

    auto name = rayui::make_state(std::string("Player"));

    auto root = rayui::Panel({ .padding = 40.0 }, {
        rayui::VStack({ .spacing = 20.0 }, {
            rayui::Label(rayui::fmt("Hello, {}!", name)),
            rayui::Textbox(name, { .width = 400.0 })
        })
    });

    while (!WindowShouldClose()) {
        root.update(GetMousePosition(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        BeginDrawing();
        ClearBackground(BLACK);
        root.draw();
        EndDrawing();
    }

    rayui::font.unload();
    CloseWindow();
    return 0;
}
```

---

## Table of contents

- [Classes](#classes)
  - [Foundation](#foundation)
  - [Layout](#layout)
  - [Display](#display)
  - [Inputs](#inputs)
  - [Navigation](#navigation)
  - [Data](#data)
  - [Overlays](#overlays)
  - [Motion](#motion)
- [Functions](#functions)
- [Props](#props)
- [Enums](#enums)
- [Helper types](#helper-types)
- [Special notes](#special-notes)

---

## Classes

### Foundation

- **`Size`**: `{ double width; double height; }`. Used everywhere layout is measured.
- **`State<T>`**: handle to a shared reactive value.
  - **Methods**
    - `get() -> const T&`: read the value.
    - `set(T) -> void`: write the value; fires `OnChanged` if it differs.
    - `toggle() -> void`: flips a `State<bool>`.
    - `OnChanged() -> event::Signal<T>&`: subscribe to changes.
    - `anchor() -> shared_ptr<void>`: internal; keeps derived values alive.
- **`Text`**: either a fixed string or a reactive binding.
  - **Methods**
    - `Text("literal")`, `Text(std::string)`, `Text(State<std::string>)`: construct from any source.
    - `Text::from(state, formatter)`: derive from any `State<T>`.
    - `Text::from_format(pattern, args...)`: same as `fmt`, called directly.
    - `get() -> const std::string&`: current value.
    - `OnChanged()`: subscribe to text changes.
    - `anchor()`: internal keep-alive.
- **`Animated`**: a number that can glide toward a target.
  - **Methods**
    - `Animated(double initial = 0.0)`
    - `get() -> double`
    - `set(double) -> void`: jump instantly, cancels any running tween.
    - `animate_to(target, tween::TweenInfo) -> void`: glide, given a duration/easing.
    - `animate_to(target, seconds) -> void`: glide, easier form.
    - `stop() -> void`, `is_animating() -> bool`
    - `OnChanged() -> Signal<double>&`, `OnFinished() -> Signal<>&`
- **`Animator`**: global stepper for every `Animated`.
  - **Methods**
    - `update(double dt) -> void`
    - `tick(double now) -> void`: dt from real time; called by `Element::update`.
    - `active_count() -> size_t`
- **`Font_Manager`**: the one font used by all text-drawing widgets. Access the global as `rayui::font`.
  - **Methods**
    - `load(path, base_size = 64) -> bool`
    - `unload() -> void`: call before `CloseWindow`.
    - `get() -> Font`
    - `revision() -> unsigned`
    - `measure(text, size) -> Size`
    - `pen(text, size) -> double`: advances including trailing spacing.
    - `advance(codepoint, size) -> double`
    - `draw(text, pos, size, color) -> void`
- **`Hotkey_Handle`**: returned by `rayui::hotkey`.
  - **Methods**
    - `remove() -> void`: unregister.
    - `explicit operator bool()`: true when bound.
- **`Widget`**: base class for every widget; user code rarely touches it.
  - **Methods**
    - `desired() -> Size`: preferred size given the widget's own measure.
    - `desired_for_width(width) -> Size`: for wrapping widgets.
    - `flex_weight() -> double`
    - `fixed_width() -> double`, `fixed_height() -> double`
    - `is_enabled()`, `set_enabled(bool)`
    - `is_visible()`, `set_visible(bool)`
    - `opacity()`, `set_opacity(double)`
    - `focusable() -> bool`: overridden by interactive widgets.
    - `has_focus() -> bool`
    - `collect_focusables(out) -> void`: containers override to recurse.
    - `draw_focus_ring()`, `has_custom_focus_ring()`
- **`Element`**: type-erased handle to any widget; also the root driver.
  - **Methods**
    - `update(mouse, pressed) -> void`: full update from raylib state.
    - `update(mouse, pressed, down, released) -> void`: manual button states.
    - `draw() -> void`: render the tree and its overlays.
    - `get() -> Widget*`, `explicit operator bool()`
- **`Ref<T>`**: typed handle that adds chaining helpers.
  - **Methods**
    - `onClick(cb)`: connect `OnClick`.
    - `onChange(cb)`: connect `OnChanged`.
    - `onHover(cb)`: connect `OnHoverChanged`.
    - `onSubmit(cb)`: connect `OnSubmit`.
    - `onFocus(cb)`: connect `OnFocusChanged`.
    - `onClose(cb)`: connect `OnClose`.
    - `onToggled(cb)`: connect `OnToggled`.
    - `onSelect(cb)`: connect `OnSelect`.
    - `onRowClick(cb)`: connect `OnRowClick`.
    - `enabled(bool)` / `enabled(State<bool>)`: enable state.
    - `visible(bool)` / `visible(State<bool>)`: visibility.
    - `opacity(double)` / `opacity(State<U>)`: opacity.

### Layout

- **`Panel`**: layered container; every child fills the padded area. Usually the root.
  - **Methods:** *(all from `Widget`; children are constructor-only).*
- **`Stack`**: vertical (`VStack`) or horizontal (`HStack`) flow with spacing, alignment, and flex.
  - **Methods:** *(all from `Widget`).*
- **`Wrap`**: flows children left-to-right and wraps onto new rows at the container's width.
  - **Methods:** *(all from `Widget`).*
- **`Grid`**: lays children into a fixed number of equal columns.
  - **Methods:** *(all from `Widget`).*
- **`Positioned`**: places one child at an offset from an anchor of the parent's area.
  - **Methods:** *(all from `Widget`).*
- **`Spacer`**: an empty widget that takes flex space.
  - **Methods:** *(all from `Widget`).*
- **`Cond`**: shows one of two children depending on a bool or predicate.
  - **Methods**
    - `set_active(bool) -> void`: switch which branch is live.
- **`Switch`**: shows one of N children, chosen by an index.
  - **Methods**
    - `set_index(int) -> void`
- **`ForEach_View`**: a `Stack` whose children are rebuilt when a `State<vector<T>>` changes. Built by the `ForEach` factory; not constructed directly.
  - **Methods:** *(all from `Widget`).*

### Display

- **`Label`**: single-line text.
  - **Methods**
    - `set_text(std::string) -> void`
    - `text() -> const std::string&`
- **`Text_Display`**: read-only text that wraps at its width.
  - **Methods**
    - `text() -> const std::string&`
    - `line_count() -> size_t`
- **`Text_Line`**: single-line read-only text; scrolls horizontally if it overflows.
  - **Methods**
    - `text() -> const std::string&`
    - `scroll_to(double) -> void`
    - `scroll_to_start()`, `scroll_to_end()`
    - `scroll_offset() -> double`, `overflowing() -> bool`
- **`Image_View`**: draws a `Texture2D` with a fit mode.
  - **Methods:** *(all from `Widget`).*
- **`Progress_Bar`**: horizontal progress bar with an animated fill.
  - **Methods**
    - `set_fraction(double) -> void`
    - `fraction() -> double`
- **`Badge`**: small pill of text on a colored background.
  - **Methods:** *(all from `Widget`).*
- **`Chip`**: clickable tag with an optional close button.
  - **Methods:** *(all from `Widget`).*
- **`Link`**: clickable text with hover underline and pointing cursor.
  - **Methods:** *(all from `Widget`).*
- **`Shape`**: blank widget driven by callbacks; base for `Rect`, `Circle`, `Outline`, `Custom`.
  - **Methods**
    - `set_measure(fn) -> void`
    - `set_update(fn) -> void`
    - `set_draw(fn) -> void`

### Inputs

- **`Button`**: clickable text on a rounded background.
  - **Methods**
    - `set_text(std::string) -> void`
- **`Slider`**: drag-to-adjust numeric slider bound to a numeric state.
  - **Methods**
    - `refresh() -> void`: rebuild meshes after an external change.
- **`Toggle`**: on/off switch bound to a `State<bool>`; the knob glides.
  - **Methods**
    - `sync() -> void`: glide the knob to match the current value.
- **`Checkbox`**: box + label bound to a `State<bool>`.
  - **Methods:** *(all from `Widget`).*
- **`Radio`**: one radio button sharing an int state.
  - **Methods:** *(all from `Widget`).*
- **`Textbox`**: single-line text input bound to a `State<std::string>`.
  - **Methods**
    - `text() -> const std::string&`
    - `focused() -> bool`
- **`Text_Field`**: multi-line editable text that wraps.
  - **Methods**
    - `text() -> const std::string&`
    - `focused() -> bool`
- **`Number_Input`**: numeric text field bound to a numeric state. Typed values are clamped to `[min, max]` and optionally snapped to multiples of `step`.
  - **Methods**
    - `value() -> double`
    - `sync() -> void`: reformat the field after an external change.
    - `focused() -> bool`
- **`Dropdown`**: select box that opens a scrollable list on top of everything.
  - **Methods**
    - `is_open() -> bool`
    - `hovered_index() -> int`
    - `popup_bounds() -> const coordinate::rect&`
    - `close_popup() -> void`
- **`Segmented_Control`**: row of exclusive segments with a sliding thumb.
  - **Methods:** *(all from `Widget`).*
- **`Rating`**: a row of stars bound to an int state.
  - **Methods:** *(all from `Widget`).*

### Navigation

- **`Tab_Bar`**: the header row of `Tabs`: titles, hover fade, sliding indicator.
  - **Methods:** *(all from `Widget`).*
- **`Tree_View`**: hierarchical list built from a value tree.
  - **Methods**
    - `set_expanded(path, expanded) -> void`
    - `expand_all()`, `collapse_all()`
    - `selected_path() -> const std::vector<int>&`
- **`Collapsible`**: header that shows or hides its content with an animated unfold.
  - **Methods**
    - `is_open() -> bool`
    - `set_open(bool, instant = false) -> void`
    - `toggle() -> void`
- **`Accordion`**: a column of `Collapsible` sections where only one is open.
  - **Methods:** *(all from `Widget`).*

### Data

- **`Table`**: scrollable, striped, selectable data grid with a sticky header.
  - **Methods**
    - `set_rows(rows) -> void`
    - `selected_row() -> int`
- **`Scroll_View`**: vertical scroll viewport around one content element.
  - **Methods**
    - `scroll_to(offset) -> void`
    - `offset() -> double`

### Overlays

- **`Popover`**: floats a content panel next to a trigger when a `State<bool>` is true.
  - **Methods**
    - `is_open() -> bool`
    - `close_popup() -> void`
- **`Modal`**: centered dialog with a full-screen backdrop; blocks input below.
  - **Methods:** *(all from `Widget`).*
- **`ContextMenu`**: right-click menu at the pointer, with fade + scale animation.
  - **Methods**
    - `is_open() -> bool`
    - `close_popup() -> void`
- **`Tooltip`**: wraps a child; shows a small text bubble after a hover delay.
  - **Methods**
    - `showing() -> bool`
- **`Toast_Host`**: draws the global toast queue in a corner.
  - **Methods:** *(all from `Widget`).*

### Motion

- **`Reveal`**: wraps a child and animates it in/out on a `State<bool>`.
  - **Methods:** *(all from `Widget`).*

---

## Functions

### Reactive text

- `fmt(pattern, args...) -> Text`: interpolate `{}` slots; `{{`/`}}` for literal braces. Args may be `State<T>` or plain values.
- `Text::from(state, formatter) -> Text`: derive text from any state with a lambda.

### Core

- `make_state(T initial) -> State<T>`: construct a shared value.
- `request_layout() -> void`: force a relayout on the next update.
- `clear_focus() -> void`: drop keyboard focus.
- `focus_widget(Widget*) -> void`: give focus to a widget.
- `ease(seconds, style = Quad, direction = Out) -> tween::TweenInfo`: shorthand for a tween duration.
- `hotkey(key, mods, callback) -> Hotkey_Handle`: register a global keyboard shortcut. Modifiers combine with `|` (see `Hotkey_Mod`) and must match **exactly** — `Ctrl+S` and `Ctrl+Shift+S` are distinct bindings, and `Ctrl+S` will not fire when Shift is also held. Re-registering the same key+mods replaces the previous binding.
- `toast(text, props = {}) -> void`: push a message onto the global toast queue.

### Containers

- `Panel(props, children) -> Ref<Panel>`
- `VStack(props, children) -> Ref<Stack>`
- `HStack(props, children) -> Ref<Stack>`
- `Wrap(props, children) -> Ref<Wrap>`
- `Grid(props, children) -> Ref<Grid>`
- `Spacer(props = {}) -> Ref<Spacer>`
- `Positioned(props, child) -> Ref<Positioned>`
- `Cond(state, when_true, when_false = {}) -> Ref<Cond>`: bool state.
- `Cond(state, pred, when_true, when_false = {}) -> Ref<Cond>`: predicate on any state.
- `Switch(state, pages) -> Ref<Switch>`
- `ForEach(source, builder, props = { .spacing = 8.0 }) -> Ref<ForEach_View>`

### Display

- `Label(text, props = {}) -> Ref<Label>`
- `Label(state, formatter, props = {}) -> Ref<Label>`
- `TextDisplay(text, props = {}) -> Ref<Text_Display>`
- `TextBlock(text, props = {}) -> Ref<Text_Display>`: alias for `TextDisplay`.
- `TextLine(text, props = {}) -> Ref<Text_Line>`
- `Image(texture, props = {}) -> Ref<Image_View>`
- `ProgressBar(state, min, max, props = {}) -> Ref<Progress_Bar>`
- `ProgressBar(fraction, props = {}) -> Ref<Progress_Bar>`
- `Badge(text, props = {}) -> Ref<Badge>`
- `Chip(text, props = {}) -> Ref<Chip>`
- `Link(text, props = {}) -> Ref<Link>`

### Shapes / builder

- `Custom(props, draw) -> Ref<Shape>`
- `Rect(width, height, color, radius = 0.0) -> Ref<Shape>`
- `Circle(radius, color) -> Ref<Shape>`
- `Outline(width, height, color, thickness = 2.0, radius = 0.0) -> Ref<Shape>`

### Inputs

- `Button(text, props = {}) -> Ref<Button>`
- `Slider(state, min, max, props = {}) -> Ref<Slider>`
- `Toggle(state, props = {}) -> Ref<Toggle>`
- `Checkbox(state, label = {}, props = {}) -> Ref<Checkbox>`
- `Radio(state, value, label = {}, props = {}) -> Ref<Radio>`
- `RadioGroup(state, labels, layout = { .spacing = 16.0 }, props = {}) -> Ref<Stack>`
- `Textbox(state, props = {}) -> Ref<Textbox>`
- `TextField(state, props = {}) -> Ref<Text_Field>`
- `NumberInput(state, props = {}) -> Ref<Number_Input>`
- `Dropdown(state, options, props = {}) -> Ref<Dropdown>`
- `SegmentedControl(state, options, props = {}) -> Ref<Segmented_Control>`
- `Rating(state, props = {}) -> Ref<Rating>`

### Navigation / data

- `Tab(title, content) -> Tab_Page`
- `Tabs(selected, tabs, props = {}) -> Ref<Stack>`
- `Collapsible(title, content, props = {}) -> Ref<Collapsible>`
- `AccordionItem(title, content) -> Accordion_Item`
- `Accordion(selected, items, props = {}) -> Ref<Accordion>`
- `TreeView(nodes, props = {}) -> Ref<Tree_View>`
- `TreeNode(label, children = {}) -> Tree_Node_Data`
- `Table(columns, rows, props = {}) -> Ref<Table>`
- `Column(header, props = {}) -> Table_Column`
- `ScrollView(props, content) -> Ref<Scroll_View>`
- `Divider(props = {}) -> Ref<Divider>`

### Overlays

- `Reveal(visible, child, props = {}) -> Ref<Reveal>`
- `Popover(open, trigger, content, props = {}) -> Ref<Popover>`
- `Modal(open, content, props = {}) -> Ref<Modal>`
- `ContextMenu(selected, items, trigger, props = {}) -> Ref<ContextMenu>`
- `Tooltip(text, child, props = {}) -> Ref<Tooltip>`
- `ToastHost(props = {}) -> Ref<Toast_Host>`

### Mesh (namespace `rayui::mesh`)

- `from_polygon(points, center) -> coordinate::tri_mesh`
- `make_rect(r, radius = 0.0, corner_segments = 6) -> coordinate::tri_mesh`
- `make_circle(center, radius, segments = 24) -> coordinate::tri_mesh`
- `make_line(a, b, thickness) -> coordinate::tri_mesh`
- `draw(mesh, color) -> void`
- `drop_shadow(rect, corner_radius, spread = 10.0, strength = 0.4, color = BLACK) -> void`

---

## Props

### Container props

- **`Panel_Props`**
  - `padding`: inner inset on all sides.
  - `backgroundColor`: fill.
  - `cornerRadius`
  - `width`, `height`, `flex`

- **`Stack_Props`**
  - `spacing`: gap between children.
  - `alignment`: cross-axis align.
  - `justify`: main-axis align (when nothing has flex).
  - `width`, `height`, `flex`

- **`Wrap_Props`**
  - `spacingX`, `spacingY`
  - `width`, `height`, `flex`

- **`Grid_Props`**
  - `columns`: number of equal columns.
  - `spacingX`, `spacingY`
  - `width`, `height`, `flex`

- **`Positioned_Props`**
  - `anchor`: reference point on the parent.
  - `x`, `y`: offset from the anchor.
  - `width`, `height`: optional override of the child's size.

- **`Spacer_Props`**
  - `width`, `height`, `flex`

### Display props

- **`Label_Props`**
  - `fontSize`, `color`
  - `textAlign`: `Start`, `Center`, `End`.
  - `width`, `height`, `flex`

- **`Text_Display_Props`**
  - `fontSize`, `color`, `textAlign`, `lineHeight`
  - `maxWidth`: wrap no wider than this when no width is set.
  - `width`, `height`, `flex`

- **`Text_Line_Props`**
  - `fontSize`, `color`, `textAlign`
  - `width`, `height`, `padding`, `flex`
  - `scrollbarHeight`, `scrollbarMinHeight`, `scrollbarFadeTime`, `scrollbarIdleTime`
  - `wheelStep`, `smoothTime`
  - `scrollbarThumbColor`, `scrollbarTrackColor`

- **`Image_Props`**
  - `fit`: `Contain`, `Cover`, `Stretch`.
  - `tint`
  - `width`, `height`, `flex`

- **`Progress_Props`**
  - `width`, `height`, `flex`
  - `cornerRadius`, `animateTime`
  - `trackColor`, `fillColor`

- **`Badge_Props`**
  - `fontSize`, `paddingX`, `paddingY`, `cornerRadius`
  - `textColor`, `backgroundColor`
  - `width`, `height`, `flex`

- **`Chip_Props`**
  - `fontSize`, `paddingX`, `paddingY`, `cornerRadius`
  - `closeable`: show the little X.
  - `fadeTime`
  - `textColor`, `backgroundColor`, `hoverColor`, `closeColor`
  - `width`, `height`, `flex`

- **`Link_Props`**
  - `fontSize`, `underlineOnHover`
  - `color`, `hoverColor`
  - `width`, `height`, `flex`

- **`Shape_Props`**
  - `width`, `height`, `flex`

### Input props

- **`Button_Props`**
  - `padding`, `fontSize`, `cornerRadius`
  - `textColor`, `backgroundColor`, `hoverColor`, `pressedColor`
  - `width`, `height`, `flex`, `fadeTime`

- **`Slider_Props`**
  - `width`, `height`, `flex`
  - `trackHeight`, `knobRadius`
  - `trackColor`, `fillColor`, `knobColor`

- **`Toggle_Props`**
  - `trackWidth`, `trackHeight`
  - `offColor`, `onColor`, `knobColor`
  - `animateTime`
  - `width`, `height`, `flex`

- **`Checkbox_Props`**
  - `fontSize`
  - `textColor`, `boxColor`, `checkColor`
  - `width`, `height`, `flex`

- **`Radio_Props`**
  - `fontSize`
  - `textColor`, `ringColor`, `dotColor`
  - `width`, `height`, `flex`

- **`Textbox_Props`**
  - `placeholder`
  - `width`, `height`, `flex`
  - `fontSize`, `padding`, `cornerRadius`
  - `maxLength`: 0 = unlimited.
  - `textColor`, `placeholderColor`, `backgroundColor`, `focusColor`, `caretColor`, `selectionColor`

- **`Text_Field_Props`**
  - `placeholder`
  - `width`, `height`, `flex`
  - `fontSize`, `lineHeight`, `padding`, `cornerRadius`
  - `minLines`, `maxLength`
  - `textColor`, `placeholderColor`, `backgroundColor`, `focusColor`, `caretColor`, `selectionColor`

- **`Number_Input_Props`**
  - `width`, `height`, `flex`
  - `fontSize`, `padding`, `cornerRadius`
  - `step`: `0` = no rounding; `> 0` = typed values snap to the nearest multiple of `step`.
  - `min`, `max`, `decimals`
  - `textColor`, `backgroundColor`, `focusColor`

- **`Dropdown_Props`**
  - `placeholder`
  - `width`, `height`, `flex`
  - `fontSize`, `padding`, `cornerRadius`
  - `maxVisible`: rows shown before the list scrolls.
  - `textColor`, `placeholderColor`, `backgroundColor`
  - `listColor`, `hoverColor`, `activeColor`, `focusColor`

- **`Segmented_Props`**
  - `fontSize`, `padding`, `cornerRadius`, `animateTime`
  - `textColor`, `activeTextColor`, `trackColor`, `thumbColor`
  - `width`, `height`, `flex`

- **`Rating_Props`**
  - `max`, `starSize`, `spacing`
  - `filledColor`, `emptyColor`
  - `width`, `height`, `flex`

### Navigation props

- **`Tabs_Props`**
  - `fontSize`, `tabPadding`, `indicatorHeight`, `gap`
  - `stretch`: tabs split width equally.
  - `textColor`, `activeColor`, `indicatorColor`
  - `width`, `height`, `flex`

- **`Collapsible_Props`**
  - `fontSize`, `headerPadding`, `spacing`, `cornerRadius`
  - `animateTime`: open/close glide duration.
  - `defaultOpen`
  - `headerColor`, `headerHoverColor`, `contentColor`, `textColor`
  - `width`, `height`, `flex`

- **`Accordion_Props`**
  - `fontSize`, `gap`, `animateTime`
  - `headerColor`, `headerHoverColor`, `contentColor`, `textColor`
  - `width`, `height`, `flex`

- **`Tree_View_Props`**
  - `fontSize`, `rowPadding`, `indentWidth`
  - `arrowSize`, `arrowGap`, `sidePadding`, `cornerRadius`
  - `width`, `height`, `flex`
  - `defaultExpanded`, `showGuides`
  - `scrollbarWidth`, `scrollbarMinWidth`, `scrollbarFadeTime`, `scrollbarIdleTime`
  - `wheelStep`, `smoothTime`
  - `hoverColor`, `selectedColor`, `selectedTextColor`, `arrowColor`, `guideColor`

### Data props

- **`Table_Props`**
  - `width`, `height`, `flex`
  - `fontSize`, `headerFontSize`, `rowPadding`, `cellPadding`, `cornerRadius`
  - `striped`, `stickyHeader`
  - `scrollbarWidth`, `scrollbarMinWidth`, `scrollbarFadeTime`, `scrollbarIdleTime`
  - `wheelStep`, `smoothTime`
  - `headerBackground`, `rowColor`, `altRowColor`, `hoverColor`, `selectedColor`, `textColor`, `selectedTextColor`, `dividerColor`

- **`Column_Props`**
  - `width`: auto = flex column.
  - `flex`
  - `align`

- **`Scroll_Props`**
  - `width`, `height`, `flex`
  - `scrollbarWidth`, `scrollbarMinWidth`, `scrollbarFadeTime`, `scrollbarIdleTime`
  - `wheelStep`, `smoothTime`
  - `trackColor`, `thumbColor`

- **`Divider_Props`**
  - `axis`: the direction the line runs.
  - `thickness`, `color`
  - `width`, `height`, `flex`

### Overlay props

- **`Reveal_Props`**
  - `enterTime`, `exitTime`
  - `enterOffsetY`, `exitOffsetY`: slide distances.
  - `width`, `height`, `flex`

- **`Popover_Props`**
  - `side`: `Top`, `Bottom`, `Left`, `Right`.
  - `gap`, `cornerRadius`, `padding`
  - `closeOnOutsideClick`, `closeOnEscape`
  - `backgroundColor`
  - `width`, `height`

- **`Modal_Props`**
  - `dismissOnBackdrop`, `dismissOnEscape`
  - `padding`, `cornerRadius`
  - `backdropColor`, `contentColor`
  - `contentWidth`, `contentHeight`
  - `fadeTime`

- **`ContextMenu_Props`**
  - `fontSize`, `padding`, `rowPadding`, `cornerRadius`
  - `animateTime`: open/close glide.
  - `scaleFrom`: panel scale at fade = 0.
  - `textColor`, `backgroundColor`, `hoverColor`

- **`Tooltip_Props`**
  - `delay`: seconds of hovering before it appears.
  - `fontSize`, `padding`, `cornerRadius`
  - `offsetX`, `offsetY`
  - `backgroundColor`, `textColor`

- **`Toast_Props`**
  - `level`: `Info`, `Success`, `Warning`, `Error`. Selects a preset `*Color` below unless `color` is set explicitly.
  - `color`: direct background override. Leave at the default (alpha 0) to use the level's preset.
  - `duration`
  - `fontSize`, `padding`, `lineHeight`, `cornerRadius`, `maxWidth`
  - `textColor`
  - `infoColor`, `successColor`, `warningColor`, `errorColor`

- **`Toast_Host_Props`**
  - `anchor`: `TopLeft` … `BottomRight`.
  - `marginX`, `marginY`, `gap`
  - `fadeInTime`, `fadeOutTime`

---

## Enums

- **`Align`**: `Start`, `Center`, `End`, `Stretch`.
- **`Axis`**: `Vertical`, `Horizontal`.
- **`Fit`**: `Contain`, `Cover`, `Stretch`.
- **`Anchor`**: `TopLeft`, `Top`, `TopRight`, `Left`, `Center`, `Right`, `BottomLeft`, `Bottom`, `BottomRight`.
- **`Side`**: `Top`, `Bottom`, `Left`, `Right`.
- **`Toast_Level`**: `Info`, `Success`, `Warning`, `Error`.
- **`Hotkey_Mod`**: `HOTKEY_NONE`, `HOTKEY_SHIFT`, `HOTKEY_CTRL`, `HOTKEY_ALT`, `HOTKEY_SUPER`. Combine with `|`.

---

## Helper types

- **`Table_Column`**: `{ Text header; double width; double flex; Align align; }`.
- **`Table_Row`**: `{ vector<string> cells; }`.
- **`Tree_Node_Data`**: `{ Text label; vector<Tree_Node_Data> children; Color color; }`.
- **`Tab_Page`**: `{ Text title; Element content; }`.
- **`Accordion_Item`**: `{ Text title; Element content; }`.

---

## Special notes

- **Addition note about the canvas/letterboxing** Everything is authored on a 1920x1080 canvas and scaled to the window. All sizes, font sizes, and offsets are in logical units of that canvas.
- **`AUTO_SIZE`** = `-1.0`. Use it to say "size to content".
- **One font** All text uses `rayui::font`. Load it once at startup, unload it before `CloseWindow`.
- **Overlay queue** Popovers, dropdowns, modals, context menus, tooltips, and toasts all render on top of the whole tree. Only one popup-like widget is open at a time.
- **Overlays consume their opening click.** When a popup (context menu, dropdown, etc.) handles a click that also triggers a state change which opens an overlay (e.g. a modal), the overlay sees the same frame's input but the click flags are already cleared — the new overlay won't be dismissed by the press that opened it.
- **Focus navigation** Tab / Shift+Tab move focus through focusable widgets; Enter and Space activate the focused one. Escape clears focus unless a widget claims it first.
- **Hotkeys fire after the tree** They run regardless of focus unless a focused widget explicitly consumes the key that frame (text inputs consume Escape when they blur).