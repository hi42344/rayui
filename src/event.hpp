#pragma once
#include <functional>
#include <cstdint>
#include <mutex>

namespace rayui::event {
    class Connection {
    private:
        std::function<void()> m_disconnect;
        bool m_connected = false;

    public:
        Connection() = default;
        Connection(std::function<void()> disconnect_fn)
            : m_disconnect(std::move(disconnect_fn))
            , m_connected(true) {
        }

        void Disconnect() {
            if (m_connected && m_disconnect) {
                m_disconnect();
                m_connected = false;
            }
        }

        bool IsConnected() const { return m_connected; }
        ~Connection() {}
    };

    class ScopedConnection {
    private:
        Connection m_conn;

    public:
        ScopedConnection() = default;
        explicit ScopedConnection(Connection c)
            : m_conn(std::move(c)) {
        }
        ~ScopedConnection() { m_conn.Disconnect(); }

        ScopedConnection(const ScopedConnection&) = delete;
        ScopedConnection& operator=(const ScopedConnection&) = delete;

        ScopedConnection(ScopedConnection&&) = default;
        ScopedConnection& operator=(ScopedConnection&&) = default;
    };

    template <typename... Args>
    class Signal {
    private:
        using CallbackType = std::function<void(Args...)>;

        struct Listener {
            CallbackType callback;
            uint64_t id;
            bool once = false;
            int priority = 0;
        };

        std::vector<Listener> m_listeners;
        uint64_t m_next_id = 0;
        bool m_firing = false;
        bool m_paused = false;
        std::vector<Listener> m_pending;

        mutable std::recursive_mutex m_mutex;
        std::shared_ptr<bool> m_lifetime_token = std::make_shared<bool>(true);

        void flush_pending() {
            for (auto& l : m_pending)
                m_listeners.push_back(std::move(l));
            m_pending.clear();
        }

        Connection make_connection(uint64_t id) {
            std::weak_ptr<bool> weak_lifetime = m_lifetime_token;

            return Connection([this, id, weak_lifetime]() {
                if (auto pin = weak_lifetime.lock()) {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);

                    auto it = std::find_if(m_listeners.begin(), m_listeners.end(),
                        [id](const Listener& l) { return l.id == id; });
                    if (it != m_listeners.end()) {
                        m_listeners.erase(it);
                        return;
                    }

                    auto pending_it = std::find_if(m_pending.begin(), m_pending.end(),
                        [id](const Listener& l) { return l.id == id; });
                    if (pending_it != m_pending.end()) {
                        m_pending.erase(pending_it);
                    }
                }
                });
        }

    public:
        Signal() = default;

        ~Signal() {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            *m_lifetime_token = false;
        }

        Signal(const Signal&) = delete;
        Signal& operator=(const Signal&) = delete;

        Connection Connect(CallbackType callback, int priority = 0) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            uint64_t id = m_next_id++;
            Listener l{ std::move(callback), id, false, priority };

            if (m_firing)
                m_pending.push_back(std::move(l));
            else
                m_listeners.push_back(std::move(l));

            return make_connection(id);
        }

        Connection Once(CallbackType callback, int priority = 0) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            uint64_t id = m_next_id++;
            Listener l{ std::move(callback), id, true, priority };

            if (m_firing)
                m_pending.push_back(std::move(l));
            else
                m_listeners.push_back(std::move(l));

            return make_connection(id);
        }

        Connection ConnectIf(std::function<bool(Args...)> predicate,
            CallbackType callback,
            int priority = 0) {
            return Connect(
                [predicate = std::move(predicate),
                callback = std::move(callback)](Args... args) {
                    if (predicate(args...))
                        callback(args...);
                },
                priority);
        }

        void Fire(Args... args) {
            std::vector<Listener> listeners_snapshot;
            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                if (m_paused)
                    return;

                if (!m_pending.empty()) {
                    flush_pending();
                }

                std::stable_sort(m_listeners.begin(), m_listeners.end(),
                    [](const Listener& a, const Listener& b) {
                        return a.priority > b.priority;
                    });

                if (m_listeners.empty())
                    return;

                m_firing = true;
                listeners_snapshot = m_listeners;
            }

            std::vector<uint64_t> to_remove;

            for (auto& l : listeners_snapshot) {
                if (l.callback)
                    l.callback(args...);
                if (l.once)
                    to_remove.push_back(l.id);
            }

            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                m_firing = false;

                for (uint64_t id : to_remove) {
                    m_listeners.erase(
                        std::remove_if(m_listeners.begin(), m_listeners.end(),
                            [id](const Listener& l) { return l.id == id; }),
                        m_listeners.end());
                }
            }
        }

        template <typename ExecutorFunc>
        void FireAsync(ExecutorFunc&& enqueue_fn, Args... args) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (m_paused)
                return;

            if (!m_pending.empty()) {
                flush_pending();
            }
            std::stable_sort(m_listeners.begin(), m_listeners.end(),
                [](const Listener& a, const Listener& b) {
                    return a.priority > b.priority;
                });

            auto args_tuple = std::make_tuple(args...);

            for (auto& l : m_listeners) {
                if (l.callback) {
                    enqueue_fn([cb = l.callback, args_tuple]() {
                        std::apply(cb, args_tuple);
                        });
                }
                if (l.once) {
                    make_connection(l.id).Disconnect();
                }
            }
        }

        void Pause() {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_paused = true;
        }

        void Resume() {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_paused = false;
        }

        bool IsPaused() const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return m_paused;
        }

        void DisconnectAll() {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_listeners.clear();
            m_pending.clear();
        }

        size_t ListenerCount() const {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            return m_listeners.size() + m_pending.size();
        }
    };

    template <typename... Args>
    class Relay {
    private:
        std::vector<Signal<Args...>*> m_targets;

    public:
        void Add(Signal<Args...>& signal) { m_targets.push_back(&signal); }
        void Remove(Signal<Args...>& signal) {
            m_targets.erase(std::remove(m_targets.begin(), m_targets.end(), &signal),
                m_targets.end());
        }
        void Fire(Args... args) {
            for (auto* s : m_targets)
                if (s)
                    s->Fire(args...);
        }
    };

} // namespace event