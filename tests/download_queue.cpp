#include "download_queue.hpp"
#include <cassert>
#include <deque>
#include <filesystem>
#include <iterator>
int main(){
  const std::string dir="/tmp/hongguo-download-test";std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);
  std::deque<std::function<void()>> tasks;auto scheduler=[&](std::function<void()> task){tasks.push_back(std::move(task));return true;};
  int finished=0,calls=0;auto reader=[&](const std::string&,int64_t offset){calls++;return DownloadChunk{6,std::vector<unsigned char>(3,(unsigned char)('a'+offset))};};
  DownloadQueue queue(dir,reader,scheduler,[&](const DownloadStatus& s){finished++;assert(s.received==6);});
  auto drain=[&]{int n=0;while(!tasks.empty()){auto task=std::move(tasks.front());tasks.pop_front();task();assert(++n<100);}};
  assert(!queue.start("../bad","bad"));assert(queue.start("123","title"));assert(!queue.start("123","duplicate"));drain();assert(finished==1&&calls==2);
  std::ifstream file(dir+"/123.mp4",std::ios::binary);std::string text((std::istreambuf_iterator<char>(file)),{});assert(text=="aaaddd");assert(!std::filesystem::exists(dir+"/123.mp4.part"));
  assert(queue.start("456","cancel"));queue.cancel("456");drain();assert(queue.snapshot().back().status=="已取消");assert(!std::filesystem::exists(dir+"/456.mp4.part"));
  int errors=0;DownloadQueue broken(dir,[&](const std::string&,int64_t){errors++;return DownloadChunk{2,{1,2,3}};},scheduler,[](auto){assert(false);});assert(broken.start("789","bad range"));drain();assert(errors==3);assert(!std::filesystem::exists(dir+"/789.mp4"));
  std::filesystem::remove_all(dir);
}
