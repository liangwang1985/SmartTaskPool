/****************************************************************************\
* Created on Sat Jul 28 2018
*
* The MIT License (MIT)
* Copyright (c) 2018 leosocy
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the ",Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED ",AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
\*****************************************************************************/

#ifndef SMART_THREAD_POOL_H_
#define SMART_THREAD_POOL_H_

#include <atomic>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream> 
#include <queue>
#include <vector>
#include <map>
#include <algorithm>
#include <memory>
#include <mutex>
#include <functional>
#include <future>
#include <chrono>
#include <condition_variable>
#include <mutex>

#if _MSC_VER
#define snprintf _snprintf
#endif

namespace stp {

  /*
  *
  *                              \ Workers .../                            |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| UrgentTask HighTask MediumTask  |
  *                      |                                                 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |
  * SmartThreadPool ---->|       \ Workers .../                            |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| MediumTask LowTask DefaultTask  |
  *                      |                                                 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |
  *                      |       \ Workers ... /                           |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| UrgentTask LowTask DefaultTask  |
  *                      |                                                 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |
  *                      |       \ Workers ... /                           |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *                      |-----ClassifyThreadPool ---->TaskPriorityQueue-->| UrgentTask LowTask DefaultTask  |
  *                      |                                                 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
  *
  */

  
  class SmartThreadPool;
  class ClassifyThreadPool;
  class Worker;

  template<class CMP>
  class TaskPriorityQueue;
  class Task;

  class Daemon;
  class Monitor;

  class CMPTaskBypriority;
  class CMPTaskByStartTime;

  enum TaskPriority : unsigned char {
    DEFAULT = 0,
    LOW,
    MEDIUM,
    HIGH,
    URGENT,
  };

  // namespace detail {
  //   uint8_t g_auto_increment_thread_pool_id = 0;
  // }   // namespace detail

  class Semaphore {
    public:
        Semaphore(int value=1): count{value}, wakeups{0} {}

        void Notify(){
            std::unique_lock<std::mutex> lock(mutex_);
            if(++count<=0) { // have some thread suspended ?
                ++wakeups;
                condition_variable_.notify_one(); // notify one !
            }
        }
        
        template <class Clock, class Duration>
        bool WaitUntil(const std::chrono::time_point<Clock, Duration>& deadline) {
          std::unique_lock<std::mutex> lock(mutex_);
          if (--count < 0) { // count is not enough ?
              if(!condition_variable_.wait_until(lock, deadline,[this]()->bool{ return wakeups>0;})){
                count++;
                return false;
              }
              --wakeups;  // ok, me wakeup !
          }
          return true;
        }
        
        void Wait(){
            std::unique_lock<std::mutex> lock(mutex_);
            if (--count < 0){
                // condition_variable_.wait(lock,[this]()->bool{ return wakeups>0;});
                condition_variable_.wait(lock, 
                  [this]()->bool{ return wakeups>0;}); // suspend and wait ...
                --wakeups;  // ok, me wakeup !
            }
        }
    private:
        int count;
        int wakeups;
        std::mutex mutex_;
        std::condition_variable condition_variable_;
    };



  // // Definition of a Semaphore class that one can use for notifying, wait and
  // // and wait for a period of time. Because C++ does not have a built-in semaphore
  // // until C++20, it is necessary to define one if it is convenient to use it. 
  // class Semaphore {
  // public:
  //   // Default count is zero, which means calling Wait() immediately after
  //   // constructor this thread will go to sleep (waiting for count > 0)
  //   Semaphore(unsigned int count = 0) : count_(count) {}

  //   // Increment the counter and notify one of the thread that there is one 
  //   // available
  //   void Notify(){
  //     std::unique_lock<std::mutex> lock(mutex_);
  //     count_++;
  //     condition_variable_.notify_one();
  //   }

  //   // Wait for the counter to become positive, and consume one by decrementing
  //   void Wait(){
  //     std::unique_lock<std::mutex> lock(mutex_);
  //     condition_variable_.wait(lock, [this]() { return count_ > 0; });
  //     count_--;
  //   }

  //   // Wait for the counter to become positive until a specified time point at
  //   // deadline. If the counter becomes positive then consume it and return true,
  //   // otherwise return false
  //   template <class Clock, class Duration>
  //   bool WaitUntil(const std::chrono::time_point<Clock, Duration>& deadline) {
  //     std::unique_lock<std::mutex> lock(mutex_);
  //     if (!condition_variable_.wait_until(lock, deadline, [this]() {
  //       return count_ > 0; })) {
  //       return false;
  //     }

  //     count_--;
  //     return true;
  //   }

  // private:
  //   std::mutex mutex_;
  //   std::condition_variable condition_variable_;
  //   unsigned int count_;
  // };

