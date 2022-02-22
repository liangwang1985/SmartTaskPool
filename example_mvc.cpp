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