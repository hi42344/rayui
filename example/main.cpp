#include "raylib.h"
#include "../src/rayui.hpp"
#include "helpers/exe_path.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

void set_borderless_windowed(int monitor) {
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor) - 1);
    SetWindowPosition(0, 0);
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_BORDERLESS_WINDOWED_MODE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_UNDECORATED);
    InitWindow(1280, 720, "Rayui Example");
    set_borderless_windowed(GetCurrentMonitor());
    SetExitKey(KEY_NULL);
    int monitor = GetCurrentMonitor();
    SetTargetFPS(GetMonitorRefreshRate(monitor));

    //Dark colors
    // Deep dark neutral colors
    rayui::theme.panel = { 23, 24, 29, 255 };
    rayui::theme.text = { 232, 234, 237, 255 };
    rayui::theme.button = { 31, 33, 40, 255 };
    rayui::theme.button_hover = { 44, 47, 58, 255 };
    rayui::theme.button_pressed = { 21, 22, 27, 255 };
    rayui::theme.track = { 34, 36, 44, 255 };
    rayui::theme.accent = { 124, 140, 255, 255 };
    rayui::theme.knob = { 240, 242, 245, 255 };
    rayui::theme.input = { 15, 15, 18, 255 };
    rayui::theme.placeholder = { 108, 112, 121, 255 };
    rayui::theme.selection = { 124, 140, 255, 90 };
    rayui::theme.divider = { 40, 42, 50, 255 };
    rayui::theme.tab_inactive = { 140, 144, 152, 255 };
    rayui::theme.tooltip = { 21, 22, 27, 245 };
    rayui::theme.popup = { 26, 28, 34, 255 };
    rayui::theme.backdrop = { 0, 0, 0, 200 };
    rayui::theme.disabled_opacity = 0.4;

    // Load Rajdhani
    std::filesystem::path path = get_executable_path();
    path = path.parent_path().parent_path();
    path = path / "Font" / "Rajdhani" / "Rajdhani-SemiBold.ttf";
    TraceLog(LOG_INFO, TextFormat("Path = %s\n", path.string().c_str()));
    const int mapped = 128 * rayui::Internal::mapper.get_dpi_scale();
    TraceLog(LOG_INFO, TextFormat("Mapped scale = %i", mapped));
    rayui::font.load(path.string(), mapped);

    // -------- Global hotkeys --------
    rayui::hotkey(KEY_S, rayui::HOTKEY_CTRL, []() {
        rayui::toast("Saved!", { .level = rayui::Toast_Level::Success });
        });
    rayui::hotkey(KEY_S, rayui::HOTKEY_CTRL | rayui::HOTKEY_SHIFT, []() {
        rayui::toast("Save As...", { .level = rayui::Toast_Level::Info });
        });
    rayui::hotkey(KEY_O, rayui::HOTKEY_CTRL, []() {
        rayui::toast("Open file...", { .level = rayui::Toast_Level::Info });
        });
    rayui::hotkey(KEY_N, rayui::HOTKEY_CTRL, []() {
        rayui::toast("New file", { .level = rayui::Toast_Level::Info });
        });
    rayui::hotkey(KEY_F1, rayui::HOTKEY_NONE, []() {
        rayui::toast("Help: F1 pressed", { .level = rayui::Toast_Level::Info });
        });
    rayui::hotkey(KEY_F2, rayui::HOTKEY_NONE, []() {
        rayui::toast("Rename mode", { .level = rayui::Toast_Level::Warning });
        });
    rayui::hotkey(KEY_Q, rayui::HOTKEY_CTRL, []() {
        rayui::toast("Quit shortcut fired (not actually quitting)", { .level = rayui::Toast_Level::Warning });
        });

    // Reactive state
    auto page = rayui::make_state(0);
    auto volume = rayui::make_state(75.0f);
    auto is_muted = rayui::make_state(false);
    auto controls_on = rayui::make_state(true);
    auto mute_label = rayui::make_state(std::string("Mute"));
    auto name = rayui::make_state(std::string("Player"));
    auto notes = rayui::make_state(std::string(
        "This field wraps long lines automatically, grows with its content, and scrolls once it has a fixed height."));
    auto dark_mode = rayui::make_state(true);
    auto quality = rayui::make_state(1);
    auto language = rayui::make_state(0);

    auto likes = rayui::make_state(3);
    auto view_mode = rayui::make_state(1);
    auto chip_alpha = rayui::make_state(true);
    auto chip_beta = rayui::make_state(true);
    auto chip_gamma = rayui::make_state(true);

    auto show_modal = rayui::make_state(false);
    auto confirm_delete_open = rayui::make_state(false);
    auto popover_open = rayui::make_state(false);
    auto context_choice = rayui::make_state(-1);
    auto accordion_section = rayui::make_state(0);
    auto shape_reveal = rayui::make_state(true);

    auto tree_selection = rayui::make_state(std::string("(none)"));

    auto quantity = rayui::make_state(1);
    auto price = rayui::make_state(9.99);
    auto opacity_val = rayui::make_state(0.75);

    auto items = rayui::make_state(std::vector<std::string>{ "Apple", "Banana", "Cherry" });

    auto polish_popover = rayui::make_state(false);
    auto polish_menu = rayui::make_state(-1);

    Image checker_image = GenImageChecked(256, 128, 32, 32, DARKGRAY, LIGHTGRAY);
    Texture2D checker = LoadTextureFromImage(checker_image);
    UnloadImage(checker_image);

    std::vector<rayui::Element> tracks;
    for (int i = 1; i <= 14; ++i) {
        tracks.push_back(rayui::Label("Track " + std::to_string(i), { .fontSize = 34.0, .color = { 140, 144, 152, 255 } }));
    }

    // -------- Audio --------
    auto audio_page = rayui::VStack({ .spacing = 28.0, .alignment = rayui::Align::Center, .justify = rayui::Align::Center }, {
        rayui::Label("Audio Settings",{ .fontSize = 72.0, .color = WHITE }),

        rayui::Textbox(name,{ .placeholder = "Profile name", .width = 600.0, .maxLength = 20 })
            .onSubmit([](const std::string& value) { TraceLog(LOG_INFO, "Submitted: %s", value.c_str()); }),

        rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
            rayui::Label("Volume:",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
            rayui::Slider(volume, 0.0f, 100.0f,{ .width = 420.0 }),
            rayui::Label(rayui::fmt("{}%", volume),{ .fontSize = 40.0, .textAlign = rayui::Align::End, .width = 120.0 })
        }).enabled(controls_on),

        rayui::ProgressBar(volume, 0.0f, 100.0f,{ .width = 600.0 }).enabled(controls_on),

        rayui::Button(mute_label,{ .padding = 24.0 })
            .onClick([&]() {
                is_muted.set(!is_muted.get());
                controls_on.set(!is_muted.get());
                mute_label.set(is_muted.get() ? "Unmute" : "Mute");
            }),

        rayui::Cond(is_muted,
            rayui::Label("Status: MUTED",{ .color = { 220, 90, 90, 255 } }),
            rayui::Label("Status: ACTIVE",{ .color = { 100, 200, 130, 255 } })
        ),

        rayui::ScrollView({ .width = 600.0, .height = 200.0 },
            rayui::VStack({ .spacing = 10.0 }, std::move(tracks)))
        });

    auto notes_bytes = rayui::make_state(notes.get().size());

    // -------- Notes --------
    auto notes_page = rayui::ScrollView({ .width = 10.0, .height = 250.0 },
        rayui::VStack({ .spacing = 28.0, .alignment = rayui::Align::Center }, {
        rayui::Label("Notes",{ .fontSize = 56.0, .color = WHITE }),

        rayui::TextField(notes,{ .width = 900.0, .height = 340.0 }).onChange([&](const std::string& new_text) {
            notes_bytes.set(new_text.size());
        }),

        rayui::Label(rayui::fmt("{} bytes", notes_bytes),
            { .fontSize = 28.0, .color = { 108, 112, 121, 255 } }),

        rayui::TextDisplay(rayui::fmt("Preview: {}", notes),
            { .fontSize = 32.0, .color = { 140, 144, 152, 255 }, .width = 900.0 }),
            }));

        // -------- Widgets --------
        auto widgets_page = rayui::HStack({ .spacing = 160.0, .alignment = rayui::Align::Start, .justify = rayui::Align::Center }, {
            rayui::VStack({ .spacing = 28.0 },{
                rayui::Label("Options",{ .fontSize = 56.0, .color = WHITE }),

                rayui::HStack({ .spacing = 24.0, .alignment = rayui::Align::Center },{
                    rayui::Label("Dark mode",{ .color = { 140, 144, 152, 255 } }),
                    rayui::Toggle(dark_mode)
                }),

                rayui::Divider(),

                rayui::Label("Quality",{ .color = { 140, 144, 152, 255 } }),
                rayui::RadioGroup(quality,{ "Low", "Medium", "High" }),

                rayui::Label(rayui::fmt("Quality: {}", quality),
                    { .fontSize = 28.0, .color = { 108, 112, 121, 255 } })
            }),

            rayui::VStack({ .spacing = 28.0 },{
                rayui::Label("Language",{ .fontSize = 56.0, .color = WHITE }),

                rayui::Dropdown(language,{ "English", "Spanish", "French", "German", "Italian", "Portuguese", "Dutch", "Swedish" },
                    { .placeholder = "Pick one", .width = 420.0, .maxVisible = 5 }),

                rayui::Tooltip("Hover tooltips fade in after half a second",
                    rayui::Button("Hover me",{ .padding = 24.0 })),

                rayui::Image(checker,{ .fit = rayui::Fit::Cover, .width = 420.0, .height = 200.0 })
            })
            });

        // -------- Extras --------
        auto extras_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
            rayui::VStack({ .spacing = 40.0, .alignment = rayui::Align::Center }, {

                rayui::Label("Extras",{ .fontSize = 72.0, .color = WHITE }),

                rayui::Label("Badges",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                rayui::HStack({ .spacing = 12.0, .alignment = rayui::Align::Center },{
                    rayui::Label("v1.0.0",{ .fontSize = 34.0, .color = { 140, 144, 152, 255 } }),
                    rayui::Badge("NEW"),
                    rayui::Badge("beta",{ .textColor = BLACK, .backgroundColor = { 220, 150, 70, 255 } }),
                    rayui::Badge("stable",{ .backgroundColor = { 70, 150, 90, 255 } })
                }),

                rayui::Divider(),

                rayui::Label("Segmented control",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                rayui::SegmentedControl(view_mode,{ "List", "Grid", "Kanban" },
                    { .fontSize = 34.0, .width = 720.0 }),
                rayui::Label(rayui::fmt("Selected view: {}", view_mode),
                    { .fontSize = 28.0, .color = { 108, 112, 121, 255 } }),

                rayui::Divider(),

                rayui::Label("Chips",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                rayui::HStack({ .spacing = 12.0, .alignment = rayui::Align::Center },{
                    rayui::Chip("alpha",{ .fontSize = 32.0 })
                        .visible(chip_alpha)
                        .onClose([chip_alpha]() { chip_alpha.set(false); })
                        .onClick([]() { TraceLog(LOG_INFO, "chip alpha clicked"); }),
                    rayui::Chip("beta",{ .fontSize = 32.0 })
                        .visible(chip_beta)
                        .onClose([chip_beta]() { chip_beta.set(false); }),
                    rayui::Chip("gamma",{ .fontSize = 32.0 })
                        .visible(chip_gamma)
                        .onClose([chip_gamma]() { chip_gamma.set(false); }),
                    rayui::Chip("readonly",{ .fontSize = 32.0, .closeable = false })
                }),

                rayui::Button("Restore all chips",{ .padding = 16.0 })
                    .onClick([chip_alpha, chip_beta, chip_gamma]() {
                        chip_alpha.set(true);
                        chip_beta.set(true);
                        chip_gamma.set(true);
                    }),

                rayui::Divider(),

                rayui::Label("Rating",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                rayui::Rating(likes,{ .starSize = 60.0 }),
                rayui::Label(rayui::fmt("{} / 5 stars", likes),
                    { .fontSize = 28.0, .color = { 108, 112, 121, 255 } }),

                rayui::Divider(),

                rayui::Label("Link",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                rayui::HStack({ .spacing = 32.0, .alignment = rayui::Align::Center },{
                    rayui::Link("Open raylib.com",{ .fontSize = 34.0 })
                        .onClick([]() { OpenURL("https://www.raylib.com/"); }),
                    rayui::Link("Log a message",{ .fontSize = 34.0 })
                        .onClick([]() { TraceLog(LOG_INFO, "link clicked"); })
                }),

                rayui::Divider(),

                rayui::Label("Grid (3 columns)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                rayui::Grid({ .columns = 3, .spacingX = 16.0, .spacingY = 16.0,
                              .width = 800.0, .height = 260.0 },{
                    rayui::Button("A",{ .fontSize = 32.0 }),
                    rayui::Button("B",{ .fontSize = 32.0 }),
                    rayui::Button("C",{ .fontSize = 32.0 }),
                    rayui::Button("D",{ .fontSize = 32.0 }),
                    rayui::Button("E",{ .fontSize = 32.0 }),
                    rayui::Button("F",{ .fontSize = 32.0 })
                })
                }));

            // -------- Inputs --------
            auto inputs_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                rayui::VStack({ .spacing = 40.0, .alignment = rayui::Align::Center }, {

                    rayui::Label("Numeric inputs & formatted text",{ .fontSize = 72.0, .color = WHITE }),

                    rayui::Label("NumberInput (integer)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                    rayui::HStack({ .spacing = 24.0, .alignment = rayui::Align::Center },{
                        rayui::NumberInput(quantity,{
                            .width = 240.0,
                            .step = 1.0,
                            .min = 0.0,
                            .max = 999.0
                        }),
                        rayui::Label(rayui::fmt("Quantity: {}", quantity),{ .fontSize = 28.0, .color = { 108, 112, 121, 255 } })
                    }),

                    rayui::Divider(),

                    rayui::Label("NumberInput (decimal, step 0.25)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                    rayui::HStack({ .spacing = 24.0, .alignment = rayui::Align::Center },{
                        rayui::NumberInput(price,{
                            .width = 260.0,
                            .step = 0.25,
                            .min = 0.0,
                            .max = 999.99,
                            .decimals = 2
                        }),
                        rayui::Label(rayui::fmt("Price: ${}", price),{ .fontSize = 28.0, .color = { 108, 112, 121, 255 } })
                    }),

                    rayui::Divider(),

                    rayui::Label("fmt() with multiple args",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                    rayui::Label(
                        rayui::fmt("You have {} item{} in the cart, total ${}",
                            quantity, "s", price),
                        { .fontSize = 30.0, .color = WHITE }),

                    rayui::Label(
                        rayui::fmt("Overlay opacity: {} ({{literal braces}} work too)", opacity_val),
                        { .fontSize = 26.0, .color = { 108, 112, 121, 255 } })
                    }));

                // -------- List --------
                auto list_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                    rayui::VStack({ .spacing = 32.0, .alignment = rayui::Align::Center }, {

                        rayui::Label("Reactive list (ForEach)",{ .fontSize = 72.0, .color = WHITE }),
                        rayui::Label("The list below re-renders whenever the vector state changes.",
                            { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                        rayui::HStack({ .spacing = 12.0, .alignment = rayui::Align::Center },{
                            rayui::Button("Add item",{ .padding = 14.0 })
                                .onClick([items]() {
                                    auto v = items.get();
                                    v.push_back("Item " + std::to_string(v.size() + 1));
                                    items.set(v);
                                }),
                            rayui::Button("Remove last",{ .padding = 14.0 })
                                .onClick([items]() {
                                    auto v = items.get();
                                    if (!v.empty()) { v.pop_back(); items.set(v); }
                                }),
                            rayui::Button("Reverse",{ .padding = 14.0 })
                                .onClick([items]() {
                                    auto v = items.get();
                                    std::reverse(v.begin(), v.end());
                                    items.set(v);
                                })
                        }),

                        rayui::Panel({ .padding = 16.0, .backgroundColor = { 26, 28, 34, 255 }, .cornerRadius = 12.0,
                                       .width = 600.0 },{
                            rayui::ForEach(items, [](const std::string& item) {
                                return rayui::HStack({ .spacing = 12.0, .alignment = rayui::Align::Center },{
                                    rayui::Circle(5,{ 124, 140, 255, 255 }),
                                    rayui::Label(item,{ .fontSize = 30.0, .color = WHITE })
                                });
                            },{ .spacing = 10.0 })
                        })
                        }));

                    // -------- Table --------
                    auto table_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                        rayui::VStack({ .spacing = 32.0, .alignment = rayui::Align::Center }, {

                            rayui::Label("Table",{ .fontSize = 72.0, .color = WHITE }),
                            rayui::Label("Scrollable, striped, selectable. Sticky header. Right-align numeric columns.",
                                { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                            rayui::Table(
                                {
                                    rayui::Column("Name",{ .width = 300.0 }),
                                    rayui::Column("Category",{ .width = 200.0 }),
                                    rayui::Column("Size",{ .width = 140.0, .align = rayui::Align::End }),
                                    rayui::Column("Modified",{ .flex = 1.0 })
                                },
                                {
                                    { { "src/main.cpp", "Source", "12.4 KB", "2 minutes ago" } },
                                    { { "src/rayui.hpp", "Source", "184 KB", "1 hour ago" } },
                                    { { "examples/demo", "Example", "24 KB", "3 hours ago" } },
                                    { { "assets/checker.png", "Image", "8.2 KB", "yesterday" } },
                                    { { "build/main.exe", "Binary", "1.2 MB", "5 minutes ago" } },
                                    { { "README.md", "Doc", "4 KB", "2 weeks ago" } },
                                    { { "LICENSE", "Doc", "1 KB", "3 months ago" } },
                                    { { "docs/guide.md", "Doc", "32 KB", "1 month ago" } },
                                    { { "docs/api.md", "Doc", "48 KB", "1 month ago" } },
                                    { { "tests/rayui_test.cpp", "Test", "56 KB", "5 days ago" } },
                                    { { "tests/fixtures.json", "Test", "12 KB", "5 days ago" } },
                                    { { "scripts/build.py", "Script", "3 KB", "2 weeks ago" } },
                                    { { "scripts/deploy.sh", "Script", "1.5 KB", "2 weeks ago" } },
                                    { { ".gitignore", "Config", "0.2 KB", "3 months ago" } },
                                    { { "CMakeLists.txt", "Config", "2 KB", "1 week ago" } },
                                    { { "Makefile", "Config", "1 KB", "1 week ago" } },
                                    { { "CHANGELOG.md", "Doc", "6 KB", "3 days ago" } },
                                    { { "CONTRIBUTING.md", "Doc", "5 KB", "1 month ago" } },
                                    { { "package-lock.json", "Config", "128 KB", "2 days ago" } },
                                    { { "package.json", "Config", "1 KB", "2 days ago" } }
                                },
                                { .width = 1000.0, .height = 480.0 })
                            .onRowClick([](int row) { TraceLog(LOG_INFO, "row %d clicked", row); })
                            }));

                        // -------- Shapes --------
                        auto shapes_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                            rayui::VStack({ .spacing = 32.0, .alignment = rayui::Align::Center }, {

                                rayui::Label("Shape builder",{ .fontSize = 72.0, .color = WHITE }),
                                rayui::Label("All built with rayui::Rect / Circle / Outline / Custom",
                                    { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                                rayui::Label("Rect",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                rayui::HStack({ .spacing = 16.0, .alignment = rayui::Align::Center },{
                                    rayui::Rect(80.0, 80.0,{ 90, 170, 230, 255 }),
                                    rayui::Rect(80.0, 80.0,{ 220, 150, 70, 255 }, 16.0),
                                    rayui::Rect(160.0, 80.0,{ 70, 150, 90, 255 }, 40.0),
                                    rayui::Rect(80.0, 80.0,{ 220, 120, 180, 255 })
                                        .onClick([]() { rayui::toast("Rect clicked",{ .level = rayui::Toast_Level::Info }); })
                                }),

                                rayui::Divider(),

                                rayui::Label("Circle",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                rayui::HStack({ .spacing = 16.0, .alignment = rayui::Align::Center },{
                                    rayui::Circle(30.0,{ 220, 90, 90, 255 }),
                                    rayui::Circle(50.0,{ 230, 200, 90, 255 }),
                                    rayui::Circle(70.0,{ 170, 130, 230, 255 }),
                                    rayui::Circle(40.0,{ 140, 220, 130, 255 })
                                        .onClick([]() { rayui::toast("Circle tapped",{ .level = rayui::Toast_Level::Success }); })
                                }),

                                rayui::Divider(),

                                rayui::Label("Outline",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                rayui::HStack({ .spacing = 16.0, .alignment = rayui::Align::Center },{
                                    rayui::Outline(160.0, 80.0, WHITE, 2.0),
                                    rayui::Outline(160.0, 80.0,{ 90, 170, 230, 255 }, 6.0),
                                    rayui::Outline(200.0, 80.0,{ 220, 150, 70, 255 }, 4.0)
                                }),

                                rayui::Divider(),

                                rayui::Label("Custom (arbitrary draw callback)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                rayui::Custom({ .width = 600.0, .height = 140.0 }, [](const auto& r) {
                                    static const float heights[] = { 0.2f, 0.5f, 0.8f, 0.4f, 0.9f, 0.3f, 0.6f, 0.7f, 0.45f, 0.85f };
                                    const int count = static_cast<int>(sizeof(heights) / sizeof(heights[0]));
                                    const double bar_width = r.width / (count * 1.5);

                                    for (int i = 0; i < count; ++i) {
                                        double h = r.height * heights[i];
                                        double x = r.x + i * bar_width * 1.5;
                                        double y = r.y + r.height - h;
                                        rayui::mesh::draw(
                                            rayui::mesh::make_rect({ x, y, bar_width, h }, 4.0),
                                            Fade({ 124, 140, 255, 255 }, 0.5f + 0.5f * heights[i]));
                                    }
                                })
                                }));

                            // -------- Layout --------
                            auto layout_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                                rayui::VStack({ .spacing = 40.0, .alignment = rayui::Align::Center }, {

                                    rayui::Label("Layout extras",{ .fontSize = 72.0, .color = WHITE }),

                                    rayui::Label("Wrap (flows chips onto new rows automatically)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::Wrap({ .spacingX = 10.0, .spacingY = 10.0, .width = 900.0 },{
                                        rayui::Chip("alpha",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("beta",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("gamma",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("delta",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("epsilon",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("zeta",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("eta",{ .fontSize = 28.0, .closeable = false }),
                                        rayui::Chip("theta",{ .fontSize = 28.0, .closeable = false })
                                    }),

                                    rayui::Divider(),

                                    rayui::Label("Reveal (animated show / hide)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::HStack({ .spacing = 16.0, .alignment = rayui::Align::Center },{
                                        rayui::Button("Toggle reveal",{ .padding = 16.0 })
                                            .onClick([shape_reveal]() { shape_reveal.set(!shape_reveal.get()); }),

                                        rayui::Reveal(shape_reveal,
                                            rayui::Rect(240.0, 80.0,{ 90, 170, 230, 255 }, 16.0),
                                            { .enterTime = 0.3, .exitTime = 0.2, .enterOffsetY = 16.0, .exitOffsetY = -16.0 })
                                    }),

                                    rayui::Divider(),

                                    rayui::Label("Accordion",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::Accordion(accordion_section,{
                                        rayui::AccordionItem("Account",
                                            rayui::VStack({ .spacing = 10.0, .alignment = rayui::Align::Start },{
                                                rayui::Label("Change password",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } }),
                                                rayui::Label("Two-factor authentication",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                            })),
                                        rayui::AccordionItem("Appearance",
                                            rayui::VStack({ .spacing = 10.0, .alignment = rayui::Align::Start },{
                                                rayui::Label("Theme: dark",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } }),
                                                rayui::Label("Compact mode",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                            })),
                                        rayui::AccordionItem("Notifications",
                                            rayui::VStack({ .spacing = 10.0, .alignment = rayui::Align::Start },{
                                                rayui::Label("Email",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } }),
                                                rayui::Label("Push",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                            }))
                                        },
                                        { .fontSize = 40.0, .gap = 8.0, .animateTime = 0.22, .width = 900.0 }),

                                    rayui::Divider(),

                                    rayui::Label("Tree view",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::TreeView({
                                        rayui::TreeNode("src",{
                                            rayui::TreeNode("main.cpp"),
                                            rayui::TreeNode("utils",{
                                                rayui::TreeNode("utils.hpp"),
                                                rayui::TreeNode("utils.cpp")
                                            })
                                        }),
                                        rayui::TreeNode("README.md")
                                    },{
                                        .fontSize = 30.0,
                                        .width = 600.0,
                                        .height = 300.0,
                                        .defaultExpanded = true
                                    })
                                    .onSelect([tree_selection](const std::vector<int>& path) {
                                        std::string s = "Selected path:";
                                        for (int i : path) s += " " + std::to_string(i);
                                        tree_selection.set(s);
                                    }),

                                    rayui::Label(rayui::fmt("{}", tree_selection),{ .fontSize = 28.0, .color = { 108, 112, 121, 255 } })
                                    }));

                                // -------- Overlays --------
                                auto overlays_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                                    rayui::VStack({ .spacing = 32.0, .alignment = rayui::Align::Center, .justify = rayui::Align::Center }, {

                                    rayui::Label("Overlays",{ .fontSize = 72.0, .color = WHITE }),

                                    rayui::Label("Try tabbing through the buttons below to see focus rings.",{ .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                                    rayui::Button("Open modal",{ .padding = 20.0 })
                                        .onClick([show_modal]() { show_modal.set(true); }),

                                    rayui::Divider(),

                                    rayui::Label("Popover",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::Popover(popover_open,
                                        rayui::Button("Open popover",{ .padding = 20.0 })
                                            .onClick([popover_open]() { popover_open.toggle(); }),
                                        rayui::VStack({ .spacing = 12.0, .alignment = rayui::Align::Start },{
                                            rayui::Label("Floating content",{ .fontSize = 30.0, .color = WHITE }),
                                            rayui::Button("Close",{ .padding = 12.0 })
                                                .onClick([popover_open]() { popover_open.set(false); })
                                        }),
                                        { .side = rayui::Side::Bottom, .gap = 8.0, .padding = 16.0 }),

                                    rayui::Divider(),

                                    rayui::Label("Context menu (right-click the box)",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::ContextMenu(context_choice,
                                        { "Cut", "Copy", "Paste", "Delete" },
                                        rayui::Rect(420.0, 100.0,{ 31, 33, 40, 255 }, 12.0),
                                        { .fontSize = 30.0, .padding = 12.0, .rowPadding = 10.0 })
                                        .onSelect([&](int idx) {
                                            switch (idx) {
                                            case 0: rayui::toast("Cut",{ .level = rayui::Toast_Level::Info }); break;
                                            case 1: rayui::toast("Copy",{ .level = rayui::Toast_Level::Success }); break;
                                            case 2: rayui::toast("Paste",{ .level = rayui::Toast_Level::Warning }); break;
                                            case 3:
                                                confirm_delete_open.set(true);
                                                break;
                                            }
                                            }),

                                    rayui::Label(
                                        rayui::fmt("Selected: {}", context_choice),
                                        { .fontSize = 28.0, .color = { 108, 112, 121, 255 } }),

                                    rayui::Divider(),

                                    rayui::Label("Toasts",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                    rayui::HStack({ .spacing = 12.0, .alignment = rayui::Align::Center },{
                                        rayui::Button("Info",{ .padding = 16.0 })
                                            .onClick([]() { rayui::toast("This is an information message.",{ .level = rayui::Toast_Level::Info }); }),
                                        rayui::Button("Success",{ .padding = 16.0 })
                                            .onClick([]() { rayui::toast("Saved successfully.",{ .level = rayui::Toast_Level::Success }); }),
                                        rayui::Button("Warning",{ .padding = 16.0 })
                                            .onClick([]() { rayui::toast("Low disk space.",{ .level = rayui::Toast_Level::Warning }); }),
                                        rayui::Button("Error",{ .padding = 16.0 })
                                            .onClick([]() { rayui::toast("Something went wrong.",{ .level = rayui::Toast_Level::Error, .duration = 5.0 }); })
                                    })
                                        }));

                                    // -------- Hotkeys & Polish --------
                                    std::vector<rayui::Element> fade_rows;
                                    for (int i = 1; i <= 25; ++i) {
                                        fade_rows.push_back(rayui::HStack({ .spacing = 14.0, .alignment = rayui::Align::Center }, {
                                            rayui::Badge(std::to_string(i),{ .fontSize = 22.0, .paddingX = 12.0, .paddingY = 4.0,
                                                                              .backgroundColor = { 124, 140, 255, 255 } }),
                                            rayui::Label("Row " + std::to_string(i) + ": scroll this box, then move the mouse away",
                                                { .fontSize = 24.0, .color = { 140, 144, 152, 255 } })
                                            }));
                                    }

                                    auto fade_demo = rayui::ScrollView({
                                        .width = 900.0,
                                        .height = 280.0,
                                        .scrollbarWidth = 12.0,
                                        .scrollbarMinWidth = 3.0,
                                        .scrollbarFadeTime = 0.6,
                                        .scrollbarIdleTime = 1.0
                                        }, rayui::VStack({ .spacing = 8.0 }, std::move(fade_rows)));

                                    auto hotkeys_page = rayui::ScrollView({ .width = 1400.0, .height = 860.0 },
                                        rayui::VStack({ .spacing = 40.0, .alignment = rayui::Align::Center }, {

                                            rayui::Label("Hotkeys & Polish",{ .fontSize = 72.0, .color = WHITE }),
                                            rayui::Label("Global hotkeys, fade-to-line scrollbars, and drop shadows on every overlay",
                                                { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                                            rayui::Label("Global hotkeys",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                            rayui::Label("These work regardless of which tab or widget has focus. Try them right now:",
                                                { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                                            rayui::Panel({
                                                .padding = 24.0,
                                                .backgroundColor = { 26, 28, 34, 255 },
                                                .cornerRadius = 12.0,
                                                .width = 720.0
                                            },{
                                                rayui::VStack({ .spacing = 16.0, .alignment = rayui::Align::Start },{
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("Ctrl + S",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                                    .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("Save",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("Ctrl + Shift + S",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                                           .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("Save As...",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("Ctrl + O",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                                    .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("Open file",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("Ctrl + N",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                                    .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("New file",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("F1",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                             .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("Help",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("F2",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                             .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("Rename",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Badge("Ctrl + Q",{ .fontSize = 26.0, .paddingX = 16.0, .paddingY = 8.0,
                                                                                    .backgroundColor = { 124, 140, 255, 255 } }),
                                                        rayui::Label("Quit",{ .fontSize = 30.0, .color = { 140, 144, 152, 255 } })
                                                    })
                                                })
                                            }),

                                            rayui::Divider(),

                                            rayui::Label("Scrollbar fade",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                            rayui::Label("The bar fades to a thin line after a moment of inactivity, and expands on hover.",
                                                { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                                            fade_demo,

                                            rayui::HStack({ .spacing = 12.0, .alignment = rayui::Align::Center },{
                                                rayui::Button("Scroll to top",{ .padding = 14.0 })
                                                    .onClick([fade_demo]() { fade_demo->scroll_to(0.0); }),
                                                rayui::Button("Scroll to bottom",{ .padding = 14.0 })
                                                    .onClick([fade_demo]() { fade_demo->scroll_to(1.0e9); })
                                            }),

                                            rayui::Divider(),

                                            rayui::Label("Drop shadows",{ .fontSize = 40.0, .color = { 140, 144, 152, 255 } }),
                                            rayui::Label("Every floating overlay: modal, popover, context menu, dropdown list, tooltip, toast: casts a soft shadow now.",
                                                { .fontSize = 26.0, .color = { 108, 112, 121, 255 } }),

                                            rayui::HStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{

                                                rayui::Popover(polish_popover,
                                                    rayui::Button("Open popover",{ .padding = 18.0 })
                                                        .onClick([polish_popover]() { polish_popover.set(!polish_popover.get()); }),
                                                    rayui::VStack({ .spacing = 10.0, .alignment = rayui::Align::Start },{
                                                        rayui::Label("Shadowed popover",{ .fontSize = 30.0, .color = WHITE }),
                                                        rayui::Label("See the shadow?",{ .fontSize = 24.0, .color = { 140, 144, 152, 255 } })
                                                    }),
                                                    { .side = rayui::Side::Bottom, .gap = 8.0, .padding = 16.0 }),

                                                rayui::Button("Open modal",{ .padding = 18.0 })
                                                    .onClick([show_modal]() { show_modal.set(true); }),

                                                rayui::Tooltip("Tooltips cast a shadow too",
                                                    rayui::Button("Hover me",{ .padding = 18.0 })),

                                                rayui::ContextMenu(polish_menu,
                                                    { "Rename", "Duplicate", "Delete" },
                                                    rayui::Button("Right-click me",{ .padding = 18.0 }),
                                                    { .fontSize = 28.0, .padding = 12.0, .rowPadding = 10.0 })
                                            })
                                            }));

                                        // -------- Root --------
                                        auto modal_content = rayui::Panel({
                                            .padding = 24.0,
                                            .backgroundColor = { 26, 28, 34, 255 },
                                            .cornerRadius = 16.0
                                            }, {
                                                rayui::VStack({ .spacing = 10.0, .alignment = rayui::Align::Center },{
                                                    rayui::Label("Confirm",{ .fontSize = 48.0, .color = WHITE }),
                                                    rayui::Label("Do you really want to delete the file?",
                                                        { .fontSize = 30.0, .color = { 140, 144, 152, 255 }, .width = -100.0, .height = 80.0 }),

                                                    rayui::HStack({ .spacing = 16.0, .alignment = rayui::Align::Center },{
                                                        rayui::Button("Cancel",{ .padding = 16.0 })
                                                            .onClick([show_modal]() { show_modal.set(false); }),
                                                        rayui::Button("Confirm",{ .padding = 16.0 })
                                                            .onClick([show_modal]() {
                                                                show_modal.set(false);
                                                                rayui::toast("Confirmed!",{ .level = rayui::Toast_Level::Success });
                                                            })
                                                    })
                                                })
                                            });

                                            auto confirm_delete_content = rayui::Panel({
                                                .padding = 24.0,
                                                .backgroundColor = { 26, 28, 34, 255 },
                                                .cornerRadius = 16.0
                                                }, {
                                                    rayui::VStack({ .spacing = 20.0, .alignment = rayui::Align::Center },{
                                                        rayui::Label("Delete this item?",{ .fontSize = 44.0, .color = WHITE }),
                                                        rayui::Label("This can't be undone.",
                                                            { .fontSize = 28.0, .color = { 140, 144, 152, 255 }, .width = 460.0 }),

                                                        rayui::HStack({ .spacing = 16.0, .alignment = rayui::Align::Center },{
                                                            rayui::Button("Cancel",{ .padding = 16.0 })
                                                                .onClick([confirm_delete_open]() { confirm_delete_open.set(false); }),
                                                            rayui::Button("Delete",{
                                                                    .padding = 16.0,
                                                                    .backgroundColor = { 200, 60, 70, 255 },
                                                                    .hoverColor = { 225, 80, 90, 255 },
                                                                    .pressedColor = { 170, 45, 55, 255 }
                                                                })
                                                                .onClick([confirm_delete_open]() {
                                                                    confirm_delete_open.set(false);
                                                                    rayui::toast("Deleted.",{ .level = rayui::Toast_Level::Error });
                                                                })
                                                        })
                                                    })
                                                });

                                                auto root = rayui::Panel({
                                                    .padding = 60.0,
                                                    .backgroundColor = { 15, 15, 18, 255 }
                                                    },
    {
        rayui::Positioned({ .anchor = rayui::Anchor::TopRight, .x = -20.0, .y = -30.0 },
            rayui::Button("X",{
                .padding = 16.0,
                .fontSize = 80.0,
                .backgroundColor = { 200, 60, 70, 255 },
                .hoverColor = { 225, 80, 90, 255 },
                .pressedColor = { 170, 45, 55, 255 },
                .width = 80.0,
                .height = 80.0
            }).onClick([&]() {
                CloseWindow();
            })
        ),

        rayui::Tabs(page,{
            rayui::Tab("Audio", audio_page),
            rayui::Tab("Notes", notes_page),
            rayui::Tab("Widgets", widgets_page),
            rayui::Tab("Extras", extras_page),
            rayui::Tab("Inputs", inputs_page),
            rayui::Tab("List", list_page),
            rayui::Tab("Table", table_page),
            rayui::Tab("Shapes", shapes_page),
            rayui::Tab("Layout", layout_page),
            rayui::Tab("Overlays", overlays_page),
            rayui::Tab("Hotkeys", hotkeys_page)
        }),

        rayui::Modal(show_modal, modal_content,{
            .dismissOnBackdrop = true,
            .dismissOnEscape = true,
            .padding = 24.0,
            .cornerRadius = 16.0,
            .contentWidth = 580.0,
            .fadeTime = 0.18
        }),

        rayui::Modal(confirm_delete_open, confirm_delete_content,{
            .dismissOnBackdrop = true,
            .dismissOnEscape = true,
            .padding = 24.0,
            .cornerRadius = 16.0,
            .contentWidth = 520.0,
            .fadeTime = 0.18
        }),

        rayui::ToastHost({
            .anchor = rayui::Anchor::BottomRight,
            .marginX = 24.0,
            .marginY = 24.0,
            .gap = 12.0
        }),

        rayui::Positioned({ .anchor = rayui::Anchor::BottomRight, .x = -20.0, .y = -20.0 },
            rayui::Label("rayui",{ .fontSize = 28.0, .color = { 108, 112, 121, 255 } }))
    });

    while (!WindowShouldClose()) {
        root.update(GetMousePosition(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT));

        BeginDrawing();
        ClearBackground(BLACK);
        root.draw();
        EndDrawing();
    }

    UnloadTexture(checker);
    rayui::font.unload();
    CloseWindow();
    return 0;
}