  class Task {
  public:
    using TaskType = std::function<void()>;
    template<class T_duration>
    explicit Task(TaskType taskType, TaskPriority priority, std::string pool_name, T_duration delay_duration)
      : taskType_(taskType), priority_(priority), pool_name_(pool_name){
      auto now = std::chrono::high_resolution_clock::now();
      start_time_ = now + delay_duration;
    }
    Task(const Task& other)
      : taskType_(other.taskType_), priority_(other.priority_), pool_name_(other.pool_name_), start_time_(other.start_time_) {
    }
    Task& operator=(const Task& other) {
      taskType_ = other.taskType_;
      priority_ = other.priority_;
      pool_name_ = other.pool_name_;
      start_time_ = other.start_time_;
      return *this;
    }

    void Run() {
      taskType_();
    }

    std::string pool_name() const {
      return pool_name_;
    }
    TaskPriority priority() const {
      return priority_;
    }
    std::chrono::high_resolution_clock::time_point start_time() const{
      return start_time_;
    }

  private:
    friend CMPTaskBypriority;
    friend CMPTaskByStartTime;
    TaskType taskType_;

    TaskPriority priority_;
    std::string pool_name_;
    // Indicate the timepoint for this task to start
    std::chrono::high_resolution_clock::time_point start_time_;
  };

  class CMPTaskBypriority
  {
  public:
    bool operator()(Task &a, Task &b) const
    {
      //因为优先出列判定为!cmp，所以反向定义实现priority最大值优先
      return a.priority_ < b.priority_;
    }
  };

  class CMPTaskByStartTime
  {
  public:
    bool operator()(Task &a, Task &b) const
    {
      //因为优先出列判定为!cmp，所以反向定义实现start_time最小值优先
      return a.start_time_ > b.start_time_;
    }
  };

  template<class CMP>
  class TaskPriorityQueue {
  public:
    explicit TaskPriorityQueue(const char* queue_name, int wait_for_task_seconds)
      : queue_name_(queue_name), alive_(true), task_count_(0), pending_task_count_(0) {
    }
    TaskPriorityQueue(TaskPriorityQueue&& other) = delete;
    TaskPriorityQueue(const TaskPriorityQueue&) = delete;
    TaskPriorityQueue& operator=(TaskPriorityQueue&& other) = delete;
    TaskPriorityQueue& operator=(const TaskPriorityQueue&) = delete;
    ~TaskPriorityQueue() {
      ClearQueue();
    }
    void ClearQueue() {
      {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        alive_ = false;
      }
      queue_cv_.notify_all();
      auto task = dequeue();
      while (task) {
        task->Run();
        task = dequeue();
      }
    }
    bool empty() const {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      return tasks_.empty();
    }
    std::size_t size() const {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      return tasks_.size();
    }
    template<class T_duration, class F, class... Args>
    auto enqueue(TaskPriority priority, std::string pool_name, T_duration delay_duration, F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
      using ReturnType = typename std::result_of<F(Args...)>::type;
      auto taskType = std::make_shared< std::packaged_task<ReturnType()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
      {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        tasks_.emplace([taskType](){ (*taskType)(); }, priority, pool_name, delay_duration);
        task_count_ += 1;
        pending_task_count_ += 1;
      }
      queue_cv_.notify_one();
      return taskType->get_future();
    }
    
    void enqueue(std::unique_ptr<Task>& task) {
      {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        tasks_.emplace(*task.release());
        task_count_ += 1;
        pending_task_count_ += 1;
      }
      queue_cv_.notify_one();
    }

    std::chrono::high_resolution_clock::time_point topTaskStartTime() {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      queue_cv_.wait(lock, [this]{ return !alive_ || !tasks_.empty(); });
      if (!alive_ || tasks_.empty()) {
        return std::chrono::high_resolution_clock::now();
      }
      return tasks_.top().start_time();
    }
    std::unique_ptr<Task> dequeue() {
      std::unique_lock<std::mutex> lock(queue_mtx_);
      if (wait_for_task_seconds_ > 0)
      {
        bool status = queue_cv_.wait_for(lock, std::chrono::seconds(wait_for_task_seconds_), [this]{ return !alive_ || !tasks_.empty(); });
        if (!status || !alive_ || tasks_.empty()) {
          return nullptr;
        }
      } 
      else
      {
        queue_cv_.wait(lock, [this]{ return !alive_ || !tasks_.empty(); });
        if (!alive_) {
          return nullptr;
        }
      }
      auto task = std::unique_ptr<Task>{new Task(std::move(tasks_.top()))};
      tasks_.pop();
      pending_task_count_ -= 1;
      task_count_ -= 1;
      return task;
    }
    const char* name() const { return queue_name_.c_str(); }
    uint64_t task_count() const { 
      std::unique_lock<std::mutex> lock(queue_mtx_);
      return task_count_; 
    }
    uint64_t pending_task_count() const { 
      std::unique_lock<std::mutex> lock(queue_mtx_);
      return pending_task_count_; 
    }

  private:
    std::string queue_name_;
    std::priority_queue<Task, std::vector<Task>, CMP> tasks_;
    mutable std::mutex queue_mtx_;
    mutable std::condition_variable queue_cv_;
    int wait_for_task_seconds_;
    

#if _MSC_VER
    //std::atomic_bool alive_;
    std::atomic<bool> alive_;
    //bool alive_;
#else
    std::atomic_bool alive_;
#endif
    uint64_t task_count_;
    uint64_t pending_task_count_;
  };

