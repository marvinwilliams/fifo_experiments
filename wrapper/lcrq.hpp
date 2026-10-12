#pragma once

#include "util/base.hpp"

#include "LCRQ/LCRQueue.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>

namespace wrapper::lcrq {

class LCRQ {
   public:
    using value_type = base::value_type;
    static constexpr int value_bits = base::value_bits;

   private:
    using queue_type = LCRQueue<void>;

    class Handle {
        friend LCRQ;
        queue_type* queue_;
        int tid_;

        Handle(queue_type& queue, int tid) : queue_{&queue}, tid_{tid} {
        }

       public:
        using value_type = LCRQ::value_type;

        bool push(value_type value) {
            queue_->enqueue(reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)), tid_);
            return true;
        }

        std::optional<value_type> try_pop() {
            void* ptr = queue_->dequeue(tid_);
            if (ptr == nullptr) {
                return std::nullopt;
            }
            return value_type{reinterpret_cast<std::uintptr_t>(ptr)};
        }
    };

    int num_threads_;
    queue_type queue_;
    std::atomic_int next_tid_{0};

   public:
    using settings_type = base::EmptySettings;
    using handle_type = Handle;

    LCRQ(int num_threads, std::size_t /*capacity*/, settings_type const& /*settings*/)
        : num_threads_{num_threads}, queue_{num_threads} {
    }

    static void write_human_readable(std::ostream& out) {
        out << "LCRQ\n";
    }

    Handle get_handle() {
        auto tid = next_tid_.fetch_add(1, std::memory_order_relaxed);
        assert(tid < num_threads_);
        return Handle{queue_, tid};
    }
};

}  // namespace wrapper::lcrq
