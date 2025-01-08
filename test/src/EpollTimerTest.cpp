#include <EpollTimer.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <random>
#include <thread>
#include <vector>

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

int generateRandomNumber()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 100);
    return dist(gen);
}

auto callback_on_timer = EPOLL_TIMER_CALLBACK_GENERATOR()
{
    auto payload = std::move(task->getPayload());

    if (payload != nullptr)
    {
        std::unique_ptr<TestPayload> test_payload{static_cast<TestPayload*>(payload.release())};

        if (test_payload->m_test_data != nullptr)
        {
            test_payload->m_test_data->m_executed = true;
            --test_payload->m_test_data->m_execution_counter;

            if (reason == sia::epoll::timer::Reason::kExpire)
            {
                test_payload->m_test_data->m_timedout = std::chrono::steady_clock::now();
            }
            else
            {
                test_payload->m_test_data->m_cancelled = std::chrono::steady_clock::now();
            }
        }
    }

    EXPECT_TRUE(task->getPayload() == nullptr);
};

TEST(EpollTimerTest, BasicSingleTask)
{
    auto* test_data = new TestData();
    auto payload = std::make_unique<TestPayload>();
    payload->m_test_data = test_data;

    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    EXPECT_TRUE(scheduler.schedule(std::chrono::seconds{1}, callback_on_timer, std::move(payload)).second != nullptr);

    timer.loopConsumeAll();

    EXPECT_TRUE(test_data->m_executed);
    delete test_data;
}

TEST(EpollTimerTest, BasicMultipleTask)
{
    auto* test_data1 = new TestData();
    auto payload1 = std::make_unique<TestPayload>();
    payload1->m_test_data = test_data1;

    auto* test_data2 = new TestData();
    auto payload2 = std::make_unique<TestPayload>();
    payload2->m_test_data = test_data2;

    auto* test_data3 = new TestData();
    auto payload3 = std::make_unique<TestPayload>();
    payload3->m_test_data = test_data3;

    auto* test_data4 = new TestData();
    auto payload4 = std::make_unique<TestPayload>();
    payload4->m_test_data = test_data4;

    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    auto result1 = scheduler.schedule(std::chrono::milliseconds{1}, callback_on_timer, std::move(payload1));
    auto result2 = scheduler.schedule(std::chrono::milliseconds{1}, callback_on_timer, std::move(payload2));
    auto result3 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload3));
    auto result4 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload4));

    EXPECT_TRUE(result1.second != nullptr);
    EXPECT_TRUE(result2.second != nullptr);
    EXPECT_TRUE(result3.second != nullptr);
    EXPECT_TRUE(result4.second != nullptr);

    timer.loopConsumeAll();

    EXPECT_TRUE(test_data1->m_executed);
    EXPECT_FALSE(test_data1->m_cancelled.has_value());
    EXPECT_TRUE(test_data1->m_timedout.has_value());

    EXPECT_TRUE(test_data2->m_executed);
    EXPECT_FALSE(test_data2->m_cancelled.has_value());
    EXPECT_TRUE(test_data2->m_timedout.has_value());

    EXPECT_TRUE(test_data3->m_executed);
    EXPECT_FALSE(test_data3->m_cancelled.has_value());
    EXPECT_TRUE(test_data3->m_timedout.has_value());

    EXPECT_TRUE(test_data4->m_executed);
    EXPECT_FALSE(test_data4->m_cancelled.has_value());
    EXPECT_TRUE(test_data4->m_timedout.has_value());

    EXPECT_TRUE(test_data1->m_timedout->time_since_epoch().count() <=
                test_data3->m_timedout->time_since_epoch().count());
    EXPECT_TRUE(test_data1->m_timedout->time_since_epoch().count() <=
                test_data4->m_timedout->time_since_epoch().count());

    EXPECT_TRUE(test_data2->m_timedout->time_since_epoch().count() <=
                test_data3->m_timedout->time_since_epoch().count());
    EXPECT_TRUE(test_data2->m_timedout->time_since_epoch().count() <=
                test_data4->m_timedout->time_since_epoch().count());

    delete test_data1;
    delete test_data2;
    delete test_data3;
    delete test_data4;
}