  class Worker {
  public:
    Worker(Worker&&) = delete;
    Worker(const Worker&) = delete;
    enum State : unsigned char {
      IDLE = 0,
      BUSY,
      EXITED,
    };
    explicit Worker(TaskPriorityQueue<CMPTaskBypriority>* queue)
      : state_(State::IDLE), completed_task_count_(0) 
    {
      t_ = std::move(std::thread([queue, this]() {
        while (true) {
          auto task = queue->dequeue();
          if (task) {
            state_ = State::BUSY;
            task->Run();
            completed_task_count_ += 1;
          }
          else {
            state_ = State::EXITED;
            return;
          }
        }
      }));
      t_id_ = t_.get_id();
    }
    void Work() {
      if (t_.joinable()) {
        t_.join();
      }
    }
    std::thread::id getThreadId() { return t_id_; }
    State state() const { return state_; }
    uint64_t completed_task_count() const { return completed_task_count_; }

  private:
    std::thread t_;
    State state_;
    uint64_t completed_task_count_;
    std::thread::id t_id_;
  };

  class ClassifyThreadPool {
  public:
    ClassifyThreadPool(const char* name, uint16_t capacity, int wait_for_task_seconds)
      : name_(name), capacity_(capacity), wait_for_task_seconds_(wait_for_task_seconds) {
      workers_.reserve(capacity);
      ConnectTaskPriorityQueue();
    }

    ClassifyThreadPool(ClassifyThreadPool&&) = delete;
    ClassifyThreadPool(const ClassifyThreadPool&) = delete;
    ClassifyThreadPool& operator=(ClassifyThreadPool&&) = delete;
    ClassifyThreadPool& operator=(const ClassifyThreadPool&) = delete;

    void InitWorkers(uint16_t count) {
      for (unsigned char i = 0; i < count && workers_.size() < capacity_; ++i) {
        AddWorker();
      }
    }
    const char* name() const { return name_.c_str(); }
    uint16_t capacity() const { return capacity_; }
    uint16_t WorkerCount() const { return workers_.size(); }
    uint16_t IdleWorkerCount() const {
      return GetWorkerStateCount(Worker::State::IDLE);
    }
    uint16_t BusyWorkerCount() const {
      return GetWorkerStateCount(Worker::State::BUSY);
    }
    uint16_t ExitedWorkerCount() const {
      return GetWorkerStateCount(Worker::State::EXITED);
    }
    const std::vector<std::shared_ptr<Worker> >& workers() const { return workers_; }
    const std::unique_ptr<TaskPriorityQueue<CMPTaskBypriority>>& task_queue() const { return task_queue_; }

    std::thread::id getThreadId() { 
      if (workers_.empty()) {
        return std::this_thread::get_id();
      }
      return workers_.at(0)->getThreadId();
    }

  private:
    friend class SmartThreadPool;
    void ConnectTaskPriorityQueue() {
      std::string queue_name = name_ + "-->TaskQueue";
      task_queue_ = std::unique_ptr<TaskPriorityQueue<CMPTaskBypriority>>{new TaskPriorityQueue<CMPTaskBypriority>(queue_name.c_str(), wait_for_task_seconds_)};
    }
    void AddWorker() {
      workers_.emplace_back(std::shared_ptr<Worker>(new Worker(task_queue_.get())));
    }
    void StartWorkers() {
      for (auto& worker : workers_) {
        worker->Work();
      }
    }
    uint16_t GetWorkerStateCount(Worker::State state) const {
      uint16_t count = 0;
      for (auto& worker : workers_) {
        if (worker->state() == state) {
          count += 1;
        }
      }
      return count;
    }
    
    std::string name_;
    uint16_t capacity_;
    std::vector<std::shared_ptr<Worker> > workers_;
    std::unique_ptr<TaskPriorityQueue<CMPTaskBypriority>> task_queue_;
    int wait_for_task_seconds_;
  };
  

  class SmartThreadPool {
    class SheduleWorker {
    public:
      SheduleWorker(SheduleWorker&&) = delete;
      SheduleWorker(const SheduleWorker&) = delete;
      enum State : unsigned char {
        IDLE = 0,
        BUSY,
        EXITED,
      };
      explicit SheduleWorker()
        : state_(State::IDLE), completed_task_count_(0), task_queue_("shedulependingQueue", 0)
      {
        terminated_ = false;
        t_ = std::move(std::thread([this]() {
          while (!terminated_.load()) {
            auto next_time_point(compute_next_wait_until_time());
            // If there is a task in the queue, we call WaitUntil from the semaphore
            // This lets this thread to sleep up to next_wait_time before either 
            // woken up by a new task, or when the top task is ready to run
            if (next_time_point.first) {
              semaphore_.WaitUntil(next_time_point.second);
            }
            else {
              semaphore_.Wait();
            }
            // After waking up, dispatch as many as possible
            dispatch();
          }
        }));
      }
      void Work() {
        if (t_.joinable()) {
          t_.join();
        }
      }
      State state() const { return state_; }
      uint64_t completed_task_count() const { return completed_task_count_; }

