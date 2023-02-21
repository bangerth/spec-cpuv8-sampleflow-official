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

namespace SampleFlow
{
  class ThreadPool
  {
  public:
    ThreadPool ();
    ~ThreadPool ();

    template <typename TaskType>
    void enqueue_task (TaskType &&task);

    void join_all ();
    
  private:
    unsigned int concurrency;
    
    bool stop_signal;
    
    std::vector<std::thread> worker_threads;
    
    std::mutex queue_mutex;
    std::deque<std::pair<unsigned int,std::shared_ptr<std::function<void ()>>>> task_queue;

    std::mutex wake_up_mutex;
    std::condition_variable wake_up_signal;
    
    void worker_thread(const unsigned int thread_number);
  };


  inline
  ThreadPool::ThreadPool ()
  :
  stop_signal (false)
  {
    if(const char* env_p = std::getenv("OMP_NUM_THREADS"))
      concurrency = std::min<unsigned int> (std::atoi(env_p),
                                            std::thread::hardware_concurrency());
    else
      concurrency = std::thread::hardware_concurrency();

    // Start all of the worker threads. If we are allowed concurrency
    // (i.e., if concurrency>=2), then start as many threads as there
    // are cores on the machine. Note that hardward_concurrency() is
    // documented as possibly returning zero if the system does not
    // have the capability to say how many cores it actually might
    // have. In that case, we just bail and not start any threads at
    // all.
    if (concurrency >= 2)
      {
        std::cout << "Starting thread pool with "
                  << concurrency << " threads." << std::endl;
    
        worker_threads.reserve (concurrency);
        for (unsigned int t=0; t<concurrency; ++t)
          worker_threads.emplace_back ([this,t]() { worker_thread(t); } );
      }
    else
      {
        std::cout << "Running sequentially without a thread pool." << std::endl;
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
    if (concurrency >= 2)
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
    if (concurrency >= 2)
      {
        // Wait for all currently enqueued tasks to finish. Do this by
        // repeatedly getting the lock and checking the size of the
        // queue. If it is zero, return. If it is nonzero, tell the OS to
        // do something else instead.
        while (true)
          {
            {
              std::lock_guard<std::mutex> lock(queue_mutex);
              if (task_queue.empty())
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
            }

          // Give back the lock here:
        }
        

        
        // If there was work, execute it
        if (task)
          {
            task();
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
