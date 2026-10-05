#pragma once
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

/// Dont Use
struct BezierPoint {
    float x, y;
};
/// Dont Use
struct G_Bezier_Params {
    std::vector<BezierPoint> control_points;
};

namespace tween {
    enum class EasingStyle {
        Linear,
        Sine,
        Quad,
        Cubic,
        Quart,
        Quint,
        Expo,
        Circ,
        Back,
        Elastic,
        Bounce,
        Bezier
    };

    enum class EasingDirection {
        In,
        Out,
        InOut
    };

    /// repeat_count: 0 = play once, -1 = infinite
    struct TweenInfo {
        float time = 1.0f;
        EasingStyle easing_style = EasingStyle::Linear;
        EasingDirection easing_direction = EasingDirection::In;
        int repeat_count = 0;
        bool reverses = false;
        float delay_time = 0.0f;

        G_Bezier_Params bezier_params;
    };

    extern G_Bezier_Params BezierParams;

    inline float deCasteljau(float t, const std::vector<float>& coords) {
        std::vector<float> pts = coords;
        int n = pts.size();
        for (int r = 1; r < n; ++r)
            for (int i = 0; i < n - r; ++i)
                pts[i] = (1.0f - t) * pts[i] + t * pts[i + 1];
        return pts[0];
    }

    inline float EvaluateBezier(float x, const G_Bezier_Params& params) {
        if (x <= 0.0f) return 0.0f;
        if (x >= 1.0f) return 1.0f;

        std::vector<float> xs = { 0.0f };
        std::vector<float> ys = { 0.0f };
        for (auto& p : params.control_points) {
            xs.push_back(p.x);
            ys.push_back(p.y);
        }
        xs.push_back(1.0f);
        ys.push_back(1.0f);

        float t = x, t_min = 0.0f, t_max = 1.0f;
        for (int i = 0; i < 12; ++i) {
            float bx = deCasteljau(t, xs) - x;

            float dt = 1e-4f;
            float dbx = (deCasteljau(t + dt, xs) - deCasteljau(t - dt, xs)) / (2.0f * dt);

            if (bx > 0.0f) t_max = t; else t_min = t;

            if (std::abs(dbx) > 1e-6f) {
                t -= bx / dbx;
                if (t < t_min || t > t_max)
                    t = 0.5f * (t_min + t_max);
            }
            else {
                t = 0.5f * (t_min + t_max);
            }
        }

        return deCasteljau(t, ys);
    }

    inline float evaluate_ease(float t, const tween::TweenInfo& info) {
        if (t <= 0.0f)
            return 0.0f;
        if (t >= 1.0f)
            return 1.0f;
        constexpr float EASE_PI = 3.1415926535f;

        switch (info.easing_style) {
        case tween::EasingStyle::Linear:
            return t;

        case tween::EasingStyle::Sine:
            if (info.easing_direction == tween::EasingDirection::In)
                return 1.0f - std::cos(t * EASE_PI * 0.5f);
            if (info.easing_direction == tween::EasingDirection::Out)
                return std::sin(t * EASE_PI * 0.5f);
            return -(std::cos(EASE_PI * t) - 1.0f) * 0.5f;

        case tween::EasingStyle::Quad:
            if (info.easing_direction == tween::EasingDirection::In)
                return t * t;
            if (info.easing_direction == tween::EasingDirection::Out)
                return 1.0f - (1.0f - t) * (1.0f - t);
            return t < 0.5f ? 2.0f * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;

        case tween::EasingStyle::Cubic:
            if (info.easing_direction == tween::EasingDirection::In)
                return t * t * t;
            if (info.easing_direction == tween::EasingDirection::Out)
                return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            return t < 0.5f ? 4.0f * t * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;

        case tween::EasingStyle::Quart:
            if (info.easing_direction == tween::EasingDirection::In)
                return t * t * t * t;
            if (info.easing_direction == tween::EasingDirection::Out)
                return 1.0f - std::pow(1.0f - t, 4.0f);
            return t < 0.5f ? 8.0f * t * t * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) * 0.5f;

        case tween::EasingStyle::Quint:
            if (info.easing_direction == tween::EasingDirection::In)
                return t * t * t * t * t;
            if (info.easing_direction == tween::EasingDirection::Out)
                return 1.0f - std::pow(1.0f - t, 5.0f);
            return t < 0.5f ? 16.0f * t * t * t * t * t
                : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) * 0.5f;

        case tween::EasingStyle::Expo:
            if (t == 0.0f)
                return 0.0f;
            if (t == 1.0f)
                return 1.0f;
            if (info.easing_direction == tween::EasingDirection::In)
                return std::pow(2.0f, 10.0f * t - 10.0f);
            if (info.easing_direction == tween::EasingDirection::Out)
                return 1.0f - std::pow(2.0f, -10.0f * t);
            return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
                : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;

