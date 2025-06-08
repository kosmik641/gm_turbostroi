#pragma once

#if defined(_WIN32)
#define USE_WIN32_CS
#elif defined(POSIX)
#define USE_PTHREAD_MTX
#endif

#if defined(USE_WIN32_CS)
#include <Windows.h>
#include <memory>
using Mtx = CRITICAL_SECTION;
#elif defined(USE_PTHREAD_MTX)
#include <pthread.h>
#include <memory>
using Mtx = pthread_mutex_t;
#else
#include <mutex>
#include <memory>
using Mtx = std::mutex;
#endif

class Mutex
{
public:
    Mutex();
    ~Mutex();

    void lock();
    void unlock();
    bool try_lock();
private:
    std::unique_ptr<Mtx> m_mutex;
};