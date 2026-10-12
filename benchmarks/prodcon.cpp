#include "util/benchmark.hpp"
#include "util/memory_stats.hpp"
#include "util/thread_coordination.hpp"
#include "wrapper/util/selector.hpp"

#include <cxxopts.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

using fifo_type = FIFO;
using handle_type = fifo_type::handle_type;
using value_type = fifo_type::value_type;

using clock_type = benchmark::clock_type;

struct Settings {
    benchmark::BaseSettings base_settings{};
    fifo_type::settings_type fifo_settings;
    long long prefill_per_thread = 1 << 15;
    long long capacity_per_thread = 1 << 16;
    int producers = -1;
    double time_s = 5.0;
    benchmark::Papi papi;

    [[nodiscard]] int num_producers() const noexcept {
        return producers < 0 ? base_settings.num_threads / 2 : producers;
    }

    void register_cmd_options(cxxopts::Options& cmd) {
        base_settings.register_cmd_options(cmd);
        fifo_settings.register_cmd_options(cmd);
        papi.register_cmd_options(cmd);
        cmd.add_options()
            // clang-format off
            ("p,prefill", "Prefill per thread", cxxopts::value<long long>(prefill_per_thread), "NUMBER")
            ("capacity", "Capacity of the FIFO per thread (only bounded FIFOs)", cxxopts::value<long long>(capacity_per_thread), "NUMBER")
            ("producers", "Number of producer threads (default: half of the threads)", cxxopts::value<int>(producers), "NUMBER")
            ("t,time", "Duration in seconds", cxxopts::value<double>(time_s), "NUMBER")
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
        if (capacity_per_thread < prefill_per_thread) {
            std::cerr << "Error: Capacity must be at least the prefill\n";
            return false;
        }
        if (num_producers() < 1 || num_producers() >= base_settings.num_threads) {
            std::cerr << "Error: There must be at least one producer and one consumer\n";
            return false;
        }
        if (!(time_s > 0.0)) {
            std::cerr << "Error: Time must be positive\n";
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
        out << "Capacity per thread: " << capacity_per_thread << '\n';
        out << "Producers: " << num_producers() << '\n';
        out << "Consumers: " << base_settings.num_threads - num_producers() << '\n';
        out << "Time: " << time_s << " s\n";
        papi.write_human_readable(out);
    }

    void write_json(json::Object& obj) const {
        base_settings.write_json(obj);
        obj.object("fifo", [this](json::Object& o) { fifo_settings.write_json(o); });
        obj.entry("prefill_per_thread", prefill_per_thread);
        obj.entry("capacity_per_thread", capacity_per_thread);
        obj.entry("producers", num_producers());
        obj.entry("consumers", base_settings.num_threads - num_producers());
        obj.entry("time_s", time_s);
        papi.write_json(obj);
    }
};

struct ThreadData {
    benchmark::Interval interval{};
    bool producer = false;
    long long op_count = 0;
    long long failed_op_count = 0;
    std::vector<long long> papi_event_counter{};

