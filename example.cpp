/****************************************************************************\
 * Created on Mon Jul 30 2018
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