// SPDX-License-Identifier: GPL-3.0-only
// HEIC item extraction follows the reference client's image_heif.go (GPL-3.0).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace heic {
using Bytes=std::vector<unsigned char>;
struct Reader {
  const Bytes& b;size_t at=0;
  uint64_t number(size_t n){if(n>8||at>b.size()||n>b.size()-at)throw std::runtime_error("HEIC truncated");uint64_t v=0;while(n--)v=(v<<8)|b[at++];return v;}
  Bytes take(size_t n){if(at>b.size()||n>b.size()-at)throw std::runtime_error("HEIC truncated");Bytes v(b.begin()+at,b.begin()+at+n);at+=n;return v;}
};
struct Box{std::string type;Bytes data;};
inline std::vector<Box> boxes(const Bytes& b){Reader r{b};std::vector<Box> out;while(r.at<b.size()){const auto start=r.at;auto size=r.number(4);auto tag=r.take(4);size_t header=8;if(size==1){size=r.number(8);header=16;}else if(!size)size=b.size()-start;if(size<header||size>b.size()-start||out.size()>=4096)throw std::runtime_error("HEIC box");out.push_back({std::string(tag.begin(),tag.end()),r.take(size-header)});}return out;}
inline Bytes find(const std::vector<Box>& b,const std::string& type){for(auto& v:b)if(v.type==type)return v.data;return {};}
inline bool matches(const Bytes& b){if(b.size()<16||std::string(b.begin()+4,b.begin()+8)!="ftyp")return false;Reader r{b};const auto n=r.number(4);if(n<16||n>b.size()||n>256)return false;for(size_t i=8;i+4<=n;i+=4){if(i==12)continue;const std::string tag(b.begin()+i,b.begin()+i+4);if(tag=="heic"||tag=="heix"||tag=="hevc"||tag=="hevx")return true;}return false;}
struct Image{Bytes annexB;std::vector<int> transforms;}; // 1=CCW,2=180,3=CW,4=horizontal mirror,5=vertical mirror
inline Image extract(const Bytes& file){
  if(file.size()>20*1024*1024||!matches(file))throw std::runtime_error("HEIC format");
  auto meta=find(boxes(file),"meta");Reader mr{meta};if(mr.number(4)!=0)throw std::runtime_error("HEIC meta");auto children=boxes(mr.take(meta.size()-mr.at));
  auto pitm=find(children,"pitm");Reader pr{pitm};auto version=pr.number(1);pr.number(3);if(version>1)throw std::runtime_error("HEIC primary version");auto primary=pr.number(version?4:2);if(!primary)throw std::runtime_error("HEIC primary");
  auto iinf=find(children,"iinf");Reader ir{iinf};version=ir.number(1);ir.number(3);if(version>1)throw std::runtime_error("HEIC info");auto count=ir.number(version?4:2);auto infos=boxes(ir.take(iinf.size()-ir.at));if(count!=infos.size())throw std::runtime_error("HEIC items");bool valid=false;
  for(auto& box:infos){if(box.type!="infe")continue;Reader r{box.data};auto v=r.number(1);r.number(3);auto id=r.number(v==3?4:2),protection=r.number(2);auto kind=r.take(4);if(id==primary){valid=(v==2||v==3)&&!protection&&std::string(kind.begin(),kind.end())=="hvc1";break;}}
  if(!valid)throw std::runtime_error("HEIC unsupported item");
  auto properties=boxes(find(children,"iprp")),prop=boxes(find(properties,"ipco"));auto ipma=find(properties,"ipma");Reader ar{ipma};version=ar.number(1);auto flags=ar.number(3);count=ar.number(4);if(version>1||count>4096)throw std::runtime_error("HEIC properties");Bytes config;Image result;uint64_t width=0,height=0;
  for(uint64_t e=0;e<count;e++){auto id=ar.number(version?4:2),n=ar.number(1);for(uint64_t i=0;i<n;i++){auto index=ar.number(flags&1?2:1)&(flags&1?32767:127);if(id!=primary||!index)continue;if(index>prop.size())throw std::runtime_error("HEIC property index");auto& p=prop[index-1];if(p.type=="hvcC")config=p.data;else if(p.type=="ispe"){Reader r{p.data};r.number(4);width=r.number(4);height=r.number(4);}else if(p.type=="irot"||p.type=="imir"){if(p.data.size()!=1)throw std::runtime_error("HEIC transform");if(p.type=="irot"){if(p.data[0]&3)result.transforms.push_back(p.data[0]&3);}else result.transforms.push_back((p.data[0]&1)?5:4);}}}
  if(config.size()<23||config[0]!=1||!width||!height||width>4096||height>4096||width*height>6*1024*1024)throw std::runtime_error("HEIC dimensions/config");
  auto iloc=find(children,"iloc"),inlineData=find(children,"idat");Reader lr{iloc};version=lr.number(1);lr.number(3);auto lengths=lr.number(1),offsets=lr.number(1);size_t os=lengths>>4,ls=lengths&15,bs=offsets>>4,is=version?offsets&15:0;count=lr.number(version==2?4:2);if(version>2||count>4096||os>8||ls>8||bs>8||is>8)throw std::runtime_error("HEIC location");Bytes item;
  for(uint64_t e=0;e<count;e++){auto id=lr.number(version==2?4:2),method=version?lr.number(2)&15:0,ref=lr.number(2),base=lr.number(bs),n=lr.number(2);if(n>4096||(id==primary&&(ref||method>1)))throw std::runtime_error("HEIC external data");for(uint64_t i=0;i<n;i++){lr.number(is);auto offset=lr.number(os),size=lr.number(ls);if(id!=primary)continue;auto& data=method?inlineData:file;if(base>data.size()||offset>data.size()-base)throw std::runtime_error("HEIC offset");auto start=base+offset;if(!size)size=data.size()-start;if(size>data.size()-start||size>20*1024*1024-item.size())throw std::runtime_error("HEIC extent");item.insert(item.end(),data.begin()+start,data.begin()+start+size);}}
  auto nal=[&](const Bytes& v){if(v.size()<2||v.size()>20*1024*1024-4-result.annexB.size())throw std::runtime_error("HEIC NAL");result.annexB.insert(result.annexB.end(),{0,0,0,1});result.annexB.insert(result.annexB.end(),v.begin(),v.end());};
  Reader cr{config,23};for(unsigned i=0;i<config[22];i++){cr.number(1);auto n=cr.number(2);if(n>4096)throw std::runtime_error("HEIC NAL count");while(n--){auto size=cr.number(2);nal(cr.take(size));}}if(cr.at!=config.size()||result.annexB.empty())throw std::runtime_error("HEIC configuration");Reader nr{item};for(size_t i=0;nr.at<item.size();i++){if(i>=4096)throw std::runtime_error("HEIC picture NAL count");auto n=nr.number((config[21]&3)+1);nal(nr.take(n));}if(item.empty())throw std::runtime_error("HEIC empty");return result;
}
}
