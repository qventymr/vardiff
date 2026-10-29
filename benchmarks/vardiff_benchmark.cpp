// Copyright 2026 Dmitry
// SPDX-License-Identifier: Apache-2.0

#include <vardiff/vardiff.hpp>

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <system_error>

struct Object {
    int first = 0;
    std::vector<int> values = std::vector<int>(4096, 0);
};

template <typename Function>
void measure(std::string_view name, int iterations, Function function) {
    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        checksum += static_cast<std::uint64_t>(function(i));
    }
    const auto duration = std::chrono::steady_clock::now() - start;
    const double ns = std::chrono::duration<double, std::nano>(duration).count();
    std::cout << std::left << std::setw(26) << name << std::right << std::setw(12)
              << std::fixed << std::setprecision(1) << ns / iterations
              << " ns/op; checksum=" << checksum << '\n';
}

int main(int argc, char** argv) {
    int iterations = 10000;
    if (argc > 2) {
        std::cerr << "Usage: vardiff_benchmark [positive iteration count]\n";
        return 1;
    }
    if (argc == 2) {
        const std::string_view input = argv[1];
        const auto result = std::from_chars(input.data(), input.data() + input.size(), iterations);
        if (result.ec != std::errc{} || result.ptr != input.data() + input.size() || iterations <= 0) {
            std::cerr << "Expected a positive iteration count\n";
            return 1;
        }
    }

    Object object;
    auto watcher = vardiff::track(object, vardiff::watch_options{.history_limit = 8},
                                  vardiff::field("first", &Object::first),
                                  vardiff::field("values", &Object::values));
    measure("changed / unchanged", iterations, [&](int) { return watcher.changed(); });
    measure("poll / unchanged", iterations, [&](int) { return watcher.poll().size(); });
    measure("diff / unchanged", iterations, [&](int) { return watcher.diff().size(); });
    object.first = 1;
    measure("changed / first field", iterations, [&](int) { return watcher.changed(); });
    object.first = 0;
    object.values.back() = 1;
    measure("diff / last element", iterations, [&](int) { return watcher.diff().size(); });
    watcher.snapshot();
    measure("poll / changed", iterations, [&](int i) {
        object.first = i + 1;
        return watcher.poll().size();
    });
}
