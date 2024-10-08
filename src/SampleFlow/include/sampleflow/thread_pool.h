/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2023 by Wolfgang Bangerth.
 *
 * This file is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE.md at
 * the top level directory of deal.II.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Wolfgang Bangerth, Colorado State University, 2023.
 *
 * The class in this file implements a simple thread pool that can
 * execute tasks by mapping them to idle threads. This is a
 * work-around for problems with the original version of this
 * benchmark that used std::async in hopes of letting the compiler
 * run-time do the same: Schedule tasks to available resources.
 *
 * When using std::async with default values for its first argument,
 * the C++ standard describes the result in terms that suggest that
 * compilers should schedule these tasks to "available resources and
 * defer execution of tasks if none are currently available". This
 * certainly suggests the use of a thread pool, but at least as of
 * early 2023, compilers including GCC do not actually do that: They
 * just spawn a new thread. This is not the intent (though there may
 * be good technical reasons for the choice I'm not aware of), and the
 * result is that that early version of the benchmark created and
 * destroyed thousands or tens of thousands of threads just to execute
 * tasks that take a few milliseconds.
 *
 * To avoid both the overhead associated with this, and to produce a
 * better benchmark, this file implements a simple thread pool that
 * can execute collections of tasks. It is not a full-blown
 * task-scheduling thing, but supports the fork-join style of this
 * benchmark where a number of tasks are enqueued and then we wait for
 * all of them to finish again.
 */

#include <deque>
#include <vector>
#include <thread>
#include <memory>
#include <condition_variable>

namespace SampleFlow
{
  class ThreadPool
  {
  public:
    ThreadPool (const unsigned int max_threads);
    ~ThreadPool ();

    template <typename TaskType>
    void enqueue_task (TaskType &&task);

    void join_all ();
    
  private:
    unsigned int n_worker_threads;
    
    bool stop_signal;
    
    std::vector<std::thread> worker_threads;
    
    std::mutex queue_mutex;
    std::deque<std::pair<unsigned int,std::shared_ptr<std::function<void ()>>>> task_queue;
    unsigned int currently_executing_tasks;

    std::mutex wake_up_mutex;
    std::condition_variable wake_up_signal;
    
    void worker_thread(const unsigned int thread_number);
  };


  inline
  ThreadPool::ThreadPool (const unsigned int max_threads)
  :
  stop_signal (false),
  currently_executing_tasks (0)
  {
    // EXAMPLES: There are 3 inputs to consider:
    //   - the number of chains (passed as max_threads),
    //   - the available number of virtual CPUs (std::thread::hardware_concurrency),
    //   - the user's desire for how many CPUs to consume (expressed as OMP_NUM_THREADS)
    // Complication: the master thread eats a virtual CPU.
    // The examples below may help clarify the intended computation of n_worker_threads:
    //    max_threads aka chains  64   64   64   64   64  64  64  64
    //    hw_concurrency         192  192  192  192  192   8   8   8
    //    OMP_NUM_THREADS         32   64   65  128   na  na  16   4
    //    concurrency             31   63   64  127  191   7   7   3
    //    n_worker_threads        31   63   64   64   64   7   7   3

    unsigned int concurrency;
    if(const char* env_p = std::getenv("OMP_NUM_THREADS"))
      concurrency = std::min<unsigned int> (std::atoi(env_p),
                                            std::thread::hardware_concurrency());
    else
      // If not explicitly set, just take the number of cores in the
      // system. Note that hardware_concurrency() is documented as
      // possibly returning zero if the system does not have the
      // capability to say how many cores it actually might have. In
      // that case, we will just bail below and not start any threads
      // at all.
      concurrency = std::thread::hardware_concurrency();

    // Reserve one thread for the main thread that spawns all of the
    // tasks, and allow OMP_NUM_THREADS-1 as workers in this pool:
    if (concurrency != 0)
      --concurrency;

    // Limit the concurrency in case someone sets OMP_NUM_THREADS to
    // something large (or not at all, and is on a large system) but
    // we only have a fairly low number of chains:
    n_worker_threads = std::min(concurrency, max_threads);
    
    // Start all of the worker threads if we are allowed concurrency
    // (i.e., if n_worker_threads>=1).
    if (n_worker_threads >= 1)
      {
        worker_threads.reserve (n_worker_threads);
        for (unsigned int t=0; t<n_worker_threads; ++t)
          worker_threads.emplace_back ([this,t]() { worker_thread(t); } );
      }
    else
      {
        // No concurrency requested or possible -- in that case, just run
        // everything sequentially.
      }  
  }
  


  inline
  ThreadPool::~ThreadPool ()
  {
    join_all();
    
    // Get the queue_mutex, set the stop signal, and then wait for all
    // of the threads to realize what's going on. For them to get to
    // the state where they actually pay attention, we have to notify
    // them via the condition variable:
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      stop_signal = true;
    }
    wake_up_signal.notify_all();
    
    for (auto &t : worker_threads)
      t.join();
  }    


  template <typename TaskType>
  inline
  void ThreadPool::enqueue_task (TaskType &&task)
  {
    if (n_worker_threads >= 1)
      {
        static int n_tasks = 0;
    
        // Get the lock and put the task into the queue:
        {
          std::lock_guard<std::mutex> lock(queue_mutex);

          task_queue.emplace_back (n_tasks,
                                   std::make_shared<std::function<void ()>>(std::move(task)));
        }

        // Now make sure that at least one of the threads actually wakes
        // up:
        wake_up_signal.notify_one();
      }
    else
      {
        // No concurrency. Just execute the task outright:
        task();
      }
  }


  inline
  void ThreadPool::join_all()
  {
    if (n_worker_threads >= 1)
      {
        // Wait for all currently enqueued tasks to finish. Do this by
        // repeatedly getting the lock and checking the size of the
        // queue. If it is zero, return. If it is nonzero, or if it is
        // zero but one of the tasks is still executing, tell the OS
        // to do something else instead.
        while (true)
          {
            {
              std::lock_guard<std::mutex> lock(queue_mutex);
              if (task_queue.empty() && (currently_executing_tasks == 0))
                return;
            }

            std::this_thread::yield();
          }
      }
  }
  

  inline
  void ThreadPool::worker_thread(const unsigned int thread_number)
  {
    while (true)
      {
        std::function<void ()> task;
        unsigned int this_task;

        // Get the queue_mutex, check whether we are supposed to stop work,
        // and if not, see if there is work
        {
          std::lock_guard<std::mutex> lock(queue_mutex);

          if (stop_signal)
            return;
        
          if (task_queue.size() > 0)
            {
              // There is work. Get one of the tasks and take it out of the queue:
              this_task = task_queue.front().first;
              task = std::move(*task_queue.front().second);
              task_queue.pop_front();

              ++currently_executing_tasks;
            }

          // Give back the lock here:
        }
        

        
        // If there was work, execute it. Once done, decrement the
        // counter for the currently running tasks.
        if (task)
          {
            task();

            std::lock_guard<std::mutex> lock(queue_mutex);
            --currently_executing_tasks;
          }
        else
          {
            // There was no work for us to do. Let the OS do something
            // else instead and wait until we get asked to wake up via
            // a condition variable:
            std::unique_lock<std::mutex> lock(wake_up_mutex);
            wake_up_signal.wait(lock);
          }
        
      }
  }
}
