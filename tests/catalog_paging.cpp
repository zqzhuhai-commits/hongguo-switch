#include "catalog_paging.hpp"
#include <cassert>
#include <limits>
int main(){
  CatalogPaging p;p.reset();auto first=p.begin();assert(first&&first->page==1&&!p.begin());
  auto indices=p.complete(*first,{"1","2","2",""},true);assert(indices&&*indices==std::vector<size_t>({0,1}));
  auto next=p.begin();assert(next&&next->page==2);assert(p.fail(*next));assert(!p.begin());auto retry=p.begin(true);assert(retry&&retry->page==2);
  indices=p.complete(*retry,{"2","3","4"},true);assert(indices&&*indices==std::vector<size_t>({1,2}));
  auto obsolete=p.begin();p.reset();auto current=p.begin();assert(!p.complete(*obsolete,{"old"},false));assert(!p.fail(*obsolete));assert(p.loading);
  indices=p.complete(*current,{"1","2"},true);assert(indices&&indices->size()==2);
  for(int i=0;i<3;i++){auto r=p.begin();assert(r);indices=p.complete(*r,{"1","2"},true);assert(indices&&indices->empty());}assert(!p.more&&!p.begin());
  p.reset();auto r=p.begin();p.complete(*r,{},true);assert(!p.more);
  p.reset();r=p.begin();p.complete(*r,{"last"},false);assert(!p.more&&!p.begin(true));
  assert(!CatalogPaging::nearBottom(1800,500,1100));assert(CatalogPaging::nearBottom(1800,500,1150));assert(CatalogPaging::nearBottom(400,500,0));
  assert(!CatalogPaging::nearBottom(0,500,0));assert(!CatalogPaging::nearBottom(500,0,0));assert(!CatalogPaging::nearBottom(500,500,-1));assert(!CatalogPaging::nearBottom(std::numeric_limits<float>::infinity(),500,0));
}
