#include "raylib.h"
#include "../src/rayui.hpp"

int main() {
    InitWindow(800, 600, "rayui — hello");
    SetExitKey(KEY_NULL);   // so Escape blurs a text field instead of quitting
    SetTargetFPS(60);

    // Reactive state. Any widget bound to it re-renders when it changes.
    auto name = rayui::make_state(std::string("world"));
    auto greet = rayui::make_state(std::string("Hello, world!"));

    // Declarative tree.
    auto root = rayui::Panel({ .padding = 40.0 }, {
        rayui::VStack({ .spacing = 20.0, .alignment = rayui::Align::Center,
                        .justify = rayui::Align::Center },{

            rayui::Label(greet,{ .fontSize = 56.0 }),

            rayui::Textbox(name,{ .placeholder = "Type a name…", .width = 400.0 }),

            rayui::Button("Say hello",{ .padding = 16.0 })
                .onClick([name, greet]() {
                    greet.set("Hello, " + name.get() + "!");
                })
        })
        });

    while (!WindowShouldClose()) {
        root.update(GetMousePosition(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT));

        BeginDrawing();
        ClearBackground({ 15, 15, 18, 255 });
        root.draw();
        EndDrawing();
    }

    rayui::font.unload();
    CloseWindow();
    return 0;
}