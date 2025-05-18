#include "mutex.h"

#if defined(USE_WIN32_CS)
Mutex::Mutex()
{
    m_mutex.reset(new Mtx);
    InitializeCriticalSection(m_mutex.get());
}

Mutex::~Mutex()
{
    DeleteCriticalSection(m_mutex.get());
    m_mutex.reset();
}

void Mutex::lock()
{
    EnterCriticalSection(m_mutex.get());
}

void Mutex::unlock()
{
    LeaveCriticalSection(m_mutex.get());
}

bool Mutex::try_lock()
{
    return TryEnterCriticalSection(m_mutex.get());
}
#elif defined(USE_PTHREAD_MTX)
Mutex::Mutex()
{
    m_mutex.reset(new Mtx);
}

Mutex::~Mutex()
{
    m_mutex.reset();
}

void Mutex::lock()
{
    pthread_mutex_lock(m_mutex.get());
}

void Mutex::unlock()
{
    pthread_mutex_unlock(m_mutex.get());
}

bool Mutex::try_lock()
{
    return (pthread_mutex_trylock(m_mutex.get()) == 0);
}
#else
Mutex::Mutex()
{
    m_mutex.reset(new Mtx);
}

Mutex::~Mutex()
{
    m_mutex.reset();
}

void Mutex::lock()
{
    m_mutex->lock();
}

void Mutex::unlock()
{
    m_mutex->unlock();
}

bool Mutex::try_lock()
{
    return m_mutex->try_lock();
}
#endif