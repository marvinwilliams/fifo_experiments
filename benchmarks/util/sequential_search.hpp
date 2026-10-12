#pragma once

#include "util/graph.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <vector>

namespace sequential_search {

inline constexpr long long unreachable = std::numeric_limits<long long>::max();

struct Result {
    std::vector<long long> distances;
    long long processed_nodes{0};
    long long ignored_nodes{0};
    std::size_t sum_sizes{0};
    std::size_t max_size{0};
};

// Breadth-first search, every edge has weight 1
inline Result bfs(Graph const& graph, std::size_t source) {
    Result result;
    result.distances.assign(graph.num_nodes(), unreachable);
    std::queue<std::size_t> queue;
    result.distances[source] = 0;
    queue.push(source);
    while (!queue.empty()) {
        result.sum_sizes += queue.size();
        result.max_size = std::max(result.max_size, queue.size());
        auto node = queue.front();
        queue.pop();
        auto d = result.distances[node] + 1;
        for (auto i = graph.nodes[node]; i < graph.nodes[node + 1]; ++i) {
            auto target = graph.edges[i].target;
            if (result.distances[target] == unreachable) {
                result.distances[target] = d;
                queue.push(target);
            }
        }
        ++result.processed_nodes;
    }
    return result;
}

namespace detail {
struct Node {
    long long distance;
    std::size_t id;

    friend bool operator>(Node const& lhs, Node const& rhs) noexcept {
        return lhs.distance > rhs.distance;
    }
};
}  // namespace detail

// Dijkstra's algorithm using the edge weights
inline Result dijkstra(Graph const& graph, std::size_t source) {
    using detail::Node;
    Result result;
    result.distances.assign(graph.num_nodes(), unreachable);
    // A binary min-heap, like std::priority_queue but with reserved capacity
    std::vector<Node> heap;
    heap.reserve(graph.num_nodes());
    auto const comp = std::greater<>{};
    result.distances[source] = 0;
    heap.push_back({0, source});
    while (!heap.empty()) {
        result.sum_sizes += heap.size();
        result.max_size = std::max(result.max_size, heap.size());
        std::pop_heap(heap.begin(), heap.end(), comp);
        auto node = heap.back();
        heap.pop_back();
        // Ignore stale nodes
        if (node.distance > result.distances[node.id]) {
            ++result.ignored_nodes;
            continue;
        }
        for (auto i = graph.nodes[node.id]; i < graph.nodes[node.id + 1]; ++i) {
            auto d = node.distance + graph.edges[i].weight;
            if (d < result.distances[graph.edges[i].target]) {
                result.distances[graph.edges[i].target] = d;
                heap.push_back({d, graph.edges[i].target});
                std::push_heap(heap.begin(), heap.end(), comp);
            }
        }
        ++result.processed_nodes;
    }
    return result;
}

inline Result run(Graph const& graph, std::size_t source, bool weighted) {
    return weighted ? dijkstra(graph, source) : bfs(graph, source);
}

template <typename Distances, typename Get>
std::size_t furthest_node(Distances const& distances, Get get) {
    std::size_t furthest = 0;
    long long longest = -1;
    for (std::size_t i = 0; i < distances.size(); ++i) {
        auto d = get(distances[i]);
        if (d != unreachable && d > longest) {
            longest = d;
            furthest = i;
        }
    }
    return furthest;
}

}  // namespace sequential_search
