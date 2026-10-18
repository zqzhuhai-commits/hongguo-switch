// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include "app_config.hpp"
#include "workers.hpp"
#include <curl/curl.h>
#include <mbedtls/md5.h>
#include <mbedtls/sha512.h>
#include <mbedtls/aes.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR >= 3 || MBEDTLS_VERSION_NUMBER < 0x02070000
#define mbedtls_md5_ret mbedtls_md5
#define mbedtls_sha512_ret mbedtls_sha512
#endif
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <future>

std::vector<unsigned char> unhex(const std::string& s){
  if(s.size()%2||s.size()>32*1024*1024)throw std::runtime_error("Invalid binary length");
  std::vector<unsigned char> out(s.size()/2);
  auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::runtime_error("Invalid binary data");};
  for(size_t i=0;i<out.size();i++)out[i]=(digit(s[2*i])<<4)|digit(s[2*i+1]);return out;
}
std::string hex(const unsigned char* p,size_t n){static const char* chars="0123456789abcdef";std::string s(n*2,'0');for(size_t i=0;i<n;i++){s[2*i]=chars[p[i]>>4];s[2*i+1]=chars[p[i]&15];}return s;}
static std::string text(JSContext* c,JSValueConst value){const char* p=JS_ToCString(c,value);if(!p)throw std::runtime_error("JavaScript conversion failed");std::string s(p);JS_FreeCString(c,p);return s;}
static JSValue hashFn(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
  try{if(argc!=2)throw std::runtime_error("Arguments");const auto algo=text(c,argv[0]);auto bytes=unhex(text(c,argv[1]));unsigned char out[64];size_t n;
    if(algo=="md5"){mbedtls_md5_ret(bytes.data(),bytes.size(),out);n=16;}
    else if(algo=="sha512"){mbedtls_sha512_ret(bytes.data(),bytes.size(),out,0);n=64;}
    else throw std::runtime_error("Algorithm");const auto s=hex(out,n);return JS_NewStringLen(c,s.data(),s.size());
  }catch(...){return JS_ThrowInternalError(c,"Digest failed");}
}
static JSValue aesFn(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
  try{if(argc!=4)throw std::runtime_error("Arguments");const auto algo=text(c,argv[0]);auto key=unhex(text(c,argv[1])),iv=unhex(text(c,argv[2])),in=unhex(text(c,argv[3]));
    if(key.size()!=16||iv.size()!=16)throw std::runtime_error("AES parameters");std::vector<unsigned char> out(in.size());mbedtls_aes_context aes;mbedtls_aes_init(&aes);int status=-1;
    if(algo=="aes-128-cbc"&&in.size()%16==0){mbedtls_aes_setkey_dec(&aes,key.data(),128);status=mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_DECRYPT,in.size(),iv.data(),in.data(),out.data());}
    else if(algo=="aes-128-ctr"){unsigned char stream[16]={};size_t offset=0;mbedtls_aes_setkey_enc(&aes,key.data(),128);status=mbedtls_aes_crypt_ctr(&aes,in.size(),&offset,iv.data(),stream,in.data(),out.data());}
    mbedtls_aes_free(&aes);if(status!=0)throw std::runtime_error("AES failed");const auto s=hex(out.data(),out.size());return JS_NewStringLen(c,s.data(),s.size());
  }catch(...){return JS_ThrowInternalError(c,"AES failed");}
}
struct Response{std::string body;Json headers=Json::object();};
static size_t bodyCb(char* p,size_t a,size_t b,void* data){auto* r=(Response*)data;const size_t n=a*b;if(r->body.size()+n>16*1024*1024)return 0;r->body.append(p,n);return n;}
static size_t headerCb(char* p,size_t a,size_t b,void* data){auto* r=(Response*)data;std::string line(p,a*b);auto pos=line.find(':');if(pos!=std::string::npos){std::string k=line.substr(0,pos),v=line.substr(pos+1);std::transform(k.begin(),k.end(),k.begin(),[](unsigned char x){return std::tolower(x);});while(!v.empty()&&std::isspace((unsigned char)v.front()))v.erase(v.begin());while(!v.empty()&&std::isspace((unsigned char)v.back()))v.pop_back();r->headers[k]=v;}return a*b;}
static int httpProgress(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return inApiBackground()&&apiForegroundPending()?1:0;}
static JSValue httpFn(JSContext* c,JSValueConst,int argc,JSValueConst* argv){
  CURL* handle=nullptr;curl_slist* list=nullptr;
  try{if(argc!=4)throw std::runtime_error("Arguments");std::string url=text(c,argv[0]),method=text(c,argv[1]),body=text(c,argv[3]);Json headers=Json::parse(text(c,argv[2]));
    if((url.rfind("https://",0)!=0&&url.rfind("http://",0)!=0)||url.size()>8192||body.size()>1024*1024||!headers.is_object())throw std::runtime_error("HTTP parameters");
    auto* owner=static_cast<Backend*>(JS_GetContextOpaque(c));if(!owner)throw std::runtime_error("HTTP owner");handle=owner->httpTransport();if(!handle)throw std::runtime_error("HTTP init");curl_easy_reset(handle);Response response;
    for(auto i=headers.begin();i!=headers.end();++i){const std::string line=i.key()+": "+i.value().get<std::string>();if(line.find('\r')!=std::string::npos||line.find('\n')!=std::string::npos)throw std::runtime_error("Header");list=curl_slist_append(list,line.c_str());}
    curl_easy_setopt(handle,CURLOPT_URL,url.c_str());curl_easy_setopt(handle,CURLOPT_HTTPHEADER,list);
    curl_easy_setopt(handle,CURLOPT_CAINFO,"romfs:/cacert.pem");curl_easy_setopt(handle,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(handle,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT,inApiBackground()?3L:15L);curl_easy_setopt(handle,CURLOPT_TIMEOUT,inApiBackground()?8L:35L);curl_easy_setopt(handle,CURLOPT_MAXCONNECTS,8L);curl_easy_setopt(handle,CURLOPT_DNS_CACHE_TIMEOUT,300L);curl_easy_setopt(handle,CURLOPT_TCP_KEEPALIVE,1L);curl_easy_setopt(handle,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(handle,CURLOPT_XFERINFOFUNCTION,httpProgress);curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(handle,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(handle,CURLOPT_MAXREDIRS,3L);curl_easy_setopt(handle,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTP|CURLPROTO_HTTPS);curl_easy_setopt(handle,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,bodyCb);curl_easy_setopt(handle,CURLOPT_WRITEDATA,&response);curl_easy_setopt(handle,CURLOPT_HEADERFUNCTION,headerCb);curl_easy_setopt(handle,CURLOPT_HEADERDATA,&response);
    if(method=="POST"){curl_easy_setopt(handle,CURLOPT_POST,1L);curl_easy_setopt(handle,CURLOPT_POSTFIELDS,body.data());curl_easy_setopt(handle,CURLOPT_POSTFIELDSIZE,(long)body.size());}else if(method!="GET")throw std::runtime_error("HTTP method");
    const auto status=curl_easy_perform(handle);long code=0,connections=0;double elapsed=0,dns=0,firstByte=0;curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&code);curl_easy_getinfo(handle,CURLINFO_TOTAL_TIME,&elapsed);curl_easy_getinfo(handle,CURLINFO_NAMELOOKUP_TIME,&dns);curl_easy_getinfo(handle,CURLINFO_STARTTRANSFER_TIME,&firstByte);curl_easy_getinfo(handle,CURLINFO_NUM_CONNECTS,&connections);trace("http:code="+std::to_string(code)+":ms="+std::to_string((long)(elapsed*1000))+":dns_ms="+std::to_string((long)(dns*1000))+":first_byte_ms="+std::to_string((long)(firstByte*1000))+":new_connections="+std::to_string(connections));curl_easy_reset(handle);handle=nullptr;curl_slist_free_all(list);list=nullptr;
    if(status!=CURLE_OK)throw std::runtime_error("Network unavailable");const std::string value=Json{{"status",code},{"headers",response.headers},{"hex",hex((const unsigned char*)response.body.data(),response.body.size())}}.dump();return JS_NewStringLen(c,value.data(),value.size());
  }catch(...){if(handle)curl_easy_reset(handle);if(list)curl_slist_free_all(list);return JS_ThrowInternalError(c,"内容源或网络暂不可用");}
}
void Backend::evaluate(const std::string& script,const char* name){JSValue value=JS_Eval(context,script.data(),script.size(),name,JS_EVAL_TYPE_GLOBAL);const bool failed=JS_IsException(value);JS_FreeValue(context,value);if(failed){JS_FreeValue(context,JS_GetException(context));throw std::runtime_error(std::string("Backend load failed: ")+name);}}
Backend::Backend(){
  runtime=JS_NewRuntime();if(!runtime)throw std::runtime_error("Runtime init");JS_SetMemoryLimit(runtime,256*1024*1024);JS_SetMaxStackSize(runtime,256*1024);context=JS_NewContext(runtime);if(!context)throw std::runtime_error("Context init");
  JS_SetContextOpaque(context,this);JSValue global=JS_GetGlobalObject(context);JS_SetPropertyStr(context,global,"__hash",JS_NewCFunction(context,hashFn,"__hash",2));JS_SetPropertyStr(context,global,"__aes",JS_NewCFunction(context,aesFn,"__aes",4));JS_SetPropertyStr(context,global,"__http",JS_NewCFunction(context,httpFn,"__http",4));JS_FreeValue(context,global);
  // All legacy source settings migrate to the released Phone adapter.
  evaluate("globalThis.SwitchConfig={source:\"phone\"};","config");
  for(const char* file:{"deps.js","prelude.js","hongguo.cjs","phone_source.js","adapter.js"}){std::ifstream in(std::string("romfs:/")+file);if(!in)throw std::runtime_error("Backend resource missing");std::string script((std::istreambuf_iterator<char>(in)),{});evaluate(script,file);}
}
Backend::~Backend(){if(transport)curl_easy_cleanup(transport);if(context)JS_FreeContext(context);if(runtime)JS_FreeRuntime(runtime);}
#ifdef HONGGUO_PREVIEW
#include "fixtures.hpp"
#include <cstdlib>
#endif
Json Backend::call(const std::string& function,const Json& args){
#ifdef HONGGUO_PREVIEW
  if(!std::getenv("HONGGUO_LIVE")&&function!="selftest")return previewCall(function,args);
#endif

  if(!inApiWorker()){
    auto task=std::make_shared<std::packaged_task<Json()>>([this,function,args]{return call(function,args);});
    auto result=task->get_future();
    if(!apiTask([task]{(*task)();}))throw std::runtime_error("后台任务已停止");
    return result.get();
  }
  std::lock_guard<std::mutex> guard(lock);trace("api:"+function+":begin");JS_UpdateStackTop(runtime);
  if(function!="home"&&function!="search"&&function!="detail"&&function!="open"&&function!="read"&&function!="selftest"&&function!="clearCache"&&function!="qualities"&&function!="prefetch"&&function!="downloadSlice")throw std::runtime_error("Invalid method");
  evaluate("globalThis.__done=false;globalThis.__answer='';Promise.resolve(SwitchAPI["+Json(function).dump()+"](..."+args.dump()+")).then(v=>{globalThis.__answer=JSON.stringify({ok:true,value:v});globalThis.__done=true;},e=>{globalThis.__answer=JSON.stringify({ok:false,message:String(e&&e.message||'接口处理失败').replace(/https?:\\/\\/[^ ]+/g,'[地址]').slice(0,180)});globalThis.__done=true;});","rpc");
  JSContext* job=nullptr;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
  while(true){JSValue global=JS_GetGlobalObject(context);JSValue done=JS_GetPropertyStr(context,global,"__done");bool complete=JS_ToBool(context,done);JS_FreeValue(context,done);if(complete){JSValue answer=JS_GetPropertyStr(context,global,"__answer");std::string raw=text(context,answer);JS_FreeValue(context,answer);JS_FreeValue(context,global);Json result=Json::parse(raw);if(!result.value("ok",false))throw std::runtime_error(result.value("message",std::string("内容源或网络暂不可用")));trace("api:"+function+":complete");return result["value"];}JS_FreeValue(context,global);
    if(std::chrono::steady_clock::now()>deadline||JS_ExecutePendingJob(runtime,&job)<=0)throw std::runtime_error("接口处理失败");
  }
}
