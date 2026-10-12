#pragma once

#include "util/base.hpp"

#include "scal/boundedsize_kfifo.h"

#include <cxxopts.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ostream>

namespace wrapper::kfifo {

class KFifo {
   public:
    using value_type = base::value_type;
    // scal::TaggedValue keeps a 16 bit ABA tag in the upper bits and sign-extends bit 47
    static constexpr int value_bits = 47;

   private:
    using queue_type = scal::BoundedSizeKFifo<value_type>;

    struct Settings {
        double k_per_thread = 1.0;
        base::CapacitySettings capacity;

        void register_cmd_options(cxxopts::Options& cmd) {
            cmd.add_options()("k,k-per-thread", "Segment size k per thread (rounded up to a power of two)",
                              cxxopts::value<double>(k_per_thread), "NUMBER");
            capacity.register_cmd_options(cmd);
        }

        [[nodiscard]] bool validate() const {
            if (!(k_per_thread > 0.0)) {
                std::cerr << "Error: k per thread must be positive\n";
                return false;
            }
            return capacity.validate();
        }

        void write_human_readable(std::ostream& out) const {
            out << "k per thread: " << k_per_thread << '\n';
            capacity.write_human_readable(out);
        }

        template <typename JsonObject>
        void write_json(JsonObject& obj) const {
            obj.entry("k_per_thread", k_per_thread);
            capacity.write_json(obj);
        }
    };

    static std::uint64_t segment_size(int num_threads, Settings const& settings) {
        auto k = std::lround(static_cast<double>(num_threads) * settings.k_per_thread);
        return std::bit_ceil(static_cast<std::uint64_t>(std::max(1L, k)));
    }

    static std::uint64_t num_segments(int num_threads, std::size_t capacity, Settings const& settings) {
        auto k = segment_size(num_threads, settings);
        return std::max<std::uint64_t>(4, (settings.capacity.scaled(capacity) + k - 1) / k);
    }

    queue_type queue_;

   public:
    using settings_type = Settings;
    using handle_type = base::SelfHandle<KFifo>;

    KFifo(int num_threads, std::size_t capacity, Settings const& settings)
        : queue_{segment_size(num_threads, settings), num_segments(num_threads, capacity, settings)} {
    }

    bool push(value_type value) {
        return queue_.enqueue(value);
    }

    std::optional<value_type> try_pop() {
        value_type value{};
        if (!queue_.dequeue(&value)) {
            return std::nullopt;
        }
        return value;
    }

    static void write_human_readable(std::ostream& out) {
        out << "Bounded-size k-FIFO\n";
    }

    handle_type get_handle() {
        return handle_type{*this};
    }
};

}  // namespace wrapper::kfifo
