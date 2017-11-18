#pragma once
/*
 * This file is modified from https://github.com/progschj/ThreadPool
 * -----------------------------------------------------------------
 *Copyright (c) 2012 Jakob Progsch, Václav Zeman

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
-------------------------------------
*/


#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    using PIF = std::pair<int, std::function<void()> >;
    class Compare {
    public:
        bool operator()(PIF& a , PIF& b) {
            return a.first > b.first;
        }
    };
    std::mutex write_mutex;
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue

    std::priority_queue<
    PIF,
    std::vector<PIF>,
    Compare
    > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition, condition_resource;
    int resource;
    bool stop;

public:
// the constructor just launches some amount of workers
    ThreadPool(size_t threads, size_t _resource)
        :   resource(_resource), stop(false)
    {
        for(size_t i = 0; i<threads; ++i)
            workers.emplace_back(
                [this]
        {
            for(;;)
            {
                std::function<void()> task;
                int pri;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock,
                    [this] { return this->stop || !this->tasks.empty(); });
                    if(this->stop && this->tasks.empty())
                        return;
                    task = std::move(this->tasks.top().second);
                    pri = this->tasks.top().first;
                    this->tasks.pop();
                }
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition_resource.wait(lock,
                                                  [this] { return this->resource > 0; });
                    this->resource-=pri;
                }
                task();
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->resource+=pri;
                }
                condition_resource.notify_all();
            }
        }
        );
    }

// add new work item to the pool
    template<class F>
    std::future<int> enqueue(int x, F&& f)
    {
        using return_type = int;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
                        std::bind(std::forward<F>(f))
                    );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace(std::make_pair(x, [task]() {
                (*task)();
            }));
        }
        condition.notify_one();
        return res;
    }

// the destructor joins all threads
    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }

};

