#include <EpollTimer.hpp>
#include <Utils.hpp>
#include <benchmark/benchmark.h>

#include <cstdint>
#include <gtest/gtest.h>

struct TestData
{
    bool m_executed{false};
    std::optional<std::chrono::steady_clock::time_point> m_timedout;
    std::optional<std::chrono::steady_clock::time_point> m_cancelled;
    std::atomic_int64_t m_execution_counter{0};
};

struct TestPayload : public sia::epoll::timer::TimerTaskPayload
{
    TestData* m_test_data{nullptr};
};

auto callback_on_timer = EPOLL_TIMER_CALLBACK_GENERATOR(){};

void MultiTaskScheduleBenchmark(const std::uint64_t batch_size, sia::epoll::timer::EpollTimer& timer,
                                sia::epoll::timer::EpollTimerScheduler& scheduler, benchmark::State& state)
{
    struct TestDataPack
    {
        TestData* m_test_data;
        std::chrono::milliseconds m_expire;
    };

    std::uint64_t timeout_limit = state.range(1);

    auto schedule_timers = [timeout_limit, &scheduler]()
    {
        auto timeout = generateRandomNumber(timeout_limit);
        auto result = scheduler.schedule(std::chrono::milliseconds{timeout}, callback_on_timer, nullptr, -1);
    };

    for (std::size_t i = 0; i < batch_size; ++i)
    {
        schedule_timers();
        timer.clearPipe();
    }

    // scheduler.schedule(std::chrono::milliseconds{500}, callback_on_timer, nullptr, -1);

    state.ResumeTiming();
    timer.loopConsumeAll();
    state.PauseTiming();
}

void MultiTaskScheduleBenchmarkEntry(benchmark::State& state)
{
    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};
    std::uint64_t batch_size = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();
        MultiTaskScheduleBenchmark(batch_size, timer, scheduler, state);
        state.ResumeTiming();
    }
}