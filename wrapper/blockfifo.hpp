#pragma once

#include "blockfifo/block_based_queue.hpp"
#include "util/base.hpp"

#include <cxxopts.hpp>

#include <cassert>
#include <cstddef>
#include <optional>

#include <iostream>
#include <ostream>

namespace wrapper::blockfifo {

class BlockFifo {
   public:
    using value_type = base::value_type;
    static constexpr int value_bits = base::value_bits;

   private:
    using queue_type = ::blockfifo::block_based_queue<value_type>;

    struct Settings {
        double blocks_per_window_per_thread = 1.0;
        std::size_t cells_per_block = 7;
        base::CapacitySettings capacity;

        void register_cmd_options(cxxopts::Options& cmd) {
            cmd.add_options()("b,blocks-per-thread", "Blocks per window per thread",
                              cxxopts::value<double>(blocks_per_window_per_thread), "NUMBER");
            cmd.add_options()("c,cells-per-block", "Cells per block", cxxopts::value<std::size_t>(cells_per_block),
                              "NUMBER");
            capacity.register_cmd_options(cmd);
        }

        [[nodiscard]] bool validate() const {
            if (!(blocks_per_window_per_thread > 0.0)) {
                std::cerr << "Error: Blocks per window per thread must be positive\n";
                return false;
            }
            if (cells_per_block == 0 || cells_per_block >= (std::size_t{1} << 16)) {
                std::cerr << "Error: Cells per block must be in [1, 65535]\n";
                return false;
            }
            return capacity.validate();
        }

        void write_human_readable(std::ostream& out) const {
            out << "Blocks per window per thread: " << blocks_per_window_per_thread << '\n';
            out << "Cells per block: " << cells_per_block << '\n';
            capacity.write_human_readable(out);
        }

        template <typename JsonObject>
        void write_json(JsonObject& obj) const {
            obj.entry("blocks_per_window_per_thread", blocks_per_window_per_thread);
            obj.entry("cells_per_block", cells_per_block);
            capacity.write_json(obj);
        }
    };

    class Handle {
        friend BlockFifo;
        queue_type::handle handle_;

        explicit Handle(queue_type::handle handle) : handle_{std::move(handle)} {
        }

       public:
        using value_type = BlockFifo::value_type;

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

    BlockFifo(int num_threads, std::size_t capacity, Settings const& settings)
        : queue_{num_threads, settings.capacity.scaled(capacity), settings.blocks_per_window_per_thread,
                 settings.cells_per_block} {
    }

    static void write_human_readable(std::ostream& out) {
        out << "BlockFIFO\n";
    }

    Handle get_handle() {
        return Handle{queue_.get_handle()};
    }
};

}  // namespace wrapper::blockfifo
