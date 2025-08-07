//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "util/config/ConfigDefinition.hpp"
#include "util/log/Logger.hpp"
#include "util/prometheus/Prometheus.hpp"

#include <benchmark/benchmark.h>
#include <boost/log/core/core.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include <barrier>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace util;

struct BenchmarkLoggingInitializer {
    static constexpr auto kLOG_FORMAT = "%TimeStamp% (%SourceLocation%) [%ThreadID%] %Channel%:%Severity% %Message%";

    static void
    initFileLogging(LogService::FileLoggingParams const& params)
    {
        boost::log::add_common_attributes();
        std::filesystem::create_directories(params.logDir);
        LogService::initFileLogging(params, kLOG_FORMAT);
    }
};

namespace {

std::string
uniqueLogDir()
{
    auto const epochTime = std::chrono::high_resolution_clock::now().time_since_epoch();
    return "/tmp/clio_benchmark_logs_" +
        std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(epochTime).count());
}

}  // anonymous namespace

static void
benchmarkConcurrentFileLogging(benchmark::State& state)
{
    auto const numThreads = state.range(0);
    auto const messagesPerThread = state.range(1);

    PrometheusService::init(config::getClioConfig());

    auto const logDir = uniqueLogDir();
    for (auto _ : state) {
        state.PauseTiming();
        std::filesystem::remove_all(logDir);

        BenchmarkLoggingInitializer::initFileLogging(
            {.logDir = logDir, .rotationSizeMB = 1, .dirMaxSizeMB = 10, .rotationHours = 24}
        );
        state.ResumeTiming();

        std::vector<std::thread> threads;
        threads.reserve(numThreads);

        std::chrono::high_resolution_clock::time_point start;
        std::barrier barrier(numThreads, [&start]() { start = std::chrono::high_resolution_clock::now(); });

        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([t, messagesPerThread, &barrier]() {
                barrier.arrive_and_wait();

                Logger threadLogger("Thread_" + std::to_string(t));

                for (int i = 0; i < messagesPerThread; ++i) {
                    LOG(threadLogger.info()) << "Test log message #" << i;
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
        boost::log::core::get()->flush();

        auto end = std::chrono::high_resolution_clock::now();

        state.SetIterationTime(std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count());
    }

    std::filesystem::remove_all(logDir);

    auto const totalMessages = numThreads * messagesPerThread;
    state.counters["TotalMessagesRate"] = benchmark::Counter(totalMessages, benchmark::Counter::kIsRate);
    state.counters["Threads"] = numThreads;
    state.counters["MessagesPerThread"] = messagesPerThread;
}

BENCHMARK(benchmarkConcurrentFileLogging)
    ->ArgsProduct({
        {1, 2, 4, 8},               // Number of threads
        {500, 1000, 2000, 100000},  // Messages per thread
    })
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
