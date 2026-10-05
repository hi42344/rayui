#pragma once

#include "core.hpp"

namespace rayui {
    namespace detail {

        inline Color mix(Color a, Color b, double t) {
            t = std::clamp(t, 0.0, 1.0);
            auto lerp = [t](unsigned char x, unsigned char y) {
                return static_cast<unsigned char>(std::lround(x + (y - x) * t));
                };
            return { lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a) };
        }

        inline size_t utf8_prev(const std::string& s, size_t i) {
            if (i == 0) return 0;
            --i;
            while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
            return i;
        }

        inline size_t utf8_next(const std::string& s, size_t i) {
            if (i >= s.size()) return s.size();
            ++i;
            while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
            return i;
        }

        inline size_t utf8_count(const std::string& s) {
            size_t count = 0;
            for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++count;
            return count;
        }

        inline int utf8_decode(const std::string& s, size_t i, size_t* length) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1)));
            if (i + n > s.size()) n = 1;
            *length = n;
            if (n == 1) return c;

            int cp = c & (0xFF >> (n + 1));
            for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
            return cp;
        }

        inline std::string trim_right(std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
            return s;
        }

        struct Text_Line {
            size_t start = 0;
            size_t end = 0;
            double width = 0.0;
        };

        inline std::vector<Text_Line> wrap_text(const std::string& text, double max_width, double size) {
            constexpr size_t NONE = static_cast<size_t>(-1);
            std::vector<Text_Line> lines;
            size_t line_start = 0;
            double width = 0.0;
            size_t last_break = NONE;
            double width_at_break = 0.0;

            size_t i = 0;
            while (i < text.size()) {
                size_t length = 1;
                int codepoint = utf8_decode(text, i, &length);

                if (codepoint == '\n') {
                    lines.push_back({ line_start, i, width });
                    i += length;
                    line_start = i;
                    width = 0.0;
                    last_break = NONE;
                    continue;
                }

                const double advance = font.advance(codepoint, size);
                const bool is_space = codepoint == ' ' || codepoint == '\t';

                while (!is_space && i > line_start && width + advance > max_width + 1e-9) {
                    if (last_break != NONE) {
                        lines.push_back({ line_start, last_break, width_at_break });
                        width -= width_at_break;
                        line_start = last_break;
                        last_break = NONE;
                    }
                    else {
                        lines.push_back({ line_start, i, width });
                        line_start = i;
                        width = 0.0;
                    }
                }

                width += advance;
                if (is_space) {
                    last_break = i + length;
                    width_at_break = width;
                }
                i += length;
            }

            lines.push_back({ line_start, text.size(), width });
            return lines;
        }

        class Wrap_Cache {
        public:
            const std::vector<Text_Line>& get(const std::string& text, double width, double size) const {
                if (!m_valid || m_width != width || m_size != size || m_revision != font.revision() ||
                    m_spacing != font.spacing || m_text != text) {
                    m_lines = wrap_text(text, width, size);
                    m_text = text;
                    m_width = width;
                    m_size = size;
                    m_revision = font.revision();
                    m_spacing = font.spacing;
                    m_valid = true;
                }
                return m_lines;
            }

        private:
            mutable std::vector<Text_Line> m_lines;
            mutable std::string m_text;
            mutable double m_width = 0.0;
            mutable double m_size = 0.0;
            mutable double m_spacing = 0.0;
            mutable unsigned m_revision = 0;
            mutable bool m_valid = false;
        };

        inline void begin_clip(const Rectangle& r) {
            int x = static_cast<int>(std::floor(r.x));
            int y = static_cast<int>(std::floor(r.y));
            int w = static_cast<int>(std::ceil(r.x + r.width)) - x;
            int h = static_cast<int>(std::ceil(r.y + r.height)) - y;
            BeginScissorMode(x, y, std::max(w, 0), std::max(h, 0));
        }

        inline void push_clip(const coordinate::rect& r) {
            Vector2 a = Internal::mapper.logical_to_screen({ r.x, r.y });
            Vector2 b = Internal::mapper.logical_to_screen({ r.x + r.width, r.y + r.height });
            float x1 = a.x, y1 = a.y, x2 = b.x, y2 = b.y;

            if (!Internal::clip_stack.empty()) {
                const Rectangle& outer = Internal::clip_stack.back();
                x1 = std::max(x1, outer.x);
                y1 = std::max(y1, outer.y);
                x2 = std::min(x2, outer.x + outer.width);
                y2 = std::min(y2, outer.y + outer.height);
            }

            x2 = std::max(x2, x1);
            y2 = std::max(y2, y1);
            Internal::clip_stack.push_back({ x1, y1, x2 - x1, y2 - y1 });
            begin_clip(Internal::clip_stack.back());
        }

        inline void pop_clip() {
            if (Internal::clip_stack.empty()) return;

            Internal::clip_stack.pop_back();
            EndScissorMode();
            if (!Internal::clip_stack.empty()) begin_clip(Internal::clip_stack.back());
        }

        inline void poll_keyboard(Input_State& input) {
            for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
                int bytes = 0;
                const char* utf8 = CodepointToUTF8(codepoint, &bytes);
                input.typed.append(utf8, static_cast<size_t>(bytes));
            }

            auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
            const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

            Key_State& k = input.keys;
            k.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            k.ctrl = ctrl;
            k.up = pressed(KEY_UP);
            k.down = pressed(KEY_DOWN);
            k.backspace = pressed(KEY_BACKSPACE);
            k.del = pressed(KEY_DELETE);
            k.left = pressed(KEY_LEFT);
            k.right = pressed(KEY_RIGHT);
            k.home = pressed(KEY_HOME);
            k.end = pressed(KEY_END);
            k.enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
            k.escape = IsKeyPressed(KEY_ESCAPE);
            k.select_all = ctrl && IsKeyPressed(KEY_A);
            k.copy = ctrl && IsKeyPressed(KEY_C);
            k.cut = ctrl && IsKeyPressed(KEY_X);
            k.paste = ctrl && IsKeyPressed(KEY_V);
            k.tab = pressed(KEY_TAB);
            k.space = IsKeyPressed(KEY_SPACE);
        }

        struct Toast_Data {
            std::string text;
            Toast_Level level = Toast_Level::Info;
            double duration = 3.0;
            double fontSize = 30.0;
            double padding = 10.0;      // per-side inset (was hardcoded 18)
            double lineHeight = 1.25;
            double cornerRadius = 10.0;
            double maxWidth = 480.0;
            double age = 0.0;
            Animated fade{ 0.0 };
        };

        inline std::vector<std::shared_ptr<Toast_Data>> toasts;

        /// @brief Cached single-line text run used by all widgets that draw a
        /// single non-wrapping string (buttons, labels, chips, options, ...).
        class Text_Run {
        public:
            Text_Run(std::string text, double size)
                : m_text(std::move(text))
                , m_size(size) {
            }

            bool set_text(std::string text) {
                if (text == m_text) return false;
                m_text = std::move(text);
                m_valid = false;
                return true;
            }

            const std::string& text() const { return m_text; }
            double size() const { return m_size; }

            const Size& measured() const {
                if (!m_valid || m_revision != font.revision()) {
                    m_extent = font.measure(m_text, m_size);
                    m_revision = font.revision();
                    m_valid = true;
                }
                return m_extent;
            }

            void draw(coordinate::pos top_left, Color color) const {
                font.draw(m_text, top_left, m_size, color);
            }

        private:
            std::string m_text;
            double m_size;
            mutable Size m_extent;
            mutable unsigned m_revision = 0;
            mutable bool m_valid = false;
        };

    }
}