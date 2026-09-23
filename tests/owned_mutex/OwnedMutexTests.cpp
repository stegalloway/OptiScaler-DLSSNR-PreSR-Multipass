#include "OwnedMutexUnderTest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    OwnedMutex mutex;
    mutex.lock(4);
    if (!mutex.ownedByCurrentThread(4) || mutex.ownedByCurrentThread(5))
        return 1;

    std::atomic<bool> started = false;
    std::atomic<bool> entered = false;
    std::atomic<bool> wrongThreadSkipped = false;
    std::thread worker([&]
                       {
                           wrongThreadSkipped = mutex.ownedByCurrentThread(4);
                           started = true;
                           mutex.lock(3);
                           entered = true;
                           if (!mutex.ownedByCurrentThread(3))
                               wrongThreadSkipped = true;
                           mutex.unlockThis(3);
                       });
    while (!started)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool blockedOtherThread = !entered;
    mutex.unlockThis(4);
    worker.join();
    if (!blockedOtherThread || !entered || wrongThreadSkipped)
        return 2;

    mutex.lock(5);
    const bool present1Nested = mutex.ownedByCurrentThread(5);
    mutex.unlockThis(5);
    if (!present1Nested || mutex.ownedByCurrentThread(5))
        return 3;
    std::puts("PASS same-thread recursive bypass; other thread remains blocked");
    return 0;
}