      template<class T_duration,  class F, class... Args>
      auto ApplyDelayAsync(const char* pool_name, TaskPriority priority, T_duration delay_duration,
        F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        auto res = task_queue_.enqueue(priority, pool_name, delay_duration, f, args...);
        semaphore_.Notify();
        return res;
      }


      // Helper function to compute the next time period to wait before waking up
      // again. If the first field is false, it means there is currently no task
      // in the queue, otherwise, the second field represents the time duration
      // between now and the task's start time on top of the queue
      std::pair<bool, std::chrono::high_resolution_clock::time_point>
        compute_next_wait_until_time(){
        // Lock the mutex and return the start_time
        std::lock_guard<std::mutex> lock(mutex_);
        if (!task_queue_.empty()) {
          return std::make_pair(true, task_queue_.topTaskStartTime());
        }
        return std::make_pair(false, std::chrono::high_resolution_clock::time_point());
      }

      // Helper function to dispatch the tasks on top of the queue to the 
      // threadpool as much as possible, as long as the tasks' start_time is 
      // before now
      void dispatch(){
        // Lock the mutex and keep popping the task on top of the task queue until 
        // the start_time is after now
        std::lock_guard<std::mutex> lock(mutex_);
        while (!task_queue_.empty() && task_queue_.topTaskStartTime() <= now()) {
          // There is no easy way to move the top item out of the priority queue's 
          // top element without performing the following const cast. This is mainly
          // because top() returns a const T_duration&, which cannot bind to T_duration&&. 

          auto task = task_queue_.dequeue();
          pool_->ApplyAsync(task->pool_name().c_str(), task);
          //task->pool_name_, task->priority
          //worker_thread_pool_.Submit(std::move(task.function_wrapper_));
        }
      }

      // Just an alias of computing now timepoint
      std::chrono::high_resolution_clock::time_point now() const {
        return std::chrono::high_resolution_clock::now();
      }

      // A mutex to protect the whole delay queue
      std::mutex mutex_;
      // A semaphore used by the delay queue to synchronize task insertion
      Semaphore semaphore_;

      // A flag to indiate whether the delay queue has been terminated
      std::atomic<bool> terminated_;

    private:
      friend class SmartThreadPoolBuilder;
      friend class Monitor;
      std::thread t_;
      State state_;
      uint64_t completed_task_count_;
      TaskPriorityQueue<CMPTaskByStartTime> task_queue_;
      std::shared_ptr<SmartThreadPool> pool_;
    };

  public:    
    SmartThreadPool(SmartThreadPool&&) = delete;
    SmartThreadPool(const SmartThreadPool&) = delete;
    SmartThreadPool& operator=(SmartThreadPool&&) = delete;
    SmartThreadPool& operator=(const SmartThreadPool&) = delete;

    template<class F, class... Args>
    auto ApplyAsync(const char* pool_name, TaskPriority priority,
      F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
      auto& pool = pools_.at(pool_name);
      auto res = pool->task_queue()->enqueue(priority, "", std::chrono::milliseconds(0), f, args...);
      if (pool->task_queue()->size() >= pool->WorkerCount()
        && pool->WorkerCount() < pool->capacity()) {
        pool->AddWorker();
      }
      return res;
    }

    void ApplyAsync(std::string pool_name, std::unique_ptr<Task>& task) {
      auto& pool = pools_.at(pool_name);
      pool->task_queue()->enqueue(task);
    }

    /*
      class T_duration : is std::chrono::duration
      
    */
    template<class T_duration, class F, class... Args>
    auto ApplyDelayAsync(const char* pool_name, TaskPriority priority, T_duration delay_duration,
      F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
      return sheduleWorker_.ApplyDelayAsync(pool_name, priority, delay_duration, f, args...);
    }

    void StartAllWorkers(){
      for (auto&& pool : pools_) {
        pool.second->StartWorkers();
      }
    }

    std::string GetThreadIdOfString(const std::thread::id & id)
    {
        std::stringstream sin;
        sin << id;
        return sin.str();
    }

    bool IsInThread(std::string pool_name) {
      auto& pool = pools_.at(pool_name);
      if (pool) {
        return std::this_thread::get_id() == pool->getThreadId();
      }
      return false;
    }

  private:
    friend class SmartThreadPoolBuilder;
    friend class Monitor;
    SmartThreadPool(){}
    std::map<std::string, std::unique_ptr<ClassifyThreadPool> > pools_;
    std::map<std::string, std::thread::id > idMap_;
    SheduleWorker sheduleWorker_;
  };