        case tween::EasingStyle::Circ:
            if (info.easing_direction == tween::EasingDirection::In)
                return 1.0f - std::sqrt(1.0f - t * t);
            if (info.easing_direction == tween::EasingDirection::Out)
                return std::sqrt(1.0f - std::pow(t - 1.0f, 2.0f));
            return t < 0.5f
                ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) * 0.5f
                : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) * 0.5f;

        case tween::EasingStyle::Back: {
            const float c1 = 1.70158f;
            const float c2 = c1 * 1.525f;
            const float c3 = c1 + 1.0f;
            if (info.easing_direction == tween::EasingDirection::In)
                return c3 * t * t * t - c1 * t * t;
            if (info.easing_direction == tween::EasingDirection::Out)
                return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
            return t < 0.5f
                ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
        }

        case tween::EasingStyle::Elastic: {
            const float c4 = (2.0f * EASE_PI) / 3.0f;
            const float c5 = (2.0f * EASE_PI) / 4.5f;
            if (t == 0.0f)
                return 0.0f;
            if (t == 1.0f)
                return 1.0f;
            if (info.easing_direction == tween::EasingDirection::In)
                return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
            if (info.easing_direction == tween::EasingDirection::Out)
                return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
            return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f
                : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) * 0.5f + 1.0f;
        }

        case tween::EasingStyle::Bounce: {
            auto bounceOut = [](float x) -> float {
                const float n1 = 7.5625f;
                const float d1 = 2.75f;
                if (x < 1.0f / d1) return n1 * x * x;
                if (x < 2.0f / d1) { x -= 1.5f / d1;   return n1 * x * x + 0.75f; }
                if (x < 2.5f / d1) { x -= 2.25f / d1;  return n1 * x * x + 0.9375f; }
                x -= 2.625f / d1;
                return n1 * x * x + 0.984375f;
                };
            if (info.easing_direction == tween::EasingDirection::In)
                return 1.0f - bounceOut(1.0f - t);
            if (info.easing_direction == tween::EasingDirection::Out)
                return bounceOut(t);
            return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) * 0.5f
                : (1.0f + bounceOut(2.0f * t - 1.0f)) * 0.5f;
        }

        case tween::EasingStyle::Bezier:
            return EvaluateBezier(t, info.bezier_params);
        default:
            return t;
        }
    }

    template <typename T>
    inline void apply_lerp(T& target, const T& start, const T& end, float alpha) {
        target = static_cast<T>(start * (1.0f - alpha) + end * alpha);
    }

    template <typename T>
    class Tween {
    private:
        T* m_target;
        T m_start;
        T m_end;
        TweenInfo m_info;
        float m_elapsed_time = 0.0f;
        int m_current_cycle = 0;
        bool m_is_playing = false;
        bool m_is_reversing_phase = false;

    public:
        Tween(T* target, const TweenInfo& info, const T& goal_value)
            : m_target(target)
            , m_start(*target)
            , m_end(goal_value)
            , m_info(info) {
        }

        void play(bool auto_update = false) {
            if (!m_target)
                return;
            m_start = *m_target;
            m_elapsed_time = -m_info.delay_time;
            m_current_cycle = 0;
            m_is_reversing_phase = false;
            m_is_playing = true;

            if (auto_update) {
                auto last_time = std::chrono::high_resolution_clock::now();

                while (m_is_playing) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<float> delta_time = current_time - last_time;
                    last_time = current_time;

                    update(delta_time.count());
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            }
        }

        void pause() { m_is_playing = false; }
        void resume(bool auto_update = false) {
            if (!m_target || m_is_playing)
                return;

            m_is_playing = true;

            if (auto_update) {
                auto last_time = std::chrono::high_resolution_clock::now();

                while (m_is_playing) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<float> delta_time = current_time - last_time;
                    last_time = current_time;

                    update(delta_time.count());
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            }
        }
        void stop() {
            m_is_playing = false;
            m_elapsed_time = 0.0f;
        }

        void update(float delta_time) {
            if (!m_is_playing || !m_target)
                return;
            m_elapsed_time += delta_time;
            if (m_elapsed_time < 0.0f)
                return;

            float progress = m_elapsed_time / m_info.time;
            if (progress > 1.0f)
                progress = 1.0f;

            float actual_alpha = m_is_reversing_phase ? (1.0f - progress) : progress;
            float eased_alpha = evaluate_ease(actual_alpha, m_info);
            apply_lerp(*m_target, m_start, m_end, eased_alpha);

            if (progress >= 1.0f) {
                if (m_info.reverses && !m_is_reversing_phase) {
                    m_is_reversing_phase = true;
                    m_elapsed_time = 0.0f;
                }
                else {
                    m_current_cycle++;
                    if (m_info.repeat_count == -1 || m_current_cycle <= m_info.repeat_count) {
                        m_is_reversing_phase = false;
                        m_elapsed_time = 0.0f;
                    }
                    else {
                        m_is_playing = false;
                    }
                }
            }
        }

        /// @brief Reverses the start and the end values
        void reverse() {
            std::swap(m_start, m_end);

            if (m_elapsed_time > 0.0f && m_elapsed_time < m_info.time) {
                m_elapsed_time = m_info.time - m_elapsed_time;
                if (m_info.reverses) {
                    m_is_reversing_phase = !m_is_reversing_phase;
                }
            }
        }

        bool is_playing() const { return m_is_playing; }
        bool is_done() const {
            if (m_is_playing)
                return false;

            if (m_info.repeat_count != -1 && m_current_cycle > m_info.repeat_count) {
                return true;
            }

            return false;
        }
    };

    class TweenService {
    public:
        template <typename T>
        static Tween<T> create(T& target,
            const tween::TweenInfo& info,
            const T& goal) {
            return Tween<T>(&target, info, goal);
        }
    };
} // namespace tween