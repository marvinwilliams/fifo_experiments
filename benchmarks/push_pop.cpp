#include "util/benchmark.hpp"
#include "util/memory_stats.hpp"
#include "util/thread_coordination.hpp"
#include "wrapper/util/selector.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

using fifo_type = FIFO;
using handle_type = fifo_type::handle_type;
using value_type = fifo_type::value_type;

using clock_type = benchmark::clock_type;

struct Settings {
    benchmark::BaseSettings base_settings{};
    fifo_type::settings_type fifo_settings;
    long long prefill_per_thread = 1 << 20;
    long long elements_per_thread = 1 << 14;
    long long batch_size = 1 << 12;
    benchmark::Papi papi;

    void register_cmd_options(cxxopts::Options& cmd) {
        base_settings.register_cmd_options(cmd);
        fifo_settings.register_cmd_options(cmd);
        papi.register_cmd_options(cmd);
        cmd.add_options()
            // clang-format off
            ("p,prefill", "Prefill per thread", cxxopts::value<long long>(prefill_per_thread), "NUMBER")
            ("n,elements", "Number of elements per thread", cxxopts::value<long long>(elements_per_thread), "NUMBER")
            ("batch-size", "Batch size", cxxopts::value<long long>(batch_size), "NUMBER")
            // clang-format on
            ;
    }

    bool validate() const {
        if (!base_settings.validate()) {
            return false;
        }
        if (!fifo_settings.validate()) {
            return false;
        }
        if (prefill_per_thread < 0) {
            std::cerr << "Error: Prefill must be nonnegative\n";
            return false;
        }
        if (elements_per_thread < 0) {
            std::cerr << "Error: Number of elements must be nonnegative\n";
            return false;
        }
        if (batch_size <= 0) {
            std::cerr << "Error: batch size must be greater than 0\n";
            return false;
        }
        if (!papi.validate()) {
            return false;
        }
        return true;
    }

    void write_human_readable(std::ostream& out) const {
        base_settings.write_human_readable(out);
        fifo_settings.write_human_readable(out);
        out << "Prefill per thread: " << prefill_per_thread << '\n';
        out << "Elements per thread: " << elements_per_thread << '\n';
        out << "Batch size: " << batch_size << '\n';
        papi.write_human_readable(out);
    }

    void write_json(json::Object& obj) const {
        base_settings.write_json(obj);
        obj.object("fifo", [this](json::Object& o) { fifo_settings.write_json(o); });
        obj.entry("prefill_per_thread", prefill_per_thread);
        obj.entry("elements_per_thread", elements_per_thread);
        obj.entry("batch_size", batch_size);
        papi.write_json(obj);
    }
};

struct ThreadData {
    benchmark::Interval push_interval{};
    benchmark::Interval pop_interval{};
    long long push_count{0};
    long long failed_push_count{0};
    long long pop_count{0};
    long long failed_pop_count{0};
    int event_set = -1;
    std::vector<long long> push_papi_event_counter{};
    std::vector<long long> pop_papi_event_counter{};

    void write_json(json::Object& obj, clock_type::time_point push_origin, clock_type::time_point pop_origin,
                    benchmark::Papi const& papi) const {
        benchmark::write_interval(obj, "push_", push_interval, push_origin);
        benchmark::write_interval(obj, "pop_", pop_interval, pop_origin);
        obj.entry("pushes", push_count);
        obj.entry("failed_pushes", failed_push_count);
        obj.entry("pops", pop_count);
        obj.entry("failed_pops", failed_pop_count);
        papi.write_counters(obj, "push_papi", push_papi_event_counter);
        papi.write_counters(obj, "pop_papi", pop_papi_event_counter);
    }
};

struct SharedData {
    std::atomic_llong counter{0};
    std::atomic_llong pushed{0};
    std::atomic_llong completed{0};
    memory_stats::Snapshot memory_start;
    memory_stats::Snapshot memory_after_push;
    memory_stats::Snapshot memory_end;
    std::vector<ThreadData> thread_data;
};

std::vector<benchmark::Interval> intervals(SharedData const& data, benchmark::Interval ThreadData::* member) {
    std::vector<benchmark::Interval> result;
    for (auto const& t : data.thread_data) {
        result.push_back(t.*member);
    }
    return result;
}