TEST(EpollTimerTest, BasicMultipleTaskCancel)
{
    auto* test_data1 = new TestData();
    auto payload1 = std::make_unique<TestPayload>();
    payload1->m_test_data = test_data1;

    auto* test_data2 = new TestData();
    auto payload2 = std::make_unique<TestPayload>();
    payload2->m_test_data = test_data2;

    auto* test_data3 = new TestData();
    auto payload3 = std::make_unique<TestPayload>();
    payload3->m_test_data = test_data3;

    auto* test_data4 = new TestData();
    auto payload4 = std::make_unique<TestPayload>();
    payload4->m_test_data = test_data4;

    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    auto result3 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload3));
    auto result4 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload4));

    auto result1 = scheduler.schedule(
        std::chrono::milliseconds{1},
        EPOLL_TIMER_CALLBACK_GENERATOR(result3) {
            result3.second->cancelTimer();
            callback_on_timer(task, reason);
        },
        std::move(payload1));

    auto result2 = scheduler.schedule(
        std::chrono::milliseconds{1},
        EPOLL_TIMER_CALLBACK_GENERATOR(result4) {
            result4.second->cancelTimer();
            callback_on_timer(task, reason);
        },
        std::move(payload2));

    EXPECT_TRUE(result1.second != nullptr);
    EXPECT_TRUE(result2.second != nullptr);
    EXPECT_TRUE(result3.second != nullptr);
    EXPECT_TRUE(result4.second != nullptr);

    timer.loopConsumeAll();

    EXPECT_TRUE(test_data1->m_executed);
    EXPECT_FALSE(test_data1->m_cancelled.has_value());
    EXPECT_TRUE(test_data1->m_timedout.has_value());

    EXPECT_TRUE(test_data2->m_executed);
    EXPECT_FALSE(test_data2->m_cancelled.has_value());
    EXPECT_TRUE(test_data2->m_timedout.has_value());

    EXPECT_TRUE(test_data3->m_executed);
    EXPECT_TRUE(test_data3->m_cancelled.has_value());
    EXPECT_FALSE(test_data3->m_timedout.has_value());

    EXPECT_TRUE(test_data4->m_executed);
    EXPECT_TRUE(test_data4->m_cancelled.has_value());
    EXPECT_FALSE(test_data4->m_timedout.has_value());

    EXPECT_TRUE(test_data1->m_timedout->time_since_epoch().count() <=
                test_data3->m_cancelled->time_since_epoch().count());
    EXPECT_TRUE(test_data1->m_timedout->time_since_epoch().count() <=
                test_data4->m_cancelled->time_since_epoch().count());

    EXPECT_TRUE(test_data2->m_timedout->time_since_epoch().count() <=
                test_data3->m_cancelled->time_since_epoch().count());
    EXPECT_TRUE(test_data2->m_timedout->time_since_epoch().count() <=
                test_data4->m_cancelled->time_since_epoch().count());

    delete test_data1;
    delete test_data2;
    delete test_data3;
    delete test_data4;
}

TEST(EpollTimerTest, ComplexMultiThreadRandomTest)
{
    auto worker_thread_func = [](sia::epoll::timer::EpollTimerScheduler* scheduler, std::int64_t batch_size)
    {
        std::int64_t counter{0};
        auto* test_data = new TestData();
        test_data->m_execution_counter = batch_size;

        while (counter++ < batch_size)
        {
            auto payload = std::make_unique<TestPayload>();
            payload->m_test_data = test_data;
            auto duration = generateRandomNumber();

            auto result =
                scheduler->schedule(std::chrono::milliseconds{duration}, callback_on_timer, std::move(payload));

            EXPECT_TRUE(result.second != nullptr);

            // Adding some extra complexity.
            //
            if (duration % 10 == 0)
            {
                result.second->cancelTimer();
            }
        }

        // Wait for 5 seconds which should be enough for the last scheduled timer task.
        //
        std::this_thread::sleep_for(std::chrono::seconds{5});

        EXPECT_TRUE(test_data->m_execution_counter == 0);

        delete test_data;
    };

    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    // Create the timer thread.
    //
    auto timer_thread_func = [](sia::epoll::timer::EpollTimer* timer) { timer->loop(); };
    std::thread timer_thread{timer_thread_func, &timer};

    constexpr std::uint32_t kMaxThreadCount = 1000;
    constexpr std::int64_t kBatchSize = 1000;

    // Create worker threads.
    //
    std::vector<std::thread> threads{kMaxThreadCount};

    for (std::size_t i = 0; i < threads.size(); ++i)
    {
        threads[i] = std::thread{worker_thread_func, &scheduler, kBatchSize};
    }

    // Wait for worker threads.
    //
    for (std::size_t i = 0; i < threads.size(); ++i)
    {
        threads[i].join();
    }

    timer.breakLoop();
    timer_thread.join();
}

TEST(EpollTimerTest, BasicSingleTaskTimeoutValidation)
{
    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    TestData* test_data = new TestData();
    auto payload = std::make_unique<TestPayload>();
    payload->m_test_data = test_data;

    auto result = scheduler.schedule(std::chrono::milliseconds{10}, callback_on_timer, std::move(payload), 0);

    EXPECT_TRUE(result.first == sia::epoll::timer::Status::kSuccess);
    EXPECT_TRUE(result.second != nullptr);

    auto expiration = result.second->getExpiration();

    timer.loopConsumeAll();

    EXPECT_TRUE(test_data->m_timedout.value() >= expiration.value());

    auto elapsed = test_data->m_timedout.value() - expiration.value();

    constexpr std::chrono::nanoseconds tolerance{10 * 1000000};

    EXPECT_TRUE(elapsed <= tolerance);
}