  class Monitor {
  public:
    void StartMonitoring(const SmartThreadPool& pool, const std::chrono::duration<int>& monitor_second_period) {
      t_ = std::move(std::thread([&pool, &monitor_second_period, this](){
        while (true) {
          std::this_thread::sleep_for(monitor_second_period);
          for (auto&& pool_map : pool.pools_) {
            auto& classify_pool = *pool_map.second.get();
            MonitorClassifyPool(classify_pool);
          }

          char now[128];
          std::time_t t = std::time(NULL);
          std::strftime(now, sizeof(now), "%A %c", std::localtime(&t));
          std::string now_str(now);

          std::stringstream monitor_log;
          auto cmp = [](const std::string& s1, const std::string& s2) { return s1.size() < s2.size(); };
          size_t max_row_msg_length = 0;

          for (size_t i = 0; i < pool_msgs_.size(); ++i) {
            int max_pool_msg_length = std::max_element(pool_msgs_.begin(), pool_msgs_.end(), cmp)->length();
            int max_workers_msg_length = std::max_element(workers_msgs_.begin(), workers_msgs_.end(), cmp)->length();
            max_pool_msg_length += 2;
            max_workers_msg_length += 2;
            std::stringstream row_log;
            row_log << std::left << std::setw(max_pool_msg_length) << pool_msgs_.at(i)
              << std::left << std::setw(max_workers_msg_length) << workers_msgs_.at(i)
              << std::left << tasks_msgs_.at(i) << std::endl;
            if (row_log.str().length() > max_row_msg_length) {
              max_row_msg_length = row_log.str().length();
            }
            monitor_log << row_log.str();
          }

          int head_front_length = (max_row_msg_length - now_str.length()) / 2;
          int head_back_length = max_row_msg_length - now_str.length() - head_front_length;
          std::stringstream pretty_msg;
          pretty_msg << "/" << std::setfill('-') << std::setw(head_front_length)
            << "" << now << std::setfill('-') << std::setw(head_back_length - 1)
            << "\\" << std::endl
            << monitor_log.str()
            << "\\" << std::setfill('-') << std::setw(max_row_msg_length - 1)
            << "/" << std::endl;
          std::cout << pretty_msg.str();
          pool_msgs_.clear();
          workers_msgs_.clear();
          tasks_msgs_.clear();
        }
      }));

      t_.detach();
    }

  private:
    friend class SmartThreadPoolBuilder;
    Monitor() {}
    void MonitorClassifyPool(const ClassifyThreadPool& classify_pool) {
      uint16_t busy_worker = classify_pool.BusyWorkerCount();
      uint16_t idle_worker = classify_pool.IdleWorkerCount();
      uint16_t exited_worker = classify_pool.ExitedWorkerCount();
      uint16_t total_worker = classify_pool.capacity();
      uint16_t assignable_worker = total_worker - classify_pool.WorkerCount();

      uint64_t total_task = classify_pool.task_queue()->task_count();
      uint64_t running_task = classify_pool.BusyWorkerCount();
      uint64_t pending_task = classify_pool.task_queue()->pending_task_count();
      uint64_t completed_task = total_task - running_task - pending_task;

      char pool_msg[64];
      char workers_msg[128];
      char tasks_msg[128];
      snprintf(pool_msg, sizeof(pool_msg), " ~ ThreadPool:%s", classify_pool.name());
      snprintf(workers_msg, sizeof(workers_msg), "Workers[Busy:%u, Idle:%u, Exited:%u, Assignable:%u, Total:%u]",
        busy_worker, idle_worker, exited_worker, assignable_worker, total_worker);
      snprintf(tasks_msg, sizeof(tasks_msg), "Tasks[Running:%lu, Waiting:%lu, Completed:%lu, Total:%lu]",
        running_task, pending_task, completed_task, total_task);

      pool_msgs_.emplace_back(pool_msg);
      workers_msgs_.emplace_back(workers_msg);
      tasks_msgs_.emplace_back(tasks_msg);
    }

    std::thread t_;
    std::vector<std::string> pool_msgs_;
    std::vector<std::string> workers_msgs_;
    std::vector<std::string> tasks_msgs_;
  };

  class SmartThreadPoolBuilder {
  public:
    SmartThreadPoolBuilder()
      : smart_pool_(new SmartThreadPool), enable_monitor_(false) {
    }

    SmartThreadPoolBuilder(SmartThreadPoolBuilder&&) = delete;
    SmartThreadPoolBuilder(const SmartThreadPoolBuilder&) = delete;
    SmartThreadPoolBuilder& operator=(SmartThreadPoolBuilder&&) = delete;
    SmartThreadPoolBuilder& operator=(const SmartThreadPoolBuilder&) = delete;

    SmartThreadPoolBuilder& AddClassifyPool(const char* pool_name, uint8_t capacity, uint8_t init_size, int wait_for_task_seconds = 0) {
      auto pool = new ClassifyThreadPool(pool_name, capacity, wait_for_task_seconds);
      pool->InitWorkers(init_size);
      smart_pool_->pools_.emplace(pool_name, std::unique_ptr<ClassifyThreadPool>(pool));  
      smart_pool_->idMap_.emplace(pool_name, pool->getThreadId());
      return *this;
    }
    SmartThreadPoolBuilder& EnableMonitor(const std::chrono::duration<int>& second_period = std::chrono::seconds(60)) {
      enable_monitor_ = true;
      monitor_second_period_ = second_period;
      return *this;
    }
    std::shared_ptr<SmartThreadPool> BuildAndInit() {
      if (enable_monitor_) {
        auto monitor = new Monitor();
        monitor->StartMonitoring(*smart_pool_.get(), monitor_second_period_);
      }
      smart_pool_->sheduleWorker_.pool_ = smart_pool_;
      return smart_pool_;
    }

