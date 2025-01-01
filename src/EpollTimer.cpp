#include <EpollTimer.hpp>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <iostream>
#include <thread>

namespace sia::epoll::timer
{

bool ReadWritePipe::setReadAsNonBlocking()
{
    int flags = fcntl(readFD(), F_GETFL, 0);

    if (flags == -1)
    {
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(readFD(), F_SETFL, flags) == -1)
    {
        return false;
    }

    return true;
}

bool ReadWritePipe::setWriteAsNonBlocking()
{
    int flags = fcntl(writeFD(), F_GETFL, 0);

    if (flags == -1)
    {
        return false;
    }

    flags |= O_NONBLOCK;

    if (fcntl(writeFD(), F_SETFL, flags) == -1)
    {
        return false;
    }

    return true;
}

bool ReadWritePipe::open() noexcept
{
    return (pipe(m_fds) == -1) ? false : true;
}

void ReadWritePipe::close() noexcept
{
    ::close(readFD());
    ::close(writeFD());
}

bool TimerTaskProxy::cancelTimer() noexcept
{
    bool rc{false};
    std::scoped_lock<std::mutex> lock{m_lock};

    if (m_task != nullptr)
    {
        m_task->deactivate();
        rc = true;
    }

    return rc;
}

void TimerTaskProxy::detachTask()
{
    std::scoped_lock<std::mutex> lock{m_lock};
    m_task = nullptr;
}

EpollTimer::EpollTimer()
{
    m_epoll_fd = epoll_create1(0);

    if (m_epoll_fd == -1)
    {
        throw std::runtime_error{"Epoll instance creation failed! Error:" + std::string{strerror(errno)}};
    }

    m_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

    if (m_timer_fd == -1)
    {
        close(m_epoll_fd);
        throw std::runtime_error{"Timer instance creation failed! Error:" + std::string{strerror(errno)}};
    }

    if (false == m_pipe.open())
    {
        m_pipe.close();
        close(m_timer_fd);
        close(m_epoll_fd);
        throw std::runtime_error{"Pipe creation failed! Error:" + std::string{strerror(errno)}};
    }

    if (false == m_pipe.setReadAsNonBlocking())
    {
        m_pipe.close();
        close(m_timer_fd);
        close(m_epoll_fd);
        throw std::runtime_error{"Setting read side of pipe as nonblocking failed! Error:" +
                                 std::string{strerror(errno)}};
    }

    if (false == m_pipe.setWriteAsNonBlocking())
    {
        m_pipe.close();
        close(m_timer_fd);
        close(m_epoll_fd);
        throw std::runtime_error{"Setting write side of pipe as nonblocking failed! Error:" +
                                 std::string{strerror(errno)}};
    }

    struct epoll_event timer_added_event;
    timer_added_event.events = EPOLLIN;
    timer_added_event.data.fd = m_pipe.readFD();

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_pipe.readFD(), &timer_added_event) == -1)
    {
        m_pipe.close();
        close(m_timer_fd);
        close(m_epoll_fd);
        throw std::runtime_error{"Adding timer added event to epoll interest list failed! Error: " +
                                 std::string{strerror(errno)}};
    }

    struct epoll_event timer_event;
    timer_event.events = EPOLLIN;
    timer_event.data.fd = m_timer_fd;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_timer_fd, &timer_event) == -1)
    {
        m_pipe.close();
        close(m_timer_fd);
        close(m_epoll_fd);
        throw std::runtime_error{"Adding timer event to epoll interest list failed! Error: " +
                                 std::string{strerror(errno)}};
    }
}

EpollTimer::~EpollTimer()
{
    m_pipe.close();
    close(m_timer_fd);
    close(m_epoll_fd);
}

