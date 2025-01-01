#include <EpollTimer.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <gtest/gtest.h>
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
    std::uniform_int_distribution<> dist(1, 5000);
    return dist(gen);
}

sia::epoll::timer::Callback callback_on_timer{
    [](const std::shared_ptr<sia::epoll::timer::TimerTaskProxy>& task, sia::epoll::timer::Reason reason)
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
    }};

TEST(EpollTimerTest, BasicSingleTask)
{
    auto* test_data = new TestData();
    auto payload = std::make_unique<TestPayload>();
    payload->m_test_data = test_data;

    sia::epoll::timer::EpollTimer timer;
    sia::epoll::timer::EpollTimerScheduler scheduler{timer};

    EXPECT_TRUE(scheduler.schedule(std::chrono::seconds{1}, callback_on_timer, std::move(payload)) != nullptr);

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

    auto proxy1 = scheduler.schedule(std::chrono::milliseconds{1}, callback_on_timer, std::move(payload1));
    auto proxy2 = scheduler.schedule(std::chrono::milliseconds{1}, callback_on_timer, std::move(payload2));
    auto proxy3 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload3));
    auto proxy4 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload4));

    EXPECT_TRUE(proxy1 != nullptr);
    EXPECT_TRUE(proxy2 != nullptr);
    EXPECT_TRUE(proxy3 != nullptr);
    EXPECT_TRUE(proxy4 != nullptr);

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

    auto proxy3 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload3));
    auto proxy4 = scheduler.schedule(std::chrono::milliseconds{2}, callback_on_timer, std::move(payload4));

    auto proxy1 = scheduler.schedule(
        std::chrono::milliseconds{1},
        sia::epoll::timer::Callback{
            [proxy3](const std::shared_ptr<sia::epoll::timer::TimerTaskProxy>& task, sia::epoll::timer::Reason reason)
            {
                proxy3->cancelTimer();
                callback_on_timer(task, reason);
            }},
        std::move(payload1));

    auto proxy2 = scheduler.schedule(
        std::chrono::milliseconds{1},
        sia::epoll::timer::Callback{
            [proxy4](const std::shared_ptr<sia::epoll::timer::TimerTaskProxy>& task, sia::epoll::timer::Reason reason)
            {
                proxy4->cancelTimer();
                callback_on_timer(task, reason);
            }},
        std::move(payload2));

    EXPECT_TRUE(proxy1 != nullptr);
    EXPECT_TRUE(proxy2 != nullptr);
    EXPECT_TRUE(proxy3 != nullptr);
    EXPECT_TRUE(proxy4 != nullptr);

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

            auto proxy =
                scheduler->schedule(std::chrono::milliseconds{duration}, callback_on_timer, std::move(payload));

            EXPECT_TRUE(proxy != nullptr);

            // Adding some extra complexity.
            //
            if (duration % 10 == 0)
            {
                proxy->cancelTimer();
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