#pragma once

#include "util/base.hpp"

#include <cxxopts.hpp>

#include "multififo/multififo.hpp"
#include "multififo/stick_random.hpp"

#include <cstddef>
#include <iostream>
#include <optional>
#include <ostream>
#include <utility>

namespace wrapper::multififo {

#ifdef MULTIFIFO_POP_CANDIDATES
static constexpr int pop_candidates = MULTIFIFO_POP_CANDIDATES;
#else
static constexpr int pop_candidates = 2;
#endif

class MultiFifo {
   public:
    using value_type = base::value_type;
    static constexpr int value_bits = base::value_bits;

   private:
    using queue_type = ::multififo::MultiFifo<value_type, ::multififo::mode::StickRandom<pop_candidates>>;

    struct Settings {
        int queues_per_thread = 2;
        int stickiness = 16;
        int seed = 1;
        base::CapacitySettings capacity;

        void register_cmd_options(cxxopts::Options& cmd) {
            cmd.add_options()("c,queue-factor", "The number of queues per thread",
                              cxxopts::value<int>(queues_per_thread), "NUMBER");
            cmd.add_options()("k,stickiness", "Number of operations the selected queues are used for",
                              cxxopts::value<int>(stickiness), "NUMBER");
            cmd.add_options()("mf-seed", "Seed for the MultiFIFO", cxxopts::value<int>(seed), "NUMBER");
            capacity.register_cmd_options(cmd);
        }

        [[nodiscard]] bool validate() const {
            if (queues_per_thread < 1) {
                std::cerr << "Error: Queue factor must be at least 1\n";
                return false;
            }
            if (stickiness < 1) {
                std::cerr << "Error: Stickiness must be positive\n";
                return false;
            }
            return capacity.validate();
        }

        void write_human_readable(std::ostream& out) const {
            out << "Queue factor: " << queues_per_thread << '\n';
            out << "Stickiness: " << stickiness << '\n';
            out << "MultiFIFO seed: " << seed << '\n';
            capacity.write_human_readable(out);
        }

        template <typename JsonObject>
        void write_json(JsonObject& obj) const {
            obj.entry("queue_factor", queues_per_thread);
            obj.entry("stickiness", stickiness);
            obj.entry("seed", seed);
            obj.entry("pop_candidates", pop_candidates);
            capacity.write_json(obj);
        }
    };

    class Handle {
        friend MultiFifo;
        typename queue_type::handle handle_;

        explicit Handle(typename queue_type::handle handle) : handle_{std::move(handle)} {
        }

       public:
        using value_type = MultiFifo::value_type;

        bool push(value_type value) {
            return handle_.push(value);
        }

        std::optional<value_type> try_pop() {
            return handle_.pop();
        }
    };

    queue_type queue_;

   public:
    using settings_type = Settings;
    using handle_type = Handle;

    // Each of the `num_threads * queues_per_thread` queues gets an equal share of the capacity, rounded up to a power
    // of two
    MultiFifo(int num_threads, std::size_t capacity, Settings const& settings)
        : queue_{num_threads, settings.capacity.scaled(capacity), settings.queues_per_thread, settings.stickiness,
                 settings.seed} {
    }

    static void write_human_readable(std::ostream& out) {
        out << "MultiFIFO\n";
        out << "  Mode: stick_random\n";
        out << "  Pop candidates: " << pop_candidates << '\n';
    }

    Handle get_handle() {
        return Handle{queue_.get_handle()};
    }
};

}  // namespace wrapper::multififo
