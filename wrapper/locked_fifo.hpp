#pragma once

#include "util/base.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <ostream>

namespace wrapper::locked_fifo {

class LockedFifo {
   public:
    using value_type = base::value_type;
    static constexpr int value_bits = base::value_bits;

   private:
    std::deque<value_type> queue_;
    std::mutex mutex_;

   public:
    using settings_type = base::EmptySettings;
    using handle_type = base::SelfHandle<LockedFifo>;

    LockedFifo(int /*num_threads*/, std::size_t /*capacity*/, settings_type const& /*settings*/) {
    }

    bool push(value_type value) {
        std::scoped_lock lock{mutex_};
        queue_.push_back(value);
        return true;
    }

    std::optional<value_type> try_pop() {
        std::scoped_lock lock{mutex_};
        if (queue_.empty()) {
            return std::nullopt;
        }
        auto value = queue_.front();
        queue_.pop_front();
        return value;
    }

    static void write_human_readable(std::ostream& out) {
        out << "Locked std::deque\n";
    }

    handle_type get_handle() {
        return handle_type{*this};
    }
};

}  // namespace wrapper::locked_fifo
