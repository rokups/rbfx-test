#ifndef __TRACYLOCK_HPP__
#define __TRACYLOCK_HPP__

#include <atomic>
#include <limits>

#include "../common/TracySystem.hpp"
#include "../common/TracyAlign.hpp"
#include "TracyProfiler.hpp"

namespace tracy
{

extern std::atomic<uint32_t> s_lockCounter;

template<class T>
class Lockable
{
public:
    tracy_force_inline Lockable( const SourceLocation* srcloc )
        : m_id( s_lockCounter.fetch_add( 1, std::memory_order_relaxed ) )
    {
        assert( m_id != std::numeric_limits<uint32_t>::max() );

        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockAnnounce );
        MemWrite( &item->lockAnnounce.id, m_id );
        MemWrite( &item->lockAnnounce.lckloc, (uint64_t)srcloc );
        MemWrite( &item->lockAnnounce.type, LockType::Lockable );
        tail.store( magic + 1, std::memory_order_release );
    }

    Lockable( const Lockable& ) = delete;
    Lockable& operator=( const Lockable& ) = delete;

    tracy_force_inline void lock()
    {
        const auto thread = GetThreadHandle();
        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockWait );
            MemWrite( &item->lockWait.id, m_id );
            MemWrite( &item->lockWait.thread, thread );
            MemWrite( &item->lockWait.time, Profiler::GetTime() );
            MemWrite( &item->lockWait.type, LockType::Lockable );
            tail.store( magic + 1, std::memory_order_release );
        }

        m_lockable.lock();

        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockObtain );
            MemWrite( &item->lockObtain.id, m_id );
            MemWrite( &item->lockObtain.thread, thread );
            MemWrite( &item->lockObtain.time, Profiler::GetTime() );
            tail.store( magic + 1, std::memory_order_release );
        }
    }

    tracy_force_inline void unlock()
    {
        m_lockable.unlock();

        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockRelease );
        MemWrite( &item->lockRelease.id, m_id );
        MemWrite( &item->lockRelease.thread, GetThreadHandle() );
        MemWrite( &item->lockRelease.time, Profiler::GetTime() );
        tail.store( magic + 1, std::memory_order_release );
    }

    tracy_force_inline bool try_lock()
    {
        const auto ret = m_lockable.try_lock();
        if( ret )
        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockObtain );
            MemWrite( &item->lockObtain.id, m_id );
            MemWrite( &item->lockObtain.thread, GetThreadHandle() );
            MemWrite( &item->lockObtain.time, Profiler::GetTime() );
            tail.store( magic + 1, std::memory_order_release );
        }
        return ret;
    }

    tracy_force_inline void Mark( const SourceLocation* srcloc ) const
    {
        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockMark );
        MemWrite( &item->lockMark.id, m_id );
        MemWrite( &item->lockMark.thread, GetThreadHandle() );
        MemWrite( &item->lockMark.srcloc, (uint64_t)srcloc );
        tail.store( magic + 1, std::memory_order_release );
    }

private:
    T m_lockable;
    uint32_t m_id;
};


template<class T>
class SharedLockable
{
public:
    tracy_force_inline SharedLockable( const SourceLocation* srcloc )
        : m_id( s_lockCounter.fetch_add( 1, std::memory_order_relaxed ) )
    {
        assert( m_id != std::numeric_limits<uint32_t>::max() );

        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockAnnounce );
        MemWrite( &item->lockAnnounce.id, m_id );
        MemWrite( &item->lockAnnounce.lckloc, (uint64_t)srcloc );
        MemWrite( &item->lockAnnounce.type, LockType::SharedLockable );
        tail.store( magic + 1, std::memory_order_release );
    }

    SharedLockable( const SharedLockable& ) = delete;
    SharedLockable& operator=( const SharedLockable& ) = delete;

    tracy_force_inline void lock()
    {
        const auto thread = GetThreadHandle();
        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockWait );
            MemWrite( &item->lockWait.id, m_id );
            MemWrite( &item->lockWait.thread, thread );
            MemWrite( &item->lockWait.time, Profiler::GetTime() );
            MemWrite( &item->lockWait.type, LockType::SharedLockable );
            tail.store( magic + 1, std::memory_order_release );
        }

        m_lockable.lock();

        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockObtain );
            MemWrite( &item->lockObtain.id, m_id );
            MemWrite( &item->lockObtain.thread, thread );
            MemWrite( &item->lockObtain.time, Profiler::GetTime() );
            tail.store( magic + 1, std::memory_order_release );
        }
    }

    tracy_force_inline void unlock()
    {
        m_lockable.unlock();

        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockRelease );
        MemWrite( &item->lockRelease.id, m_id );
        MemWrite( &item->lockRelease.thread, GetThreadHandle() );
        MemWrite( &item->lockRelease.time, Profiler::GetTime() );
        tail.store( magic + 1, std::memory_order_release );
    }

    tracy_force_inline bool try_lock()
    {
        const auto ret = m_lockable.try_lock();
        if( ret )
        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockObtain );
            MemWrite( &item->lockObtain.id, m_id );
            MemWrite( &item->lockObtain.thread, GetThreadHandle() );
            MemWrite( &item->lockObtain.time, Profiler::GetTime() );
            tail.store( magic + 1, std::memory_order_release );
        }
        return ret;
    }

    tracy_force_inline void lock_shared()
    {
        const auto thread = GetThreadHandle();
        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockSharedWait );
            MemWrite( &item->lockWait.id, m_id );
            MemWrite( &item->lockWait.thread, thread );
            MemWrite( &item->lockWait.time, Profiler::GetTime() );
            MemWrite( &item->lockWait.type, LockType::SharedLockable );
            tail.store( magic + 1, std::memory_order_release );
        }

        m_lockable.lock_shared();

        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockSharedObtain );
            MemWrite( &item->lockObtain.id, m_id );
            MemWrite( &item->lockObtain.thread, thread );
            MemWrite( &item->lockObtain.time, Profiler::GetTime() );
            tail.store( magic + 1, std::memory_order_release );
        }
    }

    tracy_force_inline void unlock_shared()
    {
        m_lockable.unlock_shared();

        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockSharedRelease );
        MemWrite( &item->lockRelease.id, m_id );
        MemWrite( &item->lockRelease.thread, GetThreadHandle() );
        MemWrite( &item->lockRelease.time, Profiler::GetTime() );
        tail.store( magic + 1, std::memory_order_release );
    }

    tracy_force_inline bool try_lock_shared()
    {
        const auto ret = m_lockable.try_lock_shared();
        if( ret )
        {
            Magic magic;
            auto& token = s_token.ptr;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            MemWrite( &item->hdr.type, QueueType::LockSharedObtain );
            MemWrite( &item->lockObtain.id, m_id );
            MemWrite( &item->lockObtain.thread, GetThreadHandle() );
            MemWrite( &item->lockObtain.time, Profiler::GetTime() );
            tail.store( magic + 1, std::memory_order_release );
        }
        return ret;
    }

    tracy_force_inline void Mark( const SourceLocation* srcloc ) const
    {
        Magic magic;
        auto& token = s_token.ptr;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        MemWrite( &item->hdr.type, QueueType::LockMark );
        MemWrite( &item->lockMark.id, m_id );
        MemWrite( &item->lockMark.thread, GetThreadHandle() );
        MemWrite( &item->lockMark.srcloc, (uint64_t)srcloc );
        tail.store( magic + 1, std::memory_order_release );
    }

private:
    T m_lockable;
    uint32_t m_id;
};


};

#endif