TEST(EpollTimerTest, BasicMultipleTaskTimeoutValidation)
{
    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    struct TestDataPack
    {
        TestData* m_test_data;
        std::chrono::milliseconds m_expire;
    };

    std::vector<TestDataPack> test_data_list;

    auto schedule_timers = [&test_data_list, &scheduler]()
    {
        TestData* test_data = new TestData();
        auto payload = std::make_unique<TestPayload>();
        payload->m_test_data = test_data;

        // auto timeout = generateRandomNumber();
        std::int32_t timeout = 10;
        auto result = scheduler.schedule(std::chrono::milliseconds{timeout}, callback_on_timer, std::move(payload), 0);

        EXPECT_TRUE(result.first == sia::epoll::timer::Status::kSuccess);
        EXPECT_TRUE(result.second != nullptr);

        auto expiration = result.second->getExpiration();

        EXPECT_TRUE(expiration.has_value());

        test_data_list.push_back(TestDataPack{test_data, std::chrono::milliseconds{timeout}});
    };

    constexpr std::uint32_t kBatchSize = 10000;

    // auto process_begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kBatchSize; ++i)
    {
        schedule_timers();
        timer.clearPipe();
    }
    auto process_end = std::chrono::steady_clock::now();

    timer.loopConsumeAll();

    // If there is a heavy load on the system this tolerance may not be enough.
    //
    constexpr std::chrono::nanoseconds tolerance{10 * 1000000};  // 10ms tolerance.

    for (auto& data : test_data_list)
    {
        EXPECT_TRUE(data.m_test_data->m_timedout.value().time_since_epoch() >= process_end.time_since_epoch());
        auto effective_elapsed_time = data.m_test_data->m_timedout.value() - (process_end + data.m_expire);
        // std::cout << effective_elapsed_time.count() << " vs " << (data.m_expire + tolerance).count() << '\n';
        EXPECT_TRUE(effective_elapsed_time <= (data.m_expire + tolerance));

        delete data.m_test_data;
    }
}

TEST(EpollTimerTest, BasicScheduleMultipleTasksInTimerCallback)
{
    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    TestData* test_data1 = new TestData;
    TestData* test_data2 = new TestData;

    test_data1->m_execution_counter = 1;
    test_data2->m_execution_counter = 1;

    auto callback_on_timer_schedule_tasks = EPOLL_TIMER_CALLBACK_GENERATOR(&scheduler, test_data1, test_data2)
    {
        EXPECT_TRUE(reason == sia::epoll::timer::Reason::kExpire);

        auto payload = std::move(task->getPayload());
        std::unique_ptr<TestPayload> test_payload{nullptr};

        if (payload != nullptr)
        {
            test_payload.reset(std::move(static_cast<TestPayload*>(payload.release())));
        }

        if (test_payload != nullptr)
        {
            test_payload->m_test_data->m_timedout = std::chrono::steady_clock::now();
            test_payload->m_test_data->m_execution_counter--;
        }

        auto payload1 = std::make_unique<TestPayload>();
        auto payload2 = std::make_unique<TestPayload>();

        payload1->m_test_data = test_data1;
        payload2->m_test_data = test_data2;

        sia::epoll::timer::Result result1 =
            scheduler.schedule(std::chrono::milliseconds{100}, callback_on_timer, std::move(payload1), 0);

        sia::epoll::timer::Result result2 =
            scheduler.schedule(std::chrono::milliseconds{101}, callback_on_timer, std::move(payload2), 0);

        EXPECT_TRUE(result1.first == sia::epoll::timer::Status::kSuccess);
        EXPECT_TRUE(result1.second != nullptr);

        EXPECT_TRUE(result2.first == sia::epoll::timer::Status::kSuccess);
        EXPECT_TRUE(result2.second != nullptr);
    };

    TestData* test_data = new TestData;
    test_data->m_execution_counter = 1;
    auto payload = std::make_unique<TestPayload>();
    payload->m_test_data = test_data;

    sia::epoll::timer::Result result =
        scheduler.schedule(std::chrono::milliseconds{100}, callback_on_timer_schedule_tasks, std::move(payload), 0);

    EXPECT_TRUE(result.first == sia::epoll::timer::Status::kSuccess);
    EXPECT_TRUE(result.second != nullptr);

    timer.loopConsumeAll();

    EXPECT_TRUE(test_data->m_timedout.has_value());
    EXPECT_EQ(test_data->m_execution_counter, 0);

    EXPECT_TRUE(test_data1->m_timedout.has_value());
    EXPECT_EQ(test_data1->m_execution_counter, 0);

    EXPECT_TRUE(test_data2->m_timedout.has_value());
    EXPECT_EQ(test_data2->m_execution_counter, 0);

    // Check if first timer (100ms) expired before the second timer (101ms).
    //
    EXPECT_TRUE(test_data1->m_timedout.value() <= test_data2->m_timedout.value());
}