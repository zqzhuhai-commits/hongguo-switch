#include "app_config.hpp"
#include <cassert>
#include <cstdio>
#include <iterator>
static std::string contents(const char* p){std::ifstream in(p);return std::string(std::istreambuf_iterator<char>(in),{});}
int main(){
  const char* p="/tmp/hongguo-config-test.cfg";
  {std::ofstream f(p);f<<"source=phone\nhwdec=auto\n# ignored\n";}
  AppConfig c;c.read(p);assert(c.get("source","")=="phone");
  {std::ofstream f(p);f<<" hwdec = no # comment\ninvalid\nsource=\n";}
  c.read(p);assert(c.decoder()=="no");assert(c.get("source","")=="phone");assert(c.get("missing","fallback")=="fallback");
  c.values["playback.profile"]="compatible";c.values["playback.prefetch"]="no";c.values["playback.autoplay"]="no";
  c.values["future.option"]="preserved";assert(c.write(p));
  AppConfig restored;restored.read(p);assert(restored.decoder()=="no");assert(restored.profile()=="compatible");
  assert(restored.directRendering()=="no");assert(restored.cacheBytes()=="32MiB");assert(!restored.prefetch()&&!restored.continuous());assert(restored.get("future.option","")=="preserved");
  restored.values["hwdec"]="invalid";restored.values["playback.profile"]="invalid";assert(restored.decoder()=="auto");assert(restored.profile()=="standard");assert(restored.directRendering()=="yes");assert(restored.cacheBytes()=="16MiB");
  const auto original=contents(p);restored.values["bad\nkey"]="injection";assert(!restored.write(p));assert(contents(p)==original);restored.values.erase("bad\nkey");
  assert(!restored.write("/nonexistent-hongguo-directory/config.cfg"));assert(contents(p)==original);
  std::remove(p);
}