    void write_json(json::Object& obj, clock_type::time_point origin, benchmark::Papi const& papi) const {
        benchmark::write_interval(obj, "", interval, origin);
        obj.entry("role", producer ? "producer" : "consumer");
        obj.entry("operations", op_count);
        obj.entry("failed_operations", failed_op_count);
        papi.write_counters(obj, "papi", papi_event_counter);
    }
};

struct SharedData {
    memory_stats::Snapshot memory_start;
    memory_stats::Snapshot memory_end;
    std::vector<ThreadData> thread_data;
};

std::vector<benchmark::Interval> intervals(SharedData const& data) {
    std::vector<benchmark::Interval> result;
    for (auto const& t : data.thread_data) {
        result.push_back(t.interval);
    }
    return result;
}

void write_result_json(Settings const& settings, SharedData const& data, std::ostream& out) {
    {
        json::Object root{out};
        root.object("settings", [&settings](json::Object& obj) { settings.write_json(obj); });
        root.object("results", [&](json::Object& results) {
            auto thread_intervals = intervals(data);
            long long pushes = 0;
            long long failed_pushes = 0;
            long long pops = 0;
            long long failed_pops = 0;
            std::vector<long long> papi_counters;
            for (auto const& t : data.thread_data) {
                (t.producer ? pushes : pops) += t.op_count;
                (t.producer ? failed_pushes : failed_pops) += t.failed_op_count;
                settings.papi.accumulate(papi_counters, t.papi_event_counter);
            }
            results.entry("time_ns", benchmark::time_ns(thread_intervals));
            results.entry("pushes", pushes);
            results.entry("failed_pushes", failed_pushes);
            results.entry("pops", pops);
            results.entry("failed_pops", failed_pops);
            settings.papi.write_counters(results, "papi", papi_counters);
            results.object("memory", [&data](json::Object& memory) {
                memory_stats::write_json(memory, {{"start", data.memory_start}, {"end", data.memory_end}});
            });
            auto origin = benchmark::span(thread_intervals).start;
            results.array("thread_data", data.thread_data.begin(), data.thread_data.end(),
                          [&](std::ostream& out2, ThreadData const& thread_data) {
                              json::Object obj{out2};
                              thread_data.write_json(obj, origin, settings.papi);
                          });
        });
    }
    out << '\n';
}

static constexpr long long chunk_size = 4096;

template <typename Op>
void run_until(clock_type::time_point deadline, ThreadData& data, Op op) {
    while (true) {
        for (long long i = 0; i < chunk_size; ++i) {
            if (op()) {
                ++data.op_count;
            } else {
                ++data.failed_op_count;
            }
        }
        if (clock_type::now() >= deadline) {
            return;
        }
    }
}

void benchmark_thread(thread_coordination::Context ctx, fifo_type& fifo, SharedData& shared_data,
                      Settings const& settings) {
    ThreadData data;
    data.producer = ctx.id() < settings.num_producers();
    auto handle = fifo.get_handle();
    auto event_set = settings.papi.create_event_set();
    if (ctx.id() == 0) {
        std::clog << "Prefilling...\n";
    }
    ctx.synchronize();
    auto value = static_cast<value_type>(ctx.id()) << 32;
    for (auto i = 0LL; i < settings.prefill_per_thread; ++i) {
        if (!handle.push(++value)) {
            ctx.write(std::cerr) << "Error: Prefill failed, the FIFO is too small\n";
            std::exit(EXIT_FAILURE);
        }
    }
    ctx.synchronize();
    if (ctx.id() == 0) {
        std::clog << "Working...\n";
        shared_data.memory_start = memory_stats::Snapshot::take();
    }
    ctx.synchronize();
    settings.papi.start(event_set);
    ctx.spin_synchronize();
    data.interval.start = clock_type::now();
    auto deadline =
        data.interval.start + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>{settings.time_s});
    if (data.producer) {
        run_until(deadline, data, [&] { return handle.push(++value); });
    } else {
        run_until(deadline, data, [&] { return handle.try_pop().has_value(); });
    }
    data.interval.end = clock_type::now();
    settings.papi.stop(event_set, data.papi_event_counter);
    ctx.synchronize();
    if (ctx.id() == 0) {
        shared_data.memory_end = memory_stats::Snapshot::take();
    }
    shared_data.thread_data[static_cast<std::size_t>(ctx.id())] = std::move(data);
}

void run_benchmark(Settings const& settings) {
    SharedData shared_data;
    auto num_threads = settings.base_settings.num_threads;
    shared_data.thread_data.resize(static_cast<std::size_t>(num_threads));
    auto fifo = fifo_type(num_threads, static_cast<std::size_t>(settings.capacity_per_thread * num_threads),
                          settings.fifo_settings);

    thread_coordination::dispatch(settings.base_settings.affinity, num_threads,
                                  [&](auto ctx) { benchmark_thread(std::move(ctx), fifo, shared_data, settings); });

    std::clog << "Done\n";
    std::clog << '\n';
    std::clog << "= Results =\n";
    std::clog << "Time (s): " << std::fixed << std::setprecision(3) << benchmark::seconds(intervals(shared_data))
              << '\n';
    write_result_json(settings, shared_data, std::cout);
}

int main(int argc, char* argv[]) {
    return benchmark::run<fifo_type, Settings>(argc, argv, run_benchmark);
}
