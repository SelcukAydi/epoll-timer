#include <benchmark/benchmark.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/test/Util.h>
#include <folly/io/async/test/UndelayedDestruction.h>

extern void MultiTaskScheduleBenchmarkEntry(benchmark::State& state);
extern void MultiTaskScheduleBenchmarkFollyEntry(benchmark::State& state);

extern void FollyIntrusiveHeapTest(benchmark::State& state);
extern void BoostIntrusiveSetTest(benchmark::State& state);

BENCHMARK(MultiTaskScheduleBenchmarkFollyEntry)
    ->Iterations(5)
    ->MeasureProcessCPUTime()
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond)
    ->Args({1000000, 10})
    ->Args({100000, 10})
    ->Args({10000, 10})
    ->Args({1000, 10})
    ->Args({1000000, 0})
    ->Args({100000, 0})
    ->Args({10000, 0})
    ->Args({1000, 0});

BENCHMARK(MultiTaskScheduleBenchmarkEntry)
    ->Iterations(5)
    ->MeasureProcessCPUTime()
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond)
    ->Args({1000000, 10})
    ->Args({100000, 10})
    ->Args({10000, 10})
    ->Args({1000, 10})
    ->Args({1000000, 0})
    ->Args({100000, 0})
    ->Args({10000, 0})
    ->Args({1000, 0});


// BENCHMARK(BoostIntrusiveSetTest)
//     ->Iterations(1)
//     ->MeasureProcessCPUTime()
//     ->UseRealTime()
//     ->Unit(benchmark::kMillisecond);

// BENCHMARK(FollyIntrusiveHeapTest)
//     ->Iterations(1)
//     ->MeasureProcessCPUTime()
//     ->UseRealTime()
//     ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();