  private:
    std::shared_ptr<SmartThreadPool> smart_pool_;
    bool enable_monitor_;
    std::chrono::duration<int> monitor_second_period_;
  };

}   // namespace stp
#endif  // SMART_THREAD_POOL_H_

/*
#include "smart_thread_pool.h"
using stp::SmartThreadPoolBuilder;
using stp::TaskPriority;
#define SmartThreadUIFlag "SmartThreadOfUI"
#define SmartThreadWorkerFlag "SmartThreadOfWorker"

#include <iostream>
#include <mutex>
#include <condition_variable>

std::shared_ptr<stp::SmartThreadPool> smartThreadPool; 

struct Viewer: public std::enable_shared_from_this<Viewer> {
  Viewer( std::string f){
    flag = f;
  }
  std::string flag;
  void onUserInputToSum(int from, int to);
  void updateUI(std::string str) {
    std::cout << str << std::endl;
  }
};

struct Controler: public std::enable_shared_from_this<Controler> {
  Controler( std::string f){
    flag = f;
  }
  std::string flag;
  void onSum(int from, int to);
};

struct Module: public std::enable_shared_from_this<Module> {
  Module( std::string f){
    flag = f;
  }
  std::string flag;
  void doSum(int from, int to);
};

std::shared_ptr<Viewer> getViewer(){
  static std::shared_ptr<Viewer> mgr(new Viewer(""));
  return mgr;
}
std::shared_ptr<Controler> getControler(){
  static std::shared_ptr<Controler> mgr(new Controler(""));
  return mgr;
}
std::shared_ptr<Module> getModule(){
  static std::shared_ptr<Module> mgr(new Module(""));
  return mgr;
}

void Viewer::onUserInputToSum(int from, int to) {
  smartThreadPool->ApplyAsync(SmartThreadUIFlag, TaskPriority::MEDIUM, std::bind(&Controler::onSum, getControler(), from, to));
}
void Controler::onSum(int from, int to){
  smartThreadPool->ApplyAsync(SmartThreadWorkerFlag, TaskPriority::MEDIUM, std::bind(&Module::doSum, getModule(), from, to));
  std::stringstream out;
  out << "sum from:" << from << " to:" << to << " 计算中，请稍等";
  smartThreadPool->ApplyAsync(SmartThreadUIFlag, TaskPriority::MEDIUM, std::bind(&Viewer::updateUI, getViewer(), out.str()));
}
void Module::doSum(int from, int to){
  std::this_thread::sleep_for(std::chrono::seconds(3));
  int sum = 0;
  for(int i = from; i <to; i++) {
    sum += i;
  } 
  std::stringstream out;
  out << "sum from:" << from << " to:" << to << " is: " << sum;
  smartThreadPool->ApplyAsync(SmartThreadUIFlag, TaskPriority::MEDIUM, std::bind(&Viewer::updateUI, getViewer(), out.str()));
}

void testMVC() {
  int from = std::rand() % 100;
  int to = std::rand() % 100;
  if (from > to) {
    int tmp = to; to = from; from = tmp;
  }
  smartThreadPool->ApplyAsync(SmartThreadUIFlag, TaskPriority::MEDIUM, std::bind(&Viewer::onUserInputToSum, getViewer(), from, to));
  smartThreadPool->ApplyDelayAsync(SmartThreadWorkerFlag, TaskPriority::MEDIUM,  std::chrono::seconds(6), testMVC);
}

int main(int argc, char** argv) {
  SmartThreadPoolBuilder builder;
  builder.AddClassifyPool(SmartThreadUIFlag, 1, 1)
    .AddClassifyPool(SmartThreadWorkerFlag, 1, 1);
  smartThreadPool = builder.BuildAndInit();

  //mvc使用线程池模拟，尽量不要等待函数的执行结果，而是有结果时异步通知。
  smartThreadPool->ApplyDelayAsync(SmartThreadWorkerFlag, TaskPriority::MEDIUM,  std::chrono::seconds(1), testMVC);

  smartThreadPool->StartAllWorkers();
  //任务池没有任务时不会主动退出，ctrl + c 退出
}
*/