void write_result_json(Settings const& settings, SharedData const& data, std::ostream& out) {
    {
        json::Object root{out};
        root.object("settings", [&settings](json::Object& obj) { settings.write_json(obj); });
        root.object("results", [&](json::Object& results) {
            auto push_intervals = intervals(data, &ThreadData::push_interval);
            auto pop_intervals = intervals(data, &ThreadData::pop_interval);
            long long pushes = 0;
            long long failed_pushes = 0;
            long long pops = 0;
            long long failed_pops = 0;
            std::vector<long long> push_papi;
            std::vector<long long> pop_papi;
            for (auto const& t : data.thread_data) {
                pushes += t.push_count;
                failed_pushes += t.failed_push_count;
                pops += t.pop_count;
                failed_pops += t.failed_pop_count;
                settings.papi.accumulate(push_papi, t.push_papi_event_counter);
                settings.papi.accumulate(pop_papi, t.pop_papi_event_counter);
            }
            results.entry("push_time_ns", benchmark::time_ns(push_intervals));
            results.entry("pop_time_ns", benchmark::time_ns(pop_intervals));
            results.entry("pushes", pushes);
            results.entry("failed_pushes", failed_pushes);
            results.entry("pops", pops);
            results.entry("failed_pops", failed_pops);
            settings.papi.write_counters(results, "push_papi", push_papi);
            settings.papi.write_counters(results, "pop_papi", pop_papi);
            results.object("memory", [&data](json::Object& memory) {
                memory_stats::write_json(
                    memory,
                    {{"start", data.memory_start}, {"after_push", data.memory_after_push}, {"end", data.memory_end}});
            });
            auto push_origin = benchmark::span(push_intervals).start;
            auto pop_origin = benchmark::span(pop_intervals).start;
            results.array("thread_data", data.thread_data.begin(), data.thread_data.end(),
                          [&](std::ostream& out2, ThreadData const& thread_data) {
                              json::Object obj{out2};
                              thread_data.write_json(obj, push_origin, pop_origin, settings.papi);
                          });
        });
    }
    out << '\n';
}

class Context : public thread_coordination::Context {
    handle_type handle_;
    ThreadData thread_data_;
    SharedData* shared_data_;
    Settings const* settings_;

   public:
    explicit Context(thread_coordination::Context ctx, handle_type handle, SharedData& shared_data,
                     Settings const& settings)
        : thread_coordination::Context{std::move(ctx)},
          handle_{std::move(handle)},
          shared_data_{&shared_data},
          settings_{&settings} {
    }

    bool push(value_type value) {
        return handle_.push(value);
    }

    auto try_pop() {
        return handle_.try_pop();
    }

    ThreadData& thread_data() noexcept {
        return thread_data_;
    }
    [[nodiscard]] ThreadData const& thread_data() const noexcept {
        return thread_data_;
    }
    SharedData& shared_data() noexcept {
        return *shared_data_;
    }
    [[nodiscard]] SharedData const& shared_data() const noexcept {
        return *shared_data_;
    }

    [[nodiscard]] Settings const& settings() const noexcept {
        return *settings_;
    }
};

[[gnu::noinline]] void push(Context& context) {
    auto offset =
        static_cast<value_type>(context.settings().base_settings.num_threads * context.settings().prefill_per_thread) +
        1;
    long long max = context.settings().elements_per_thread * context.settings().base_settings.num_threads;
    if (context.id() == 0) {
        context.shared_data().memory_start = memory_stats::Snapshot::take();
    }
    context.synchronize();
    context.settings().papi.start(context.thread_data().event_set);
    context.spin_synchronize();
    context.thread_data().push_interval.start = clock_type::now();
    while (true) {
        auto start = context.shared_data().counter.fetch_add(context.settings().batch_size, std::memory_order_relaxed);
        if (start >= max) {
            break;
        }
        auto end = std::min(start + context.settings().batch_size, max);
        long long pushed = 0;
        for (auto i = start; i < end; ++i) {
            if (context.push(offset + static_cast<value_type>(i))) {
                ++pushed;
            } else {
                ++context.thread_data().failed_push_count;
            }
        }
        context.thread_data().push_count += pushed;
        context.shared_data().pushed.fetch_add(pushed, std::memory_order_relaxed);
    }
    context.thread_data().push_interval.end = clock_type::now();
    context.settings().papi.stop(context.thread_data().event_set, context.thread_data().push_papi_event_counter);
    context.synchronize();
    if (context.id() == 0) {
        context.shared_data().memory_after_push = memory_stats::Snapshot::take();
    }
}

