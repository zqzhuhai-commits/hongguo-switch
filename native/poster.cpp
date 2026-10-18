// SPDX-License-Identifier: GPL-3.0-only
#include "poster.hpp"
#include "workers.hpp"
#include <curl/curl.h>
#include <borealis/extern/nanovg/stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
namespace {
struct Download { std::vector<unsigned char> bytes; const std::atomic<bool>* alive; };
size_t receive(char* data,size_t size,size_t count,void* context) {
  auto& d=*static_cast<Download*>(context);const size_t n=size*count;
  if(!d.alive->load() || n>2*1024*1024 || d.bytes.size()>2*1024*1024-n)return 0;
  d.bytes.insert(d.bytes.end(),data,data+n);return n;
}
}
PosterPixels fetchPoster(const std::string& address,const std::atomic<bool>& alive) {
#ifdef HONGGUO_PREVIEW
  if(address.rfind("preview:",0)==0){PosterPixels image;const int index=std::stoi(address.substr(8));image.rgba.resize(image.width*image.height*4);for(int y=0;y<image.height;y++)for(int x=0;x<image.width;x++){const size_t at=(y*image.width+x)*4;image.rgba[at]=45+index*19+(y*35/image.height);image.rgba[at+1]=48+index*10;image.rgba[at+2]=85+index*12+(x*25/image.width);image.rgba[at+3]=255;}return image;}
#endif
  std::string url=address;if(url.rfind("//",0)==0)url="https:"+url;else if(url.rfind("http://",0)==0)url="https://"+url.substr(7);
  PosterPixels result;
  if(url.rfind("https://",0)!=0 || url.size()>8192 || !alive.load()){trace("poster:invalid_or_cancelled");return result;}trace("poster:download_begin");
  std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl(curl_easy_init(),curl_easy_cleanup);
  if(!curl)return result;
  Download d{{},&alive};auto* h=curl.get();
  curl_easy_setopt(h,CURLOPT_URL,url.c_str());
  curl_easy_setopt(h,CURLOPT_USERAGENT,"Mozilla/5.0");
  curl_easy_setopt(h,CURLOPT_REFERER,"https://hongguoduanju.com/");
  curl_easy_setopt(h,CURLOPT_CAINFO,"romfs:/cacert.pem");
  curl_easy_setopt(h,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(h,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(h,CURLOPT_CONNECTTIMEOUT,8L);curl_easy_setopt(h,CURLOPT_TIMEOUT,20L);
  curl_easy_setopt(h,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(h,CURLOPT_FOLLOWLOCATION,1L);
  curl_easy_setopt(h,CURLOPT_MAXREDIRS,3L);curl_easy_setopt(h,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
  curl_easy_setopt(h,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(h,CURLOPT_WRITEDATA,&d);
  const auto status=curl_easy_perform(h);if(status!=CURLE_OK || !alive.load()){trace("poster:curl_error:"+std::to_string((int)status));return result;}trace("poster:download_complete");
  long code=0;curl_easy_getinfo(h,CURLINFO_RESPONSE_CODE,&code);if(code!=200){trace("poster:http_error:"+std::to_string(code));return result;}
  trace("poster:decode_begin");int w=0,height=0,channels=0;
  if(!stbi_info_from_memory(d.bytes.data(),(int)d.bytes.size(),&w,&height,&channels)){
    result=decodeModernPoster(d.bytes);trace(result.rgba.empty()?"poster:unsupported_or_invalid":"poster:modern_decode_complete");return result;
  }
  if(w<1 || height<1 || (int64_t)w*height>6*1024*1024)return result;
  std::unique_ptr<unsigned char,decltype(&stbi_image_free)> source(stbi_load_from_memory(d.bytes.data(),(int)d.bytes.size(),&w,&height,&channels,4),stbi_image_free);
  if(!source || !alive.load()){trace("poster:decode_failed");return result;}
  // Decode and resize in the worker; the UI only uploads a 256px-wide texture.
  result.height=std::max(1,std::min(512,(int)std::lround(256.0*height/w)));
  result.rgba.resize(result.width*result.height*4);
  for(int y=0;y<result.height;y++)for(int x=0;x<result.width;x++) {
    const float sx=std::max(0.0f,(x+0.5f)*w/result.width-0.5f),sy=std::max(0.0f,(y+0.5f)*height/result.height-0.5f);
    const int x0=std::min(w-1,(int)sx),x1=std::min(w-1,x0+1),y0=std::min(height-1,(int)sy),y1=std::min(height-1,y0+1);
    const float fx=sx-x0,fy=sy-y0;
    for(int c=0;c<4;c++)result.rgba[(y*result.width+x)*4+c]=(unsigned char)((1-fy)*((1-fx)*source.get()[(y0*w+x0)*4+c]+fx*source.get()[(y0*w+x1)*4+c])+fy*((1-fx)*source.get()[(y1*w+x0)*4+c]+fx*source.get()[(y1*w+x1)*4+c]));
  }
  trace("poster:decode_complete");return result;
}
