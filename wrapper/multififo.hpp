#pragma once

#include "util/base.hpp"

#if defined MF_MODE_RANDOM
#include "multififo/modes/random.hpp"
#elif defined MF_MODE_STICK_SWAP
#include "multififo/modes/stick_swap.hpp"
#else
#error "No valid mode specified"
#endif
#include "multififo/multififo.hpp"

#include <cxxopts.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ostream>
#include <ratio>
#include <utility>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace wrapper::multififo {

#ifdef MULTIFIFO_POP_CANDIDATES
static constexpr int pop_candidates = MULTIFIFO_POP_CANDIDATES;
#else
static constexpr int pop_candidates = 2;
#endif

#if defined MF_MODE_RANDOM
using mode_type = ::multififo::mode::Random<pop_candidates>;
static constexpr auto mode_name = "random";
#elif defined MF_MODE_STICK_SWAP
using mode_type = ::multififo::mode::StickSwap<pop_candidates>;
static constexpr auto mode_name = "stick_swap";
#endif

struct CycleClock {
    using rep = std::uint64_t;
    using period = std::ratio<1>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<CycleClock>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept {
#if defined(__aarch64__)
        rep ticks{};
        asm volatile("mrs %0, cntvct_el0" : "=r"(ticks));
        return time_point{duration{ticks}};
#else
        return time_point{duration{__rdtsc()}};
#endif
    }
};

#ifdef MULTIFIFO_STEADY_CLOCK
using clock_type = std::chrono::steady_clock;
static constexpr auto clock_name = "steady_clock";
#else
using clock_type = CycleClock;
static constexpr auto clock_name = "cycle";
#endif

class MultiFifo {
   public:
    using value_type = base::value_type;
    static constexpr int value_bits = base::value_bits;

   private:
    struct Policy {
        using mode_type = ::wrapper::multififo::mode_type;
        static constexpr int pop_tries = 1;
        static constexpr bool scan = true;
    };

    using queue_type = ::multififo::MultiFifo<value_type, Policy, clock_type>;

    struct Settings {
        int queues_per_thread = 2;
        typename queue_type::config_type config{};
        base::CapacitySettings capacity;

        void register_cmd_options(cxxopts::Options& cmd) {
            cmd.add_options()("c,queue-factor", "The number of queues per thread",
                              cxxopts::value<int>(queues_per_thread), "NUMBER");
#ifdef MF_MODE_STICK_SWAP
            cmd.add_options()("k,stickiness", "Number of operations the selected queues are used for",
                              cxxopts::value<int>(config.stickiness), "NUMBER");
#endif
            cmd.add_options()("mf-seed", "Seed for the MultiFIFO", cxxopts::value<int>(config.seed), "NUMBER");
            capacity.register_cmd_options(cmd);
        }

        [[nodiscard]] bool validate() const {
            if (queues_per_thread < 1) {
                std::cerr << "Error: Queue factor must be at least 1\n";
                return false;
            }
#ifdef MF_MODE_STICK_SWAP
            if (config.stickiness < 1) {
                std::cerr << "Error: Stickiness must be positive\n";
                return false;
            }
#endif
            return capacity.validate();
        }

        void write_human_readable(std::ostream& out) const {
            out << "Queue factor: " << queues_per_thread << '\n';
#ifdef MF_MODE_STICK_SWAP
            out << "Stickiness: " << config.stickiness << '\n';
#endif
            out << "MultiFIFO seed: " << config.seed << '\n';
            capacity.write_human_readable(out);
        }

        template <typename JsonObject>
        void write_json(JsonObject& obj) const {
            obj.entry("mode", mode_name);
            obj.entry("queue_factor", queues_per_thread);
#ifdef MF_MODE_STICK_SWAP
            obj.entry("stickiness", config.stickiness);
#endif
            obj.entry("seed", config.seed);
            obj.entry("pop_candidates", pop_candidates);
            obj.entry("clock", clock_name);
            capacity.write_json(obj);
        }
    };

    class Handle {
        friend MultiFifo;
        typename queue_type::handle_type handle_;

        explicit Handle(typename queue_type::handle_type handle) : handle_{std::move(handle)} {
        }

       public:
        using value_type = MultiFifo::value_type;

        bool push(value_type value) {
            return handle_.try_push(value);
        }

        std::optional<value_type> try_pop() {
            return handle_.try_pop();
        }
    };

    static std::size_t num_queues(int num_threads, Settings const& settings) {
        return static_cast<std::size_t>(num_threads) * static_cast<std::size_t>(settings.queues_per_thread);
    }

    queue_type queue_;

   public:
    using settings_type = Settings;
    using handle_type = Handle;

    MultiFifo(int num_threads, std::size_t capacity, Settings const& settings)
        : queue_{num_queues(num_threads, settings),
                 (settings.capacity.scaled(capacity) + num_queues(num_threads, settings) - 1) /
                     num_queues(num_threads, settings),
                 settings.config} {
    }

    static void write_human_readable(std::ostream& out) {
        out << "MultiFIFO\n";
        out << "  Mode: " << mode_name << '\n';
        out << "  Pop candidates: " << pop_candidates << '\n';
        out << "  Clock: " << clock_name << '\n';
    }

    Handle get_handle() {
        return Handle{queue_.get_handle()};
    }
};

}  // namespace wrapper::multififo
