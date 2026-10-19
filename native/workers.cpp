// SPDX-License-Identifier: GPL-3.0-only
#include "workers.hpp"
#include <switch.h>
#include <pthread.h>
#include <deque>
#include <mutex>
#include <cstdio>
#include <exception>
namespace {
thread_local bool backgroundWork=false;
std::mutex logMutex;
constexpr const char* logPath="sdmc:/switch/hongguo-saf/runtime.log";
struct Worker {
  pthread_t thread{};std::mutex mutex;std::deque<std::function<void()>> tasks,background;
  bool running=false,created=false;const char* name;
  explicit Worker(const char* name):name(name){}
  static void* run(void* context){auto& worker=*static_cast<Worker*>(context);
    for(;;){std::function<void()> task;{std::lock_guard<std::mutex> guard(worker.mutex);if(!worker.running&&worker.tasks.empty())break;backgroundWork=false;if(!worker.tasks.empty()){task=std::move(worker.tasks.front());worker.tasks.pop_front();}else if(!worker.background.empty()){task=std::move(worker.background.front());worker.background.pop_front();backgroundWork=true;}}
      if(task){try{task();}catch(const std::exception&){trace(std::string(worker.name)+":task_exception");}catch(...){trace(std::string(worker.name)+":unknown_exception");}}else svcSleepThread(10000000);backgroundWork=false;
    }return nullptr;
  }
  bool start(){pthread_attr_t attributes;if(pthread_attr_init(&attributes)!=0)return false;
    // TLS, curl, QuickJS and image decoders share this bounded C stack.
    int result=pthread_attr_setstacksize(&attributes,2*1024*1024);
    running=true;if(!result)result=pthread_create(&thread,&attributes,run,this);pthread_attr_destroy(&attributes);
    created=result==0;if(!created)running=false;trace(std::string(name)+(created?":started:stack_2MiB":":start_failed"));return created;
  }
  bool submit(std::function<void()> task,bool low=false){std::lock_guard<std::mutex> guard(mutex);if(!running)return false;(low?background:tasks).push_back(std::move(task));return true;}
  bool pending(){std::lock_guard<std::mutex> guard(mutex);return !running||!tasks.empty();}
  void stop(){if(!created)return;{std::lock_guard<std::mutex> guard(mutex);running=false;background.clear();}pthread_join(thread,nullptr);created=false;}
};
Worker api("api_worker"),poster("poster_worker");
}
void resetTrace(){std::lock_guard<std::mutex> guard(logMutex);if(auto* f=std::fopen(logPath,"w")){std::fputs("hongguo-switch 1.1.3\n",f);std::fclose(f);}}
void trace(const std::string& stage){std::lock_guard<std::mutex> guard(logMutex);if(auto* f=std::fopen(logPath,"a")){std::fprintf(f,"%llu %s\n",(unsigned long long)armGetSystemTick(),stage.c_str());std::fclose(f);}}
bool startWorkers(){if(!api.start())return false;if(!poster.start()){api.stop();return false;}return true;}
void stopWorkers(){poster.stop();api.stop();}
bool apiTask(std::function<void()> task){return api.submit(std::move(task));}
bool posterTask(std::function<void()> task){return poster.submit(std::move(task));}
bool inApiWorker(){return api.created&&pthread_equal(pthread_self(),api.thread);}

bool apiBackgroundTask(std::function<void()> task){return api.submit(std::move(task),true);}
bool inApiBackground(){return inApiWorker()&&backgroundWork;}
bool apiForegroundPending(){return api.pending();}