[[gnu::noinline]] void pop(Context& context) {
    if (context.id() == 0) {
        context.shared_data().counter.store(0, std::memory_order_relaxed);
        context.shared_data().completed.store(0, std::memory_order_relaxed);
    }
    context.synchronize();
    auto max = context.shared_data().pushed.load(std::memory_order_relaxed);
    context.settings().papi.start(context.thread_data().event_set);
    context.spin_synchronize();
    auto& reserved = context.shared_data().counter;
    auto& completed = context.shared_data().completed;
    auto reserve = [&] {
        auto current = reserved.load(std::memory_order_relaxed);
        while (current < max) {
            auto count = std::min(context.settings().batch_size, max - current);
            if (reserved.compare_exchange_weak(current, current + count, std::memory_order_relaxed)) {
                return count;
            }
        }
        return 0LL;
    };
    context.thread_data().pop_interval.start = clock_type::now();
    while (completed.load(std::memory_order_relaxed) < max) {
        auto count = reserve();
        if (count == 0) {
            continue;
        }
        long long popped{0};
        while (popped < count && context.try_pop()) {
            ++popped;
        }
        if (popped < count) {
            ++context.thread_data().failed_pop_count;
            reserved.fetch_sub(count - popped, std::memory_order_relaxed);
        }
        completed.fetch_add(popped, std::memory_order_relaxed);
        context.thread_data().pop_count += popped;
    }
    context.thread_data().pop_interval.end = clock_type::now();
    context.settings().papi.stop(context.thread_data().event_set, context.thread_data().pop_papi_event_counter);
    context.synchronize();
    if (context.id() == 0) {
        context.shared_data().memory_end = memory_stats::Snapshot::take();
    }
}

void benchmark_thread(Context context) {
    context.thread_data().event_set = context.settings().papi.create_event_set();
    if (context.id() == 0) {
        std::clog << "Prefilling...\n";
    }
    context.synchronize();
    auto first = static_cast<value_type>(context.id() * context.settings().prefill_per_thread) + 1;
    for (auto i = 0LL; i < context.settings().prefill_per_thread; ++i) {
        if (!context.push(first + static_cast<value_type>(i))) {
            context.write(std::cerr) << "Error: Prefill failed, the FIFO is too small\n";
            std::exit(EXIT_FAILURE);
        }
    }
    context.synchronize();
    if (context.id() == 0) {
        std::clog << "Pushing...\n";
    }
    push(context);
    if (context.id() == 0) {
        std::clog << "Popping...\n";
    }
    pop(context);
    context.shared_data().thread_data[static_cast<std::size_t>(context.id())] = std::move(context.thread_data());
}

void run_benchmark(Settings const& settings) {
    auto num_threads = settings.base_settings.num_threads;
    SharedData shared_data;
    shared_data.thread_data.resize(static_cast<std::size_t>(num_threads));

    auto fifo =
        fifo_type(num_threads,
                  static_cast<std::size_t>(num_threads * (settings.elements_per_thread + settings.prefill_per_thread)),
                  settings.fifo_settings);

    thread_coordination::dispatch(settings.base_settings.affinity, num_threads, [&](auto ctx) {
        benchmark_thread(Context(std::move(ctx), fifo.get_handle(), shared_data, settings));
    });

    std::clog << "Done\n";
    std::clog << '\n';
    std::clog << "= Results =\n";
    std::clog << "Push time: " << std::fixed << std::setprecision(3)
              << benchmark::seconds(intervals(shared_data, &ThreadData::push_interval)) << " s\n";
    std::clog << "Pop time: " << std::fixed << std::setprecision(3)
              << benchmark::seconds(intervals(shared_data, &ThreadData::pop_interval)) << " s\n";
    write_result_json(settings, shared_data, std::cout);
}

int main(int argc, char* argv[]) {
    return benchmark::run<fifo_type, Settings>(argc, argv, run_benchmark);
}
