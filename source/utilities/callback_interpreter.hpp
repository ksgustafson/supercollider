//  templated callback system
//  Copyright (C) 2008, 2009 Tim Blechmann
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; see the file COPYING.  If not, write to
//  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
//  Boston, MA 02111-1307, USA.

#ifndef UTILITIES_CALLBACK_INTERPRETER_HPP
#define UTILITIES_CALLBACK_INTERPRETER_HPP

#include "branch_hints.hpp"
#include "callback_system.hpp"

#include "nova-tt/semaphore.hpp"
#include "nova-tt/thread_priority.hpp"

#include <boost/atomic.hpp>
#include <boost/bind.hpp>
#include <boost/checked_delete.hpp>
#include <boost/ref.hpp>
#include <boost/thread.hpp>


namespace nova
{

namespace detail
{

template <class callback_type,
          class callback_deleter = boost::checked_deleter<callback_type> >
class callback_interpreter_base:
    callback_system<callback_type>
{
protected:
    callback_interpreter_base(void):
        sem(0), running(false)
    {}

public:
    void add_callback(callback_type * cb)
    {
        callback_system<callback_type>::add_callback(cb);
        sem.post();
    }

    void run(void)
    {
        running.store(true, boost::memory_order_relaxed);

        do
        {
            sem.wait();
            callback_system<callback_type>::run_callbacks();
        }
        while(likely(running.load(boost::memory_order_relaxed)));
    }

protected:
    semaphore sem;
    boost::atomic<bool> running;
};


} /* namespace detail */

template <class callback_type,
          class callback_deleter = boost::checked_deleter<callback_type> >
class callback_interpreter:
    public detail::callback_interpreter_base<callback_type, callback_deleter>
{
    typedef detail::callback_interpreter_base<callback_type, callback_deleter> super;

public:
    callback_interpreter(void)
    {}

    void run(void)
    {
        super::run();
    }

    void terminate(void)
    {
        super::running.store(false, boost::memory_order_relaxed);
        super::sem.post();
    }

    boost::thread start_thread(void)
    {
        semaphore sync_sem;
        semaphore_sync sync(sync_sem);
        return boost::thread (boost::bind(&callback_interpreter::run, this, boost::ref(sync_sem)));
    }

private:
    void run(semaphore & sync_sem)
    {
        sync_sem.post();
        run();
    }
};

template <class callback_type,
          class callback_deleter = boost::checked_deleter<callback_type> >
class callback_interpreter_threadpool:
    public detail::callback_interpreter_base<callback_type, callback_deleter>
{
    typedef detail::callback_interpreter_base<callback_type, callback_deleter> super;

public:
    callback_interpreter_threadpool(uint16_t worker_thread_count, bool rt, uint16_t priority):
        worker_thread_count_(worker_thread_count), priority(priority), rt(rt)
    {
        semaphore sync_sem;
        for (uint16_t i = 0; i != worker_thread_count; ++i)
            threads.create_thread(boost::bind(&callback_interpreter_threadpool::run, this, boost::ref(sync_sem)));

        for (uint16_t i = 0; i != worker_thread_count; ++i)
            sync_sem.wait();
    }

    ~callback_interpreter_threadpool(void)
    {
        super::running.store(false, boost::memory_order_relaxed);

        for (uint16_t i = 0; i != worker_thread_count_; ++i)
            super::sem.post();
        threads.join_all();
    }

private:
    void run(semaphore & sync_sem)
    {
        sync_sem.post();

        if (rt)
            thread_set_priority_rt(priority);
        else
            thread_set_priority(priority);

        super::run();
    }

    boost::thread_group threads;
    uint16_t worker_thread_count_;
    uint16_t priority;
    bool rt;
};


} /* namespace nova */

#endif /* UTILITIES_CALLBACK_INTERPRETER_HPP */
