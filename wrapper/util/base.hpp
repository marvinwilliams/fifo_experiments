#pragma once

#include <cxxopts.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ostream>

namespace wrapper::base {

using value_type = std::uint64_t;
inline constexpr int value_bits = 64;

template <typename Fifo>
class SelfHandle {
    friend Fifo;
    Fifo* fifo_;

    explicit SelfHandle(Fifo& fifo) : fifo_{&fifo} {
    }

   public:
    using value_type = typename Fifo::value_type;

    bool push(value_type value) {
        return fifo_->push(value);
    }
    std::optional<value_type> try_pop() {
        return fifo_->try_pop();
    }
};

struct EmptySettings {
    void register_cmd_options(cxxopts::Options& /*unused*/) {
    }

    [[nodiscard]] static bool validate() {
        return true;
    }

    static void write_human_readable(std::ostream& /*unused*/) {
    }

    template <typename JsonObject>
    static void write_json(JsonObject& /*unused*/) {
    }
};

struct CapacitySettings {
    double capacity_factor = 2.0;

    [[nodiscard]] std::size_t scaled(std::size_t capacity) const {
        return static_cast<std::size_t>(std::ceil(static_cast<double>(capacity) * capacity_factor));
    }

    void register_cmd_options(cxxopts::Options& cmd) {
        cmd.add_options()("capacity-factor", "Allocate this factor times the required capacity",
                          cxxopts::value<double>(capacity_factor), "NUMBER");
    }

    [[nodiscard]] bool validate() const {
        if (!(capacity_factor >= 1.0)) {
            std::cerr << "Error: Capacity factor must be at least 1\n";
            return false;
        }
        return true;
    }

    void write_human_readable(std::ostream& out) const {
        out << "Capacity factor: " << capacity_factor << '\n';
    }

    template <typename JsonObject>
    void write_json(JsonObject& obj) const {
        obj.entry("capacity_factor", capacity_factor);
    }
};

}  // namespace wrapper::base