void EpollTimer::updateTimerFD()
{
    if (m_processing_timers.empty())
    {
        m_to_be_expired.reset();
        struct itimerspec timer_spec;
        timer_spec.it_interval = {0, 0};
        timer_spec.it_value = {0, 0};

        if (timerfd_settime(m_timer_fd, 0, &timer_spec, nullptr) == -1)
        {
            std::runtime_error{"Could not disable timer. Error:" + std::string{strerror(errno)}};
        }

        return;
    }

    auto* earliest_task = &*m_processing_timers.begin();

    if (m_to_be_expired.has_value() && (earliest_task->getExpiration() == m_to_be_expired.value()))
    {
        return;
    }

    m_to_be_expired = earliest_task->getExpiration();

    struct itimerspec timer_spec;
    timer_spec.it_interval = {0, 0};
    timer_spec.it_value = {0, 0};

    auto current = std::chrono::steady_clock::now();
    auto delta = earliest_task->getExpiration() - current;

    if (delta < std::chrono::microseconds(1000))
    {
        delta = std::chrono::microseconds(1000);
    }

    timer_spec.it_value.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(delta).count();
    timer_spec.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count() % 1'000'000'000LL;

    if (timerfd_settime(m_timer_fd, 0, &timer_spec, nullptr) == -1)
    {
        std::runtime_error{"Could not disable timer. Error:" + std::string{strerror(errno)}};
    }
}

void EpollTimer::processTimers()
{
    while (false == m_processing_timers.empty())
    {
        auto itr = m_processing_timers.begin();
        auto* expiring_task = &*itr;

        if (false == expiring_task->isActive())
        {
            m_processing_timers.erase(itr);
            expiring_task->invokeCallback(Reason::kCancel);
            expiring_task->getProxy()->detachTask();

            delete expiring_task;
            continue;
        }

        auto this_moment = std::chrono::steady_clock::now();

        if (expiring_task->getExpiration() > this_moment)
        {
            break;
        }

        m_processing_timers.erase(itr);

        // Done with this task. Mark this task as deactivated.
        //
        expiring_task->deactivate();

        // We are safe to invoke the callback.
        // This invoke may cause addition of new timers.
        // However, these timers will be added into the pending timers list.
        //
        expiring_task->invokeCallback(Reason::kExpire);

        expiring_task->getProxy()->detachTask();

        delete expiring_task;
    }

    movePendingTimers();
    updateTimerFD();
}

void EpollTimer::loopLogic()
{
    std::int32_t num_of_events{0};
    struct epoll_event timer_event[2];

    do
    {
        num_of_events = epoll_wait(m_epoll_fd, timer_event, 2, 1000);
    } while (num_of_events == -1 && errno == EINTR);

    clearPipe();

    if (num_of_events < 0)
    {
        std::cerr << "ERROR: num_of_events is " << num_of_events << '\n';
        return;
    }

    if (num_of_events == 0)
    {
        movePendingTimers();
        updateTimerFD();
        return;
    }

    if (num_of_events > 2)
    {
        std::cerr << "ERROR: num_of_events is " << num_of_events << '\n';
        return;
    }

    for (int i = 0; i < num_of_events; ++i)
    {
        if (timer_event[i].data.fd == m_timer_fd)
        {
            processTimers();
        }
        else if (timer_event[i].data.fd == m_pipe.readFD())
        {
            movePendingTimers();
            updateTimerFD();
        }
    }
}

void EpollTimer::loop()
{
    movePendingTimers();
    updateTimerFD();

    while (true)
    {
        if (m_loop_break.load(std::memory_order_relaxed) && m_processing_timers.empty())
        {
            m_loop_break.store(false, std::memory_order_relaxed);
            break;
        }

        loopLogic();
    }
}

void EpollTimer::loopConsumeAll()
{
    movePendingTimers();
    updateTimerFD();

    while (!m_processing_timers.empty())
    {
        loopLogic();
    }
}

void EpollTimer::breakLoop()
{
    m_loop_break.store(true, std::memory_order_relaxed);
}

void EpollTimer::movePendingTimers()
{
    std::scoped_lock<std::mutex> lock{m_pending_timers_lock};
    auto iter = m_pending_timers.begin();
    while (iter != m_pending_timers.end())
    {
        auto& task = *iter;
        iter = m_pending_timers.erase(iter);
        m_processing_timers.insert(task);
    }
}