/* example:
#include "smart_thread_pool.h"
using stp::SmartThreadPoolBuilder;
using stp::TaskPriority;

#define SmartThreadUIFlag "SmartThreadOfUI"
#define SmartThreadWorkerFlag "SmartThreadOfWorker"
#define SmartThreadCPUFlag "SmartThreadOfCPU"
#define SmartThreadIOFlag "SmartThreadOfIO"

#include <iostream>
#include <mutex>
#include <condition_variable>

std::shared_ptr<stp::SmartThreadPool> smartThreadPool; 


double my_divide(double x, double y) { 
  printf("my_divide\n");
  return x / y; }

struct MyPair {
  ~MyPair(){
    printf("MyPair destory\n");  
  }
  double a, b;
  double multiply() { 
    printf("MyPair::multiply | \n"); 
    return a*b;
  }
  double divide(double c, double d) { 
    printf("MyPair::divide | \n");
    return c/d; }
};

struct SharedMyPair: public std::enable_shared_from_this<SharedMyPair> {
  ~SharedMyPair(){
    printf("SharedMyPair destory\n");
  }
  SharedMyPair(double ap, double bp, std::string f){
    a = ap;
    b = bp;
    flag = f;
    start_time_ = std::chrono::high_resolution_clock::now();
  }
  std::string flag;
  std::chrono::high_resolution_clock::time_point start_time_;
  double a, b;
  
  double multiply() { 
    printf("SharedMyPair::multiply | \n");
    return a*b; 
  }
  
  double divideJustWorkInWorkerThread(double c, double d) {
    printf("SharedMyPair::divideJustWorkInWorkerThread | flag:%s, Is In SmartThreadWorkerFlag Thread:%d\n", flag.c_str(), smartThreadPool->IsInThread(SmartThreadWorkerFlag));
    return c / d; 
  }
  
  double divideDelay(double c, double d) { 
    std::chrono::duration<double, std::milli> ms = std::chrono::high_resolution_clock::now() - start_time_;
    printf("SharedMyPair::divideDelay | flag:%s, acture time delay:%f, Is In SmartThreadCPUFlag Thread:%d\n", flag.c_str(), ms.count(), smartThreadPool->IsInThread(SmartThreadCPUFlag));
    return c / d; 
  }


  double divideJustWorkInWorkerThreadWrapper(double c, double d) {
    double ans = 0;
    if (smartThreadPool->IsInThread(SmartThreadWorkerFlag)){
      printf("SharedMyPair::divideJustWorkInWorkerThreadWrapper | flag:%s, Is In SmartThreadWorkerFlag Thread\n", flag.c_str());
      ans = divideJustWorkInWorkerThread(c,d);
    }else {
      printf("SharedMyPair::divideJustWorkInWorkerThreadWrapper | flag:%s, this_threadid:%s, not Is In SmartThreadWorkerFlag Thread, post to SmartThreadWorkerFlag\n", flag.c_str(), smartThreadPool->GetThreadIdOfString(std::this_thread::get_id()).c_str());
      auto res = smartThreadPool->ApplyAsync(SmartThreadWorkerFlag, TaskPriority::MEDIUM, std::bind(&SharedMyPair::divideJustWorkInWorkerThread, shared_from_this(),c,d));
      ans = res.get();
    }
    return ans;
  }
};


int main(int argc, char** argv) {
  // ********************How to init `SmartThreadPool`********************
  //
  // using stp::SmartThreadPool;
  // using stp::SmartThreadPoolBuilder;
  // SmartThreadPoolBuilder builder;

  // ********Build by calling a chain.********
  // builder.AddClassifyPool(const char* pool_name,
  //                         uint8_t capacity,
  //                         uint8_t init_size);
  // ******** Such as:
  // builder.AddClassifyPool(SmartThreadUIFlag, 16, 4)
  //        .AddClassifyPool(SmartThreadCPUFlag, 8, 4)
  //        .AddClassifyPool(SmartThreadIOFlag, 16, 8)
  // auto smartThreadPool = builder.BuildAndInit();  // will block current thread
  //
  // ***********************************************************************

  // ******************************How to join a task******************************
  //
  // smartThreadPool->ApplyAsync(function, args...);
  // ******** Such as:
  // 1. Run a return careless task.
  // smartThreadPool->ApplyAsync(SmartThreadIOFlag, TaskPriority::MEDIUM, [](){ //DoSomeThing(args...); }, arg1, arg2, ...);
  //
  // 2. Run a return careful task.
  // auto res = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::HIGH, [](int count){ return count; }, 666);
  // auto value = res.get();
  //
  // or you can set a timeout duration to wait for the result to become available.
  //
  // std::future_status status = res.wait_for(std::chrono::seconds(1)); // wait for 1 second.
  // if (status == std::future_status::ready) {
  //   std::cout << "Result is: " << res.get() << std::endl;
  // } else {
  //   std::cout << "Timeout" << std::endl;
  // }
  //
  // *******************************************************************************


  SmartThreadPoolBuilder builder;
  builder.AddClassifyPool(SmartThreadCPUFlag, 1, 1)
    .AddClassifyPool(SmartThreadIOFlag, 64, 32)
    .AddClassifyPool(SmartThreadUIFlag, 1, 1)
    .AddClassifyPool(SmartThreadWorkerFlag, 1, 1);
    //.EnableMonitor(std::chrono::seconds(5));
  smartThreadPool = builder.BuildAndInit();

  for (int i = 0; i < 2; ++i) {
    for (unsigned char j = 0; j < 5; ++j) {
      smartThreadPool->ApplyAsync(SmartThreadIOFlag, static_cast<TaskPriority>(j), [](int priority,  unsigned char i) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        printf("IOBoundPool priority:%d, %d\n", priority, i);
      },j, i);
    }
  }

  smartThreadPool->ApplyAsync(SmartThreadIOFlag, TaskPriority::HIGH, [](){
    int repeat_times = 5;
    while (--repeat_times >= 0) {
      printf("IOBoundPool HIGH Task\n"); 
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  });

  {
    //添加lambda函数OK
    auto res = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, [](int x, int y){
      return x + y;
    }, 1, 2);
    printf("added result: %d\n", res.get());
  }

  {
    //添加使用bind封装的无参函数OK
    auto res = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::bind(my_divide, 1, 2));
    //添加函数指针函数OK
    auto res1 = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, my_divide, 1, 2);
    printf("my_divide result: %f\n", res.get());
  }

  {
    MyPair mypair{ 1, 2 };
    //无shared_ptr保护的变量，要等待，否则在执行时，mypair可能已被删除，从而会有野崩溃
    auto res = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::bind(&MyPair::multiply, mypair));
    printf("multiply result: %f\n", res.get());
    auto res1 = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::bind(&MyPair::divide, mypair, 1, 2));
    printf("divide result: %f\n", res1.get());
  }

  { 
    //添加延迟执行函数OK！！!从日志是看，延迟1000ms执行的任务，可以会在[1000，1005]ms单被执行，即定时任务会被延迟执行
    std::shared_ptr<SharedMyPair> shard_pair1(new SharedMyPair(1, 2, "sleep 10000"));
    auto res = smartThreadPool->ApplyDelayAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::chrono::milliseconds(10000), std::mem_fn(&SharedMyPair::divideDelay), shard_pair1, 1, 2);
    std::shared_ptr<SharedMyPair> shard_pair2(new SharedMyPair(1, 2, "sleep 6000"));
    auto res1 = smartThreadPool->ApplyDelayAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::chrono::milliseconds(6000), std::bind(&SharedMyPair::divideDelay, shard_pair2, 1, 2));
    //添加延迟执行bind + placeholders函数OK！！！
    std::shared_ptr<SharedMyPair> shard_pair3(new SharedMyPair(1, 2, "sleep 1000"));
    auto res2 = smartThreadPool->ApplyDelayAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::chrono::milliseconds(1000), std::bind(&SharedMyPair::divideDelay, shard_pair3, 1, std::placeholders::_1), 2);
    // printf("shard_pair3 divide result: %f\n", res2.get());
    // printf("shard_pair2 divide result: %f\n", res1.get());
    // printf("shard_pair1 divide result: %f\n", res.get());
  }

  {
    //类chrome线程模型，可以实现让函数在特定线程（如UI线程，工作线程）中执行
    std::shared_ptr<SharedMyPair> shard_pair(new SharedMyPair(1, 2, "call from main thread"));
    auto ans = shard_pair->divideJustWorkInWorkerThreadWrapper(1,2);
    printf("main thread call divideJustWorkInWorkerThreadWrapper ans1 : %f\n", ans);
    std::shared_ptr<SharedMyPair> shard_pair1(new SharedMyPair(1, 2, "call from SmartThreadUIFlag"));
    auto res = smartThreadPool->ApplyAsync(SmartThreadUIFlag, TaskPriority::MEDIUM, std::bind(&SharedMyPair::divideJustWorkInWorkerThreadWrapper, shard_pair1, 1, std::placeholders::_1), 2);
    printf("SmartThreadUIFlag thread divideJustWorkInWorkerThreadWrapper ans : %f\n", res.get());
    std::shared_ptr<SharedMyPair> shard_pair2(new SharedMyPair(1, 2, "call from SmartThreadWorkerFlag"));
    auto res1 = smartThreadPool->ApplyAsync(SmartThreadWorkerFlag, TaskPriority::MEDIUM, std::bind(&SharedMyPair::divideJustWorkInWorkerThreadWrapper, shard_pair2, 1, std::placeholders::_1), 2);
    printf("SmartThreadWorkerFlag thread divideJustWorkInWorkerThreadWrapper ans : %f\n", res1.get());
  }

  // {
  //   //添加shared_ptr对象OK
  //   std::shared_ptr<SharedMyPair> shard_pair(new SharedMyPair(1, 2, ""));
  //   auto res = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::bind(&SharedMyPair::divide, shard_pair, 1, 2));
  //   //添加mem_fn封装的类成员函数OK
  //   auto res1 = smartThreadPool->ApplyAsync(SmartThreadCPUFlag, TaskPriority::MEDIUM, std::mem_fn(&SharedMyPair::divide), shard_pair, 1, 2);
  //   printf("shard_pair divide result: %f\n", res.get());
  // }


  smartThreadPool->StartAllWorkers();
  //任务池没有任务时不会主动退出，ctrl + c 退出
}
*/
