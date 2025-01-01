// #include <EpollTimer.hpp>
// #include <chrono>
// #include <iostream>
// #include <thread>
// #include <random>

// int generateRandomNumber()
// {
//     // Create a random number generator
//     std::random_device rd;                         // Obtain a seed from the hardware (if available)
//     std::mt19937 gen(rd());                        // Initialize the generator with the seed
//     std::uniform_int_distribution<> dist(1, 5000);  // Define the range [1, 100]

//     return dist(gen);  // Generate and return a random number
// }

// void timeout(const std::shared_ptr<sia::epoll::timer::TimerTaskProxy>& task, sia::epoll::timer::Reason reason)
// {
//     if(reason == sia::epoll::timer::Reason::kExpire)
//     {
//         std::cout << "TRACE: Task has expired.\n";
//     }
//     else
//     {
//         std::cout << "TRACE: Task has cancelled.\n";
//     }
// }

// void timerLoop(sia::epoll::timer::EpollTimer* timer)
// {
//     timer->loop();
// }

// int main()
// {
//     sia::epoll::timer::EpollTimer timer;

//     sia::epoll::timer::EpollTimerScheduler scheduler{timer};

//     std::chrono::time_point<std::chrono::steady_clock> dt{std::chrono::steady_clock::now() + std::chrono::seconds{10}};

//     sia::epoll::timer::Callback cb{[](const std::shared_ptr<sia::epoll::timer::TimerTaskProxy>& task,
//                                       sia::epoll::timer::Reason reason) { timeout(task, reason); }};

//     auto proxy = scheduler.schedule(dt, cb);
//     if (proxy == nullptr)
//     {
//         std::cerr << "Could not schedule the timer\n";
//     }

//     auto proxy1 = scheduler.schedule(std::chrono::milliseconds{11000}, cb);
//     if (proxy == nullptr)
//     {
//         std::cerr << "Could not schedule the timer\n";
//     }

//     std::thread timer_thread{timerLoop, &timer};

//     proxy->cancelTimer();

//     int counter{0};

//     while(counter++ < 100)
//     {
//         scheduler.schedule(std::chrono::milliseconds{generateRandomNumber()}, cb);
//     }

//     std::puts("Press any key to stop!");
//     char c;
//     std::cin >> c;

//     timer.breakLoop();
//     timer_thread.join();

//     return 0;
// }