Status EpollTimer::addTimerTask(TimerTask* timer_task)
{
    std::scoped_lock<std::mutex> lock{m_pending_timers_lock};
    auto itr = m_pending_timers.insert(*timer_task);
    std::int32_t written_bytes = write(m_pipe.writeFD(), "item_added", 10);
    Status status{Status::kSuccess};

    if (written_bytes == -1)
    {
        // Not counted as failure. Let the client to try again.
        //
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            status = Status::kTryAgain;
        }
        else
        {
            // Otherwise count as serious problem and ignore this task.
            //
            status = Status::kFailure;
            std::cerr << "ERROR: Could not be added the task. error_no:" << errno << " error_explain:" << strerror(errno) << '\n';
        }
    }
    // Partial write. We count this as failure for us.
    //
    else if (written_bytes != 10)
    {
        status = Status::kFailure;
    }

    if (status != Status::kSuccess)
    {
        m_pending_timers.erase(itr);
    }

    return status;
}

void EpollTimer::removeTimerTask(TimerTask* timer_task)
{
    // Deactivated while this is in processing timer list.
    //
    timer_task->deactivate();

    // Erase this from the pending timer list as well.
    //
    // std::scoped_lock<std::mutex> lock{m_pending_timers_lock};
    // m_pending_timers.erase(*timer_task);
}

void EpollTimer::clearPipe()
{
    char buffer[1024];
    ssize_t bytes_read;

    // Read from the pipe until it's empty
    while ((bytes_read = read(m_pipe.readFD(), buffer, sizeof(buffer))) > 0)
    {
    }

    if (bytes_read == -1 && errno != EAGAIN)
    {
        std::cerr << "ERROR: Reading from pipe failed. Error:" << strerror(errno) << '\n';
    }
}

std::shared_ptr<TimerTaskProxy> EpollTimerScheduler::schedule(
    const TimerTask::TimePoint& time_point, const Callback& callback, std::unique_ptr<TimerTaskPayload> payload,
    const std::optional<std::chrono::milliseconds>& backoff_timeout)
{
    TimerTask* task = new TimerTask(time_point, callback, std::move(payload));
    auto proxy = task->getProxy();

    Status status = m_epoll_timer.addTimerTask(task);

    if (status == Status::kSuccess)
    {
        return proxy;
    }

    if (backoff_timeout.has_value())
    {
        std::chrono::steady_clock::time_point backoff_point =
            std::chrono::steady_clock::now() + backoff_timeout.value();

        // Loop until the backoff or success or fail.
        //
        while (status == Status::kTryAgain && std::chrono::steady_clock::now() < backoff_point)
        {
            status = m_epoll_timer.addTimerTask(proxy->getTask());
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }

        if (status == Status::kSuccess)
        {
            return proxy;
        }

        delete task;
        return nullptr;
    }

    // Loop forever until we successfully add this task or fail.
    //
    while (status == Status::kTryAgain)
    {
        status = m_epoll_timer.addTimerTask(proxy->getTask());
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }

    if (status == Status::kSuccess)
    {
        return proxy;
    }

    delete task;
    return nullptr;
}

std::shared_ptr<TimerTaskProxy> EpollTimerScheduler::schedule(
    const std::chrono::seconds& timeout, const Callback& callback, std::unique_ptr<TimerTaskPayload> payload,
    const std::optional<std::chrono::milliseconds>& backoff_timeout)
{
    TimerTask::TimePoint scheduled_timeout{std::chrono::steady_clock::now() + timeout};
    return schedule(scheduled_timeout, callback, std::move(payload), backoff_timeout);
}

std::shared_ptr<TimerTaskProxy> EpollTimerScheduler::schedule(
    const std::chrono::milliseconds& timeout, const Callback& callback, std::unique_ptr<TimerTaskPayload> payload,
    const std::optional<std::chrono::milliseconds>& backoff_timeout)
{
    TimerTask::TimePoint scheduled_timeout{std::chrono::steady_clock::now() + timeout};
    return schedule(scheduled_timeout, callback, std::move(payload), backoff_timeout);
}

}  // namespace sia::epoll::timer
