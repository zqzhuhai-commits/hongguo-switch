// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <fstream>
#include <cstdio>
#include <map>
#include <string>
struct AppConfig {
  std::map<std::string,std::string> values;
  std::string get(const std::string& key,const std::string& fallback)const{auto i=values.find(key);return i==values.end()?fallback:i->second;}
  void read(const std::string& file){std::ifstream in(file);std::string line;while(std::getline(in,line)){auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);auto equals=line.find('=');if(equals==std::string::npos)continue;auto trim=[](std::string s){auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return std::string();auto b=s.find_last_not_of(" \t\r");return s.substr(a,b-a+1);};auto key=trim(line.substr(0,equals)),value=trim(line.substr(equals+1));if(!key.empty()&&!value.empty())values[key]=value;}}
  bool write(const std::string& file)const {
    const auto temporary=file+".tmp";
    std::ofstream out(temporary,std::ios::trunc);
    if(!out)return false;
    out<<"# 红果短剧设置；由应用设置页面保存。\n";
    for(const auto& item:values){
      if(item.first.find_first_of("\r\n=#")!=std::string::npos||item.second.find_first_of("\r\n#")!=std::string::npos){out.close();std::remove(temporary.c_str());return false;}
      out<<item.first<<"="<<item.second<<"\n";
    }
    out.close();
    if(!out||std::rename(temporary.c_str(),file.c_str())!=0){std::remove(temporary.c_str());return false;}
    return true;
  }
  bool save()const{return write("sdmc:/switch/hongguo.cfg");}
  std::string decoder()const{auto value=get("hwdec","auto");return value=="no"||value=="nvdec"||value=="nvdec-copy"?value:"auto";}
  std::string profile()const{return get("playback.profile","standard")=="compatible"?"compatible":"standard";}
  bool prefetch()const{return get("playback.prefetch","yes")=="yes";}
  bool continuous()const{return get("playback.autoplay","yes")=="yes";}
  std::string directRendering()const{return profile()=="compatible"?"no":"yes";}
  std::string cacheBytes()const{return profile()=="compatible"?"32MiB":"16MiB";}
  static AppConfig load(){AppConfig c;c.read("romfs:/hongguo.cfg");c.read("sdmc:/switch/hongguo.cfg");return c;}
};
