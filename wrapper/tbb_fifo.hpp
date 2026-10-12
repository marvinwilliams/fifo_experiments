#pragma once

#include "util/base.hpp"

#include <tbb/concurrent_queue.h>

#include <cstddef>
#include <optional>
#include <ostream>

namespace wrapper::tbb_fifo {

class TBBFifo {
   public:
    using value_type = base::value_type;
    static constexpr int value_bits = base::value_bits;

   private:
    tbb::concurrent_queue<value_type> queue_;

   public:
    using settings_type = base::EmptySettings;
    using handle_type = base::SelfHandle<TBBFifo>;

    TBBFifo(int /*num_threads*/, std::size_t /*capacity*/, settings_type const& /*settings*/) {
    }

    bool push(value_type value) {
        queue_.push(value);
        return true;
    }

    std::optional<value_type> try_pop() {
        value_type value{};
        if (!queue_.try_pop(value)) {
            return std::nullopt;
        }
        return value;
    }

    static void write_human_readable(std::ostream& out) {
        out << "TBB concurrent_queue\n";
    }

    handle_type get_handle() {
        return handle_type{*this};
    }
};

}  // namespace wrapper::tbb_fifo
