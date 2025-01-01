#pragma once

#include <boost/intrusive/set.hpp>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace sia::epoll::timer
{

enum class Status : std::uint8_t
{
    kSuccess = 0,
    kTryAgain,
    kFailure
};

enum class Reason : std::uint8_t
{
    kExpire = 0,
    kCancel = 1
};

struct TimerTask;
struct EpollTimer;

struct TimerTaskPayload
{
    virtual ~TimerTaskPayload() = default;
};

struct TimerTaskProxy final
{
    explicit TimerTaskProxy(TimerTask* task) : m_task(task)
    {
    }

    explicit TimerTaskProxy(TimerTask* task, std::unique_ptr<TimerTaskPayload> payload)
        : m_task(task), m_payload(std::move(payload))
    {
    }

    bool cancelTimer() noexcept;

    std::unique_ptr<TimerTaskPayload>& getPayload()
    {
        return m_payload;
    }

    friend struct EpollTimer;
    friend struct EpollTimerScheduler;

    private:
    void detachTask();

    TimerTask* getTask() noexcept
    {
        return m_task;
    }

    mutable std::mutex m_lock;
    TimerTask* m_task{nullptr};
    std::unique_ptr<TimerTaskPayload> m_payload{nullptr};
};

struct Callback final
{
    using CBType = std::function<void(const std::shared_ptr<TimerTaskProxy>& task, Reason reason)>;

    explicit Callback(const CBType& cb) : m_cb(cb)
    {
    }

    void operator()(const std::shared_ptr<TimerTaskProxy>& task, Reason reason)
    {
        m_cb(task, reason);
    }

    private:
    const CBType m_cb;
};

struct TimerTask : public boost::intrusive::set_base_hook<boost::intrusive::optimize_size<true>>
{
    using TimePoint = std::chrono::steady_clock::time_point;

    explicit TimerTask(const TimePoint& time_point, const Callback& callback)
        : m_expiration(time_point), m_callback(callback)
    {
        m_proxy = std::make_shared<TimerTaskProxy>(this);
    }

    explicit TimerTask(const TimePoint& time_point, const Callback& callback, std::unique_ptr<TimerTaskPayload> payload)
        : m_expiration(time_point), m_callback(callback)
    {
        m_proxy = std::make_shared<TimerTaskProxy>(this, std::move(payload));
    }

    virtual ~TimerTask() = default;

    friend bool operator<(const TimerTask& a, const TimerTask& b)
    {
        return a.m_expiration < b.m_expiration;
    }

    friend bool operator>(const TimerTask& a, const TimerTask& b)
    {
        return a.m_expiration > b.m_expiration;
    }

    friend bool operator==(const TimerTask& a, const TimerTask& b)
    {
        return a.m_expiration == b.m_expiration;
    }

    const TimePoint getExpiration() const noexcept
    {
        return m_expiration;
    }

    void deactivate() noexcept
    {
        m_is_active.store(false, std::memory_order_relaxed);
    }

    bool isActive() const noexcept
    {
        return m_is_active.load(std::memory_order_relaxed);
    }

    void invokeCallback(Reason reason)
    {
        m_callback(getProxy(), reason);
    }

    std::shared_ptr<TimerTaskProxy> getProxy() const noexcept
    {
        return m_proxy;
    }

    private:
    std::atomic_bool m_is_active{true};
    const TimePoint m_expiration;
    std::shared_ptr<TimerTaskProxy> m_proxy;
    Callback m_callback;

    public:
    boost::intrusive::set_member_hook<> m_member_hook;
};

struct ReadWritePipe final
{
    ReadWritePipe() = default;
    ReadWritePipe(const ReadWritePipe&) = delete;
    ReadWritePipe(ReadWritePipe&&) = delete;

    ~ReadWritePipe()
    {
        close();
    }

    std::int32_t readFD() const noexcept
    {
        return m_fds[0];
    }

    std::int32_t writeFD() const noexcept
    {
        return m_fds[1];
    }

    bool valid() const noexcept
    {
        return m_valid;
    }

    bool open() noexcept;
    void close() noexcept;

    bool setReadAsNonBlocking();
    bool setWriteAsNonBlocking();

    private:
    std::int32_t m_fds[2];
    bool m_valid{false};
};

using MemberOption =
    boost::intrusive::member_hook<TimerTask, boost::intrusive::set_member_hook<>, &TimerTask::m_member_hook>;
using MemberMultiset = boost::intrusive::multiset<TimerTask, MemberOption>;

struct EpollTimer
{
    EpollTimer();
    ~EpollTimer();

    EpollTimer(const EpollTimer&) = delete;
    EpollTimer(EpollTimer&&) = delete;
    EpollTimer& operator=(const EpollTimer&) = delete;
    EpollTimer& operator=(EpollTimer&&) = delete;

    Status addTimerTask(TimerTask* timer_task);
    void processTimers();
    void removeTimerTask(TimerTask* timer_task);
    void updateTimerFD();
    void loopLogic();
    void loop();
    void loopConsumeAll();
    void breakLoop();
    void movePendingTimers();
    void clearPipe();

    private:
    using IntrusiveSet = MemberMultiset;

    std::int32_t m_epoll_fd;
    std::int32_t m_timer_fd;
    ReadWritePipe m_pipe;

    std::mutex m_pending_timers_lock;
    IntrusiveSet m_processing_timers;
    IntrusiveSet m_pending_timers;
    std::atomic_bool m_loop_break{false};
    std::optional<std::chrono::steady_clock::time_point> m_to_be_expired;
};

struct EpollTimerScheduler final
{
    explicit EpollTimerScheduler(EpollTimer& epoll_timer) : m_epoll_timer{epoll_timer}
    {
    }

    std::shared_ptr<TimerTaskProxy> schedule(
        const TimerTask::TimePoint& time_point, const Callback& callback,
        std::unique_ptr<TimerTaskPayload> payload = nullptr,
        const std::optional<std::chrono::milliseconds>& backoff_timeout = std::nullopt);

    std::shared_ptr<TimerTaskProxy> schedule(
        const std::chrono::seconds& timeout, const Callback& callback,
        std::unique_ptr<TimerTaskPayload> payload = nullptr,
        const std::optional<std::chrono::milliseconds>& backoff_timeout = std::nullopt);

    std::shared_ptr<TimerTaskProxy> schedule(
        const std::chrono::milliseconds& timeout, const Callback& callback,
        std::unique_ptr<TimerTaskPayload> payload = nullptr,
        const std::optional<std::chrono::milliseconds>& backoff_timeout = std::nullopt);

    private:
    EpollTimer& m_epoll_timer;
};

}  // namespace sia::epoll::timer