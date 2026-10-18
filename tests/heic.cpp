// SPDX-License-Identifier: GPL-3.0-only
#include "heic.hpp"
#include <cassert>
#include <iostream>
using heic::Bytes;
static void num(Bytes& b,uint64_t n,int width){while(width--)b.push_back(n>>(width*8));}
static Bytes box(std::string tag,Bytes b){Bytes out;num(out,b.size()+8,4);out.insert(out.end(),tag.begin(),tag.end());out.insert(out.end(),b.begin(),b.end());return out;}
static void append(Bytes& b,Bytes a){b.insert(b.end(),a.begin(),a.end());}
int main(){
  Bytes file=box("ftyp",{'h','e','i','c',0,0,0,0,'h','e','i','c'}),meta(4),pitm(4);num(pitm,1,2);append(meta,box("pitm",pitm));
  Bytes infe={2,0,0,0,0,1,0,0,'h','v','c','1'},iinf(4);num(iinf,1,2);append(iinf,box("infe",infe));append(meta,box("iinf",iinf));
  Bytes config(23);config[0]=1;config[21]=3;config[22]=1;append(config,{32,0,1,0,2,64,1});Bytes ispe(4);num(ispe,400,4);num(ispe,600,4);auto props=box("hvcC",config);append(props,box("ispe",ispe));auto iprp=box("ipco",props);Bytes associations={0,0,0,0,0,0,0,1,0,1,2,1,2};append(iprp,box("ipma",associations));append(meta,box("iprp",iprp));
  Bytes item={0,0,0,2,38,1},iloc={1,0,0,0,0x44,0,0,1,0,1,0,1,0,0,0,1};num(iloc,0,4);num(iloc,item.size(),4);append(meta,box("iloc",iloc));append(meta,box("idat",item));append(file,box("meta",meta));
  auto image=heic::extract(file);assert(image.annexB==Bytes({0,0,0,1,64,1,0,0,0,1,38,1}));
  for(size_t n=0;n<file.size();n++){bool failed=false;try{heic::extract(Bytes(file.begin(),file.begin()+n));}catch(...){failed=true;}assert(failed);}
  auto broken=file;broken[0]=127;bool rejected=false;try{heic::extract(broken);}catch(...){rejected=true;}assert(rejected);
  std::cout<<"PASS: primary HEIC item, inline extent, Annex B and all truncated prefixes\n";
}
