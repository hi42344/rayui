#pragma once

#include <raylib.h>
#include <algorithm>
#include <vector>

namespace rayui::coordinate {

    struct pos {
        double x = 0.0;
        double y = 0.0;

        pos operator+(const pos& o) const { return { x + o.x, y + o.y }; }
        pos operator-(const pos& o) const { return { x - o.x, y - o.y }; }
    };

    struct rect {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;

        bool contains(pos p) const {
            return p.x >= x && p.x <= (x + width) &&
                p.y >= y && p.y <= (y + height);
        }
    };

    struct circle {
        pos center;
        double radius = 0.0;

        bool contains(pos p) const {
            double dx = p.x - center.x;
            double dy = p.y - center.y;
            return (dx * dx + dy * dy) <= (radius * radius);
        }
    };

    struct tri {
        pos a, b, c;

        bool contains(pos p) const {
            auto sign = [](pos p1, pos p2, pos p3) {
                return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
                };

            double d1 = sign(p, a, b);
            double d2 = sign(p, b, c);
            double d3 = sign(p, c, a);

            bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

            return !(has_neg && has_pos);
        }
    };

    struct tri_mesh {
        std::vector<tri> tris;
        rect bounds;

        void update_bounds() {
            if (tris.empty()) {
                bounds = { 0.0, 0.0, 0.0, 0.0 };
                return;
            }

            double min_x = tris[0].a.x, max_x = tris[0].a.x;
            double min_y = tris[0].a.y, max_y = tris[0].a.y;

            for (const auto& t : tris) {
                min_x = std::min({ min_x, t.a.x, t.b.x, t.c.x });
                max_x = std::max({ max_x, t.a.x, t.b.x, t.c.x });
                min_y = std::min({ min_y, t.a.y, t.b.y, t.c.y });
                max_y = std::max({ max_y, t.a.y, t.b.y, t.c.y });
            }

            bounds = { min_x, min_y, max_x - min_x, max_y - min_y };
        }

        bool contains(pos p) const {
            if (!bounds.contains(p)) return false;

            for (const auto& t : tris) {
                if (t.contains(p)) return true;
            }
            return false;
        }

        void translate(pos delta) {
            for (auto& t : tris) {
                t.a.x += delta.x; t.a.y += delta.y;
                t.b.x += delta.x; t.b.y += delta.y;
                t.c.x += delta.x; t.c.y += delta.y;
            }

            bounds.x += delta.x;
            bounds.y += delta.y;
        }
    };

    class mapper {
    private:
        double window_width = 800.0;
        double window_height = 600.0;
        double logical_width = 1920.0;
        double logical_height = 1080.0;
        double scale_factor = 1.0;
        double offset_x = 0.0;
        double offset_y = 0.0;
        double dpi_scale = 1.0;

    public:
        /// @brief Sets the design resolution (logical canvas size). Call rayui::request_layout() afterwards.
        void set_logical_size(double width, double height) {
            if (width < 1.0 || height < 1.0) return;
            logical_width = width;
            logical_height = height;
        }

        void update() {
            window_width = static_cast<double>(GetScreenWidth());
            window_height = static_cast<double>(GetScreenHeight());

            if (window_width < 1.0) window_width = 1.0;
            if (window_height < 1.0) window_height = 1.0;

            Vector2 scale = GetWindowScaleDPI();
            dpi_scale = static_cast<double>(scale.x);

            // Compute actual unscaled pixel space available
            double raw_w = window_width / dpi_scale;
            double raw_h = window_height / dpi_scale;

            // Fit the logical canvas inside the window, keeping its aspect ratio
            scale_factor = std::min(raw_w / logical_width, raw_h / logical_height);

            // Center the logical canvas inside the physical window
            offset_x = (raw_w - logical_width * scale_factor) / 2.0;
            offset_y = (raw_h - logical_height * scale_factor) / 2.0;
        }

        // Screen pixels -> logical space ([0, logical_width] x [0, logical_height])
        pos screen_to_logical(Vector2 screen_pos) const {
            double px = (static_cast<double>(screen_pos.x) / dpi_scale) - offset_x;
            double py = (static_cast<double>(screen_pos.y) / dpi_scale) - offset_y;
            return pos{ .x = px / scale_factor, .y = py / scale_factor };
        }

        // Logical space -> screen pixels
        Vector2 logical_to_screen(pos logical_pos) const {
            double px = (logical_pos.x * scale_factor + offset_x) * dpi_scale;
            double py = (logical_pos.y * scale_factor + offset_y) * dpi_scale;
            return Vector2{ .x = static_cast<float>(px), .y = static_cast<float>(py) };
        }

        float logical_to_screen_length(double logical_length) const {
            return static_cast<float>(logical_length * scale_factor * dpi_scale);
        }

        double screen_to_logical_length(float screen_length) const {
            return (static_cast<double>(screen_length) / dpi_scale) / scale_factor;
        }

        [[nodiscard]] double get_width() const { return logical_width; }
        [[nodiscard]] double get_height() const { return logical_height; }
        [[nodiscard]] double get_dpi_scale() const { return dpi_scale; }
    };

} // namespace coordinate