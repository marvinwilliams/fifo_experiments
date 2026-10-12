#pragma once

#include "util/base.hpp"

#include "dcbo/dcbo_shim.h"

#include <cxxopts.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ostream>

namespace wrapper::dcbo {

class DCBO {
   public:
    using value_type = base::value_type;
    // An LCRQ cell holding UINT64_MAX is empty
    static constexpr int value_bits = 63;

   private:
    struct Settings {
        double queues_per_thread = 1.0;
        int choices = 2;

        void register_cmd_options(cxxopts::Options& cmd) {
            cmd.add_options()("c,queue-factor", "Number of partial queues per thread",
                              cxxopts::value<double>(queues_per_thread), "NUMBER");
            cmd.add_options()("d,choices", "Number of random partial queues compared per operation",
                              cxxopts::value<int>(choices), "NUMBER");
        }

        [[nodiscard]] bool validate() const {
            if (!(queues_per_thread > 0.0)) {
                std::cerr << "Error: Queue factor must be positive\n";
                return false;
            }
            if (choices < 1) {
                std::cerr << "Error: Choices must be at least 1\n";
                return false;
            }
            return true;
        }

        void write_human_readable(std::ostream& out) const {
            out << "Queue factor: " << queues_per_thread << '\n';
            out << "Choices: " << choices << '\n';
        }

        template <typename JsonObject>
        void write_json(JsonObject& obj) const {
            obj.entry("queue_factor", queues_per_thread);
            obj.entry("choices", choices);
        }
    };

    // Owns the thread-local state of the d-CBO, so it must live on the thread that created it
    class Handle {
        friend DCBO;
        void* queue_;
        bool owns_thread_state_;

        Handle(void* queue, int thread_id) : queue_{queue}, owns_thread_state_{true} {
            dcbo_thread_init(queue_, thread_id);
        }

       public:
        using value_type = DCBO::value_type;

        Handle(Handle const&) = delete;
        Handle& operator=(Handle const&) = delete;
        Handle& operator=(Handle&&) = delete;

        Handle(Handle&& other) noexcept : queue_{other.queue_}, owns_thread_state_{other.owns_thread_state_} {
            other.owns_thread_state_ = false;
        }

        ~Handle() {
            if (owns_thread_state_) {
                dcbo_thread_free();
            }
        }

        bool push(value_type value) {
            return dcbo_enqueue(queue_, value) != 0;
        }

        std::optional<value_type> try_pop() {
            auto value = dcbo_dequeue(queue_);
            if (value == 0) {
                return std::nullopt;
            }
            return value;
        }
    };

    static std::uint32_t num_queues(int num_threads, Settings const& settings) {
        auto n = std::lround(static_cast<double>(num_threads) * settings.queues_per_thread);
        return static_cast<std::uint32_t>(std::max(1L, n));
    }

    void* queue_;
    std::atomic_int next_tid_{0};

   public:
    using settings_type = Settings;
    using handle_type = Handle;

    // The partial queues are allocated from per-thread arenas that are only released at exit
    DCBO(int num_threads, std::size_t /*capacity*/, Settings const& settings)
        : queue_{dcbo_create(num_queues(num_threads, settings), static_cast<std::uint32_t>(settings.choices),
                             num_threads)} {
    }

    static void write_human_readable(std::ostream& out) {
        out << "d-CBO (LCRQ partial queues)\n";
    }

    // Must be called on the thread that uses the handle
    Handle get_handle() {
        return Handle{queue_, next_tid_.fetch_add(1, std::memory_order_relaxed)};
    }
};

}  // namespace wrapper::dcbo
