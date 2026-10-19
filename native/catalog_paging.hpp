// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>
struct CatalogRequest {uint64_t generation;int page;};
class CatalogPaging {
  std::set<std::string> seen;
  int repeatedPages=0;
public:
  uint64_t generation=0;int page=0;bool loading=false,more=true,failed=false;
  void reset(){++generation;page=0;loading=false;more=true;failed=false;repeatedPages=0;seen.clear();}
  bool canLoad(bool retry=false)const{return !loading&&more&&(!failed||retry);}
  std::optional<CatalogRequest> begin(bool retry=false){if(!canLoad(retry))return {};loading=true;failed=false;return CatalogRequest{generation,page+1};}
  bool current(CatalogRequest request)const{return loading&&request.generation==generation&&request.page==page+1;}
  bool fail(CatalogRequest request){if(!current(request))return false;loading=false;failed=true;return true;}
  std::optional<std::vector<size_t>> complete(CatalogRequest request,const std::vector<std::string>& ids,bool hasMore){
    if(!current(request))return {};
    std::vector<size_t> added;for(size_t i=0;i<ids.size();++i)if(!ids[i].empty()&&seen.insert(ids[i]).second)added.push_back(i);
    repeatedPages=added.empty()?repeatedPages+1:0;page=request.page;loading=false;failed=false;
    more=hasMore&&!ids.empty()&&repeatedPages<3;return added;
  }
  static bool nearBottom(float content,float viewport,float offset){return std::isfinite(content)&&std::isfinite(viewport)&&std::isfinite(offset)&&content>0&&viewport>0&&offset>=0&&content-viewport-offset<=160;}
};
