// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdio>
struct DownloadChunk{int64_t total;std::vector<unsigned char> bytes;};
struct DownloadStatus{std::string id,title,path,status;int64_t received=0,total=0;std::string seriesId;int number=1;};
class DownloadQueue{
  struct Item{DownloadStatus state;std::atomic<bool> cancelled{false};int failures=0;};
  std::mutex mutex;std::vector<std::shared_ptr<Item>> items;
  std::function<DownloadChunk(const std::string&,int64_t)> reader;
  std::function<bool(std::function<void()>)> scheduler;
  std::function<void(const DownloadStatus&)> completed;
  std::string directory;
  void step(std::shared_ptr<Item> item){
    if(item->cancelled){std::remove((item->state.path+".part").c_str());std::lock_guard<std::mutex> guard(mutex);item->state.status="已取消";return;}
    int64_t offset;{std::lock_guard<std::mutex> guard(mutex);offset=item->state.received;}
    try{
      auto chunk=reader(item->state.id,offset);
      if(item->cancelled){schedule(item);return;}
      if(chunk.total<=0||chunk.total>2147483647||chunk.bytes.empty()||chunk.bytes.size()>1048576||offset+(int64_t)chunk.bytes.size()>chunk.total)throw std::runtime_error("Invalid download range");
      {std::lock_guard<std::mutex> guard(mutex);if(item->state.total&&item->state.total!=chunk.total)throw std::runtime_error("Changed download");}
      {std::fstream out(item->state.path+".part",std::ios::binary|std::ios::in|std::ios::out);out.seekp(offset);if(!out||!out.write((const char*)chunk.bytes.data(),chunk.bytes.size()))throw std::runtime_error("SD write");out.flush();if(!out)throw std::runtime_error("SD flush");}
      DownloadStatus done;bool finished;
      {std::lock_guard<std::mutex> guard(mutex);item->failures=0;item->state.total=chunk.total;item->state.received+=chunk.bytes.size();finished=item->state.received==chunk.total;item->state.status="下载中";done=item->state;}
      if(finished){if(std::rename((done.path+".part").c_str(),done.path.c_str())!=0)throw std::runtime_error("SD rename");{std::lock_guard<std::mutex> guard(mutex);item->state.status="已完成";done=item->state;}completed(done);}
      else schedule(item);
    }catch(...){bool retry;{std::lock_guard<std::mutex> guard(mutex);if(item->state.total>0&&item->state.received==item->state.total)item->state.received=offset;retry=++item->failures<=2;item->state.status=retry?"正在重试":"下载失败，请检查网络或 SD 卡";}if(retry)schedule(item);}
  }
  void schedule(std::shared_ptr<Item> item){if(!scheduler([this,item]{step(item);})){std::lock_guard<std::mutex> guard(mutex);item->state.status="任务已停止";}}
public:
  DownloadQueue(std::string directory,std::function<DownloadChunk(const std::string&,int64_t)> reader,std::function<bool(std::function<void()>)> scheduler,std::function<void(const DownloadStatus&)> completed):reader(std::move(reader)),scheduler(std::move(scheduler)),completed(std::move(completed)),directory(std::move(directory)){}
  bool start(const std::string& id,const std::string& title,const std::string& seriesId="",int number=1){
    if(id.empty()||id.size()>30||id.find_first_not_of("0123456789")!=std::string::npos)return false;
    auto item=std::make_shared<Item>();item->state={id,title,directory+"/"+id+".mp4","等待下载",0,0,seriesId,number};
    {std::lock_guard<std::mutex> guard(mutex);for(const auto& old:items)if(old->state.id==id&&(old->state.status=="下载中"||old->state.status=="等待下载"||old->state.status=="正在重试"||old->state.status=="已完成"))return false;std::ofstream out(item->state.path+".part",std::ios::binary|std::ios::trunc);if(!out)return false;items.push_back(item);}
    schedule(item);return true;
  }
  void cancel(const std::string& id){std::lock_guard<std::mutex> guard(mutex);for(auto& item:items)if(item->state.id==id&&item->state.status!="已完成")item->cancelled=true;}
  std::vector<DownloadStatus> snapshot(){std::lock_guard<std::mutex> guard(mutex);std::vector<DownloadStatus> result;for(const auto& item:items)result.push_back(item->state);return result;}
};
