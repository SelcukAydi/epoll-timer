#include "benchmark/benchmark.h"
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>
#include "folly/container/IntrusiveHeap.h"
#include "folly/io/async/HHWheelTimer-fwd.h"
#include <folly/io/async/test/Util.h>
#include <folly/io/async/test/UndelayedDestruction.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <random>
#include <vector>
#include <boost/intrusive/set.hpp>
#include <folly/io/async/TimerFDTimeoutManager.h>
#include <Utils.hpp>

using folly::HHWheelTimer;
using folly::TimePoint;

using StackWheelTimer = folly::TimerFDTimeoutManager;

class TestTimeout : public folly::TimerFDTimeoutManager::Callback {
 public:
  TestTimeout() {}
  TestTimeout(folly::TimerFDTimeoutManager* t, std::chrono::milliseconds timeout) {
    t->scheduleTimeout(this, timeout);
  }

  void timeoutExpired() noexcept override {
    timestamps.emplace_back();
    if (fn) {
      fn();
    }
  }

  void callbackCanceled() noexcept override {
    canceledTimestamps.emplace_back();
    if (fn) {
      fn();
    }
  }

  std::deque<TimePoint> timestamps;
  std::deque<TimePoint> canceledTimestamps;
  std::function<void()> fn;
};

void MultiTaskScheduleBenchmarkFolly(const std::uint64_t batch_size, folly::EventBase& event_base,
                                           StackWheelTimer& timer, benchmark::State& state)
{
    std::vector<TestTimeout*> tms;
    std::uint64_t max_timer{0};

    std::uint64_t timeout_limit = state.range(1);

    for (std::size_t i = 0; i < batch_size; ++i)
    {
        TestTimeout* test = new TestTimeout;
        std::uint64_t timeout = generateRandomNumber(timeout_limit);
        max_timer = std::max(timeout, max_timer);
        timer.scheduleTimeout(test, std::chrono::milliseconds(timeout));
        tms.push_back(test);
    }

    // We need to schedule a max timer to exit the loop.
    //
    TestTimeout ts;
    ts.fn = [&]() { event_base.terminateLoopSoon(); };
    timer.scheduleTimeout(&ts, std::chrono::milliseconds(max_timer));

    state.ResumeTiming();
    event_base.loop();
    state.PauseTiming();

    for (std::size_t i = 0; i < batch_size; ++i)
    {
        delete tms[i];
    }
}

void MultiTaskScheduleBenchmarkFollyEntry(benchmark::State& state)
{
    folly::EventBase event_base;
    StackWheelTimer timer{&event_base};
    std::uint64_t batch_size = state.range(0);

    for (auto _ : state)
    {
        state.PauseTiming();
        MultiTaskScheduleBenchmarkFolly(batch_size, event_base, timer, state);
        state.ResumeTiming();
    }
}

static constexpr std::uint64_t kNumItems = 10000;

void FollyIntrusiveHeapTest(benchmark::State& state)
{
    struct TimerItem : public folly::IntrusiveHeapNode<>
    {
        bool operator<(const TimerItem& other) const
        {
            // IntrusiveHeap is a max-heap.
            return expiration > other.expiration;
        }

        static void freeFunction(void* v)
        {
            delete static_cast<TimerItem*>(v);
        }

        ~TimerItem()
        {
            DCHECK(!isLinked());
        }

        std::chrono::steady_clock::time_point expiration;
        struct event* ev;
    };

    for (auto _ : state)
    {
        folly::IntrusiveHeap<TimerItem> timer_list;
        std::vector<TimerItem*> release_list;

        for (std::size_t i = 0; i < kNumItems; ++i)
        {
            TimerItem* item = new TimerItem;
            timer_list.push(item);
            release_list.push_back(item);
        }

        for (std::size_t i = 0; i < kNumItems; ++i)
        {
            timer_list.erase(release_list[i]);
        }
    }
}

void BoostIntrusiveSetTest(benchmark::State& state)
{
    struct TimerItem : public boost::intrusive::set_base_hook<boost::intrusive::optimize_size<true>>
    {
        bool operator<(const TimerItem& other) const
        {
            // IntrusiveHeap is a max-heap.
            return expiration > other.expiration;
        }

        static void freeFunction(void* v)
        {
            delete static_cast<TimerItem*>(v);
        }

        ~TimerItem()
        {
        }

        std::chrono::steady_clock::time_point expiration;
        struct event* ev;
    };

    for (auto _ : state)
    {
        boost::intrusive::set<TimerItem> timer_list;
        std::vector<TimerItem*> release_list;

        for (std::size_t i = 0; i < kNumItems; ++i)
        {
            TimerItem* item = new TimerItem;
            timer_list.insert(*item);
            release_list.push_back(item);
        }

        for (std::size_t i = 0; i < kNumItems; ++i)
        {
            timer_list.erase(*release_list[i]);
        }
    }
}