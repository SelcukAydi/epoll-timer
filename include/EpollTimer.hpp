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
#include <utility>

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

/**
 * @brief A proxy object referring to a scheduled timer task. Any operation on a timer task must be
 * performed using this proxy object.
 *
 * Proxy objects are thread-safe on operations which involve the underlying timer task. For example,
 * proxy objects can cancel any timer at any time if the timer task is still valid.
 */
struct TimerTaskProxy final
{
    public:
    explicit TimerTaskProxy(TimerTask* task) : m_task(task)
    {
    }

    explicit TimerTaskProxy(TimerTask* task, std::unique_ptr<TimerTaskPayload> payload)
        : m_task(task), m_payload(std::move(payload))
    {
    }

    public:
    /**
     * @brief Cancels the underlying task.
     * @note Note that the cancellation is just a desire. The timer might have been already expired
     * or canceled or deleted from the memory. However, in any case, calling this method is thread-safe.
     * Moreover, once the timer is canceled the cancel callback can be executed randomly. So do not assume
     * The cancel callback will be called immediately.
     *
     * @return Returns true if the cancellation is successful otherwise false. Cancelling already cancelled
     * timers do not take any action and this method returns true.
     */
    bool cancelTimer() noexcept;

    /**
     * @brief Returns the payload data attached to the underlying timer task.
     * @return A reference to the unique pointer of the attached payload data.
     */
    std::unique_ptr<TimerTaskPayload>& getPayload()
    {
        return m_payload;
    }

    /**
     * @brief Returns the associated expiration time point.
     * @return An optional value of time point.
     */
    std::optional<std::chrono::steady_clock::time_point> getExpiration();

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

struct TimerTask : public boost::intrusive::set_base_hook<boost::intrusive::optimize_size<true>>
{
    using TimePoint = std::chrono::steady_clock::time_point;
    using CBType = std::function<void(const std::shared_ptr<TimerTaskProxy>& task, Reason reason)>;

    explicit TimerTask(const TimePoint& time_point, const CBType& callback) : m_expiration(time_point), m_cb(callback)
    {
        m_proxy = std::make_shared<TimerTaskProxy>(this);
    }

    explicit TimerTask(const TimePoint& time_point, const CBType& callback, std::unique_ptr<TimerTaskPayload> payload)
        : m_expiration(time_point), m_cb(callback)
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
        // m_callback(getProxy(), reason);
        m_cb(getProxy(), reason);
    }

    std::shared_ptr<TimerTaskProxy> getProxy() const noexcept
    {
        return m_proxy;
    }

    private:
    std::atomic_bool m_is_active{true};
    const TimePoint m_expiration;
    std::shared_ptr<TimerTaskProxy> m_proxy;
    CBType m_cb;

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

/**
 * @brief Epoll based timer object which manages all the scheduled timer tasks in an intrusive container.
 */
struct EpollTimer
{
    EpollTimer();
    ~EpollTimer();

    EpollTimer(const EpollTimer&) = delete;
    EpollTimer(EpollTimer&&) = delete;
    EpollTimer& operator=(const EpollTimer&) = delete;
    EpollTimer& operator=(EpollTimer&&) = delete;

    /**
     * @brief Starts the main loop on the timer object. This method should be invoked from dedicated threads.
     *
     * @note The loop can be broken by breakLoop() method.
     */
    void loop();

    /**
     * @brief Executes one-shot loop till all the timer objects are consumed pending to be expired.
     */
    void loopConsumeAll();

    /**
     * @brief Breaks the main loop.
     */
    void breakLoop();

    /**
     * @brief Clears newly-item-added pipe. This method should be called from test codes only.
     */
    void clearPipe();

    private:
    using IntrusiveSet = MemberMultiset;

    friend struct EpollTimerScheduler;

    void loopLogic();
    void processTimers();
    void updateTimerFD();
    void movePendingTimers();
    void removeTimerTask(TimerTask* timer_task);
    Status addTimerTask(TimerTask* timer_task);

    std::int32_t m_epoll_fd;
    std::int32_t m_timer_fd;
    ReadWritePipe m_pipe;

    std::mutex m_pending_timers_lock;
    IntrusiveSet m_processing_timers;
    IntrusiveSet m_pending_timers;
    std::atomic_bool m_loop_break{false};
    std::optional<std::chrono::steady_clock::time_point> m_to_be_expired;
};

using Result = std::pair<Status, std::shared_ptr<TimerTaskProxy>>;

/**
 * @brief Schedules the timers safely. Clients must use this class instead of directly accessing the EpollTimer.
 */
struct EpollTimerScheduler final
{
    explicit EpollTimerScheduler(EpollTimer& epoll_timer) : m_epoll_timer{epoll_timer}
    {
    }

    /**
     * @brief Schedules the desired timer safely with the given constraints.
     *
     * @param time_point The time point at which this timer must timeout.
     * @param callback The callback which is invoked on timeout.
     * @param payload Optional payload to be passed through the callback.
     * @param backoff_timeout_in_ms Controls the behavior of failure. The behavior depends on the following constraints:
     * if backoff_timeout_in_ms is negative then this method tries to schedule the timer and returns the result
     * immediately. if backoff_timeout_in_ms is zero then this method tries to schedule the timer if the status is
     * kTryAgain. Otherwise, it returns failure. if backoff_timeout_in_ms is positive then this method tries to schedule
     * the timer until the backoff timeout point.
     *
     * @return Returns Result object. This object is a pair of TiemrTaskProxy and a status that refers to the schedule
     * was failed or successful.
     *
     * If the schedule is successful then the proxy object is not nullptr. Otherwise, the proxy object is nullptr, and
     * the status refers to the failure.
     */
    Result schedule(const TimerTask::TimePoint& time_point, const TimerTask::CBType& callback,
                    std::unique_ptr<TimerTaskPayload> payload = nullptr, std::int64_t backoff_timeout_in_ms = 0);

    /**
     * @brief Schedules the desired timer safely with the given constraints.
     *
     * @param timeout The desired timeout in seconds that this timer must timeout.
     * @param callback The callback which is invoked on timeout.
     * @param payload Optional payload to be passed through the callback.
     * @param backoff_timeout_in_ms Controls the behavior of failure. The behavior depends on the following constraints:
     * if backoff_timeout_in_ms is negative then this method tries to schedule the timer and returns the result
     * immediately. if backoff_timeout_in_ms is zero then this method tries to schedule the timer if the status is
     * kTryAgain. Otherwise, it returns failure. if backoff_timeout_in_ms is positive then this method tries to schedule
     * the timer until the backoff timeout point.
     *
     * @return Returns Result object. This object is a pair of TiemrTaskProxy and a status that refers to the schedule
     * was failed or successful.
     *
     * If the schedule is successful then the proxy object is not nullptr. Otherwise, the proxy object is nullptr, and
     * the status refers to the failure.
     */
    Result schedule(const std::chrono::seconds& timeout, const TimerTask::CBType& callback,
                    std::unique_ptr<TimerTaskPayload> payload = nullptr, std::int64_t backoff_timeout_in_ms = 0);

    /**
     * @brief Schedules the desired timer safely with the given constraints.
     *
     * @param timeout The desired timeout in milliseconds that this timer must timeout.
     * @param callback The callback which is invoked on timeout.
     * @param payload Optional payload to be passed through the callback.
     * @param backoff_timeout_in_ms Controls the behavior of failure. The behavior depends on the following constraints:
     * if backoff_timeout_in_ms is negative then this method tries to schedule the timer and returns the result
     * immediately. if backoff_timeout_in_ms is zero then this method tries to schedule the timer if the status is
     * kTryAgain. Otherwise, it returns failure. if backoff_timeout_in_ms is positive then this method tries to schedule
     * the timer until the backoff timeout point.
     *
     * @return Returns Result object. This object is a pair of TiemrTaskProxy and a status that refers to the schedule
     * was failed or successful.
     *
     * If the schedule is successful then the proxy object is not nullptr. Otherwise, the proxy object is nullptr, and
     * the status refers to the failure.
     */
    Result schedule(const std::chrono::milliseconds& timeout, const TimerTask::CBType& callback,
                    std::unique_ptr<TimerTaskPayload> payload = nullptr, std::int64_t backoff_timeout_in_ms = 0);

    private:
    EpollTimer& m_epoll_timer;
};

#define EPOLL_TIMER_CALLBACK_PARAMS \
    const std::shared_ptr<sia::epoll::timer::TimerTaskProxy>&task, sia::epoll::timer::Reason reason

#define EPOLL_TIMER_CALLBACK_GENERATOR(...) [__VA_ARGS__](EPOLL_TIMER_CALLBACK_PARAMS)

}  // namespace sia::epoll::timer