// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include "app_config.hpp"
#include "episode_groups.hpp"
#include "stream_request.hpp"
#include "poster.hpp"
#include "download_queue.hpp"
#include "workers.hpp"
#include "playback_policy.hpp"
#include "playback_touch.hpp"
#include <chrono>
#include <cstdio>
#include <borealis/views/image.hpp>
#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <switch.h>
#include <curl/curl.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <mpv/stream_cb.h>
#include <fstream>
#include <memory>
#include <atomic>

#include <cstring>
#include <algorithm>
#include <sys/stat.h>

#ifdef HONGGUO_PREVIEW
#include "fixtures.hpp"
#include <cstdlib>
#endif
static Backend* backend=nullptr;
static std::unique_ptr<DownloadQueue> downloads;
static void startDownload(const Json& detail,int index);
static void openDownloads();
static Json library;
static const char* store="sdmc:/switch/hongguo-saf/library.json";
static void save(){try{std::ofstream out(std::string(store)+".tmp");out<<library.dump();out.close();if(out)std::rename((std::string(store)+".tmp").c_str(),store);}catch(...){}}
static void load(){mkdir("sdmc:/switch",0777);mkdir("sdmc:/switch/hongguo-saf",0777);try{std::ifstream in(store);library=Json::parse(in);if(!library.is_object()||!library.contains("favorites")||!library["favorites"].is_array()||!library.contains("history")||!library["history"].is_array())throw std::runtime_error("Invalid library");}catch(...){library={{"format","hongguotv.library"},{"version",1},{"favorites",Json::array()},{"history",Json::array()},{"later",Json::array()}};}}
static void record(const Json& series,const Json& episode,int index,double seconds,double duration){if(seconds<0||duration<0)return;auto& rows=library["history"];rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Json& r){return r.value("series",Json::object()).value("id",std::string())==series.value("id",std::string());}),rows.end());rows.insert(rows.begin(),Json{{"series",series},{"episodeId",episode.value("id",std::string())},{"episodeIndex",index},{"position",(int64_t)(seconds*1000)},{"duration",(int64_t)(duration*1000)},{"completed",duration>0&&seconds>=duration-3},{"updatedAt",(int64_t)time(nullptr)*1000}});while(rows.size()>100)rows.erase(rows.end()-1);save();}
static void message(const std::string& text){auto* dialog=new brls::Dialog(text);dialog->addButton("确定",[]{});dialog->open();}
static void back(brls::View* view){view->registerAction("返回",brls::BUTTON_B,[](brls::View*){brls::Application::popActivity();return true;});}
// One touch target for the entire card, rather than its decorative children.
class ClickableBox:public brls::Box {
public:
  explicit ClickableBox(brls::Axis axis):brls::Box(axis){setFocusable(true);addGestureRecognizer(new brls::TapGestureRecognizer(this));}
  brls::View* hitTest(brls::Point point)override{return brls::Box::hitTest(point)?this:nullptr;}
};
static brls::Box* quickButton(const std::string& name,const std::string& note,std::function<void()> fn){auto* box=new ClickableBox(brls::Axis::COLUMN);box->setWidth(220);box->setHeight(88);box->setPadding(12);box->setMarginRight(12);box->setBackgroundColor(nvgRGB(35,38,49));for(const auto& text:{name,note}){auto* label=new brls::Label();label->setText(text);label->setWidth(196);label->setFontSize(text==name?20:14);label->setHorizontalAlign(brls::HorizontalAlign::LEFT);box->addView(label);}box->registerClickAction([fn](brls::View*){fn();return true;});return box;}
static brls::DetailCell* cell(const std::string& name,const std::string& note,std::function<void()> fn){auto* v=new brls::DetailCell();v->setText(name);v->setDetailText(note);v->addGestureRecognizer(new brls::TapGestureRecognizer(v));v->registerClickAction([fn](brls::View*){fn();return true;});return v;}
#include "video_surface.hpp"
static std::atomic<int> posterJobs{0};
class PosterImage:public brls::Image {
  std::string url,badge;bool started=false,finished=false;int attempts=0;std::chrono::steady_clock::time_point retryAt{};
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
public:
  explicit PosterImage(std::string url,float width=104,float height=156,std::string badge=""):url(std::move(url)),badge(std::move(badge)){setWidth(width);setHeight(height);setScalingType(brls::ImageScalingType::FIT);setBackgroundColor(nvgRGB(40,43,47));}
  ~PosterImage(){alive->store(false);}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style style,brls::FrameContext* ctx)override {
    if(!started && attempts<3 && std::chrono::steady_clock::now()>=retryAt && !url.empty() && x+w>=0 && x<=1280 && y+h>=0 && y<=720 && posterJobs.load()<2){started=true;attempts++;posterJobs++;auto flag=alive;auto address=url;
      posterTask([this,flag,address]{PosterPixels image;try{image=fetchPoster(address,*flag);}catch(...){}posterJobs--;brls::sync([this,flag,image=std::move(image)]{if(!flag->load())return;finished=true;if(!image.rgba.empty()){trace("poster:upload_begin");innerSetImage(nvgCreateImageRGBA(brls::Application::getNVGContext(),image.width,image.height,0,image.rgba.data()));trace("poster:upload_end");}else if(attempts<3){started=false;retryAt=std::chrono::steady_clock::now()+std::chrono::seconds(attempts*3);}});});
    }
    if(getTexture())brls::Image::draw(vg,x,y,w,h,style,ctx);
    else {nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,15);nvgFillColor(vg,nvgRGB(160,165,170));nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgText(vg,x+w/2,y+h/2,finished||url.empty()?"暂无封面":"封面加载中",nullptr);}
    if(!badge.empty()){nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,14);nvgFillPaint(vg,nvgLinearGradient(vg,x,y+h-80,x,y+h,nvgRGBA(0,0,0,0),nvgRGBA(0,0,0,205)));nvgFill(vg);nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,17);nvgFillColor(vg,nvgRGB(255,255,255));nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_BOTTOM);nvgText(vg,x+12,y+h-12,badge.c_str(),nullptr);}

  }
};
static brls::Box* posterCell(const Json& row,std::function<void()> fn){auto* box=new ClickableBox(brls::Axis::ROW);box->setMinHeight(172);box->setPadding(8);auto* poster=new PosterImage(row.value("cover",std::string()));poster->setMarginRight(20);poster->setShrink(0);box->addView(poster);auto* text=new brls::Box(brls::Axis::COLUMN);text->setGrow(1);text->setShrink(1);auto* title=new brls::Label();title->setText(row.value("title",std::string()));title->setWidthPercentage(100);title->setSingleLine(false);title->setFontSize(22);title->setHorizontalAlign(brls::HorizontalAlign::LEFT);text->addView(title);auto* note=new brls::Label();note->setText(row.value("badge",std::string()));note->setFontSize(16);note->setWidthPercentage(100);note->setHorizontalAlign(brls::HorizontalAlign::LEFT);text->addView(note);box->addView(text);box->registerClickAction([fn](brls::View*){fn();return true;});return box;}

static void addGrid(brls::Box* list,const std::vector<brls::View*>& tiles,size_t columns,float gap,float rowGap);
static std::mutex playbackErrorMutex;
static std::string playbackErrorId,playbackError;
class SceneFrame:public brls::AppletFrame {public:explicit SceneFrame(brls::View* root):brls::AppletFrame(root){}std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);~SceneFrame(){*alive=false;}};
static SceneFrame* frame(brls::Box* list,const std::string& title){auto* scroll=new brls::ScrollingFrame();scroll->setGrow(1);list->setPadding(20,50,30,50);scroll->setContentView(list);auto* f=new SceneFrame(scroll);f->setTitle(title);return f;}
struct Reader{std::string id;int64_t total=0,pos=0,cacheAt=-1;std::vector<unsigned char> cache;std::atomic<bool> cancelled{false};};
static int64_t readStream(void* data,char* out,uint64_t size){auto* r=(Reader*)data;if(r->cancelled)return -1;if(r->pos>=r->total)return 0;try{
  const int64_t start=(r->pos/(1024*1024))*(1024*1024);
  if(r->cacheAt!=start){auto value=backend->call("read",Json::array({r->id,start,std::min(start+1024*1024-1,r->total-1)}));r->cache=unhex(value.get<std::string>());r->cacheAt=start;}
  size_t offset=(size_t)(r->pos-r->cacheAt);if(offset>=r->cache.size())return -1;size_t take=std::min<size_t>(size,r->cache.size()-offset);if(r->cancelled)return -1;std::memcpy(out,r->cache.data()+offset,take);r->pos+=take;return take;
}catch(...){return -1;}}
static int64_t seekStream(void* data,int64_t at){auto* r=(Reader*)data;if(at<0||at>r->total)return -1;r->pos=at;return at;}
static int64_t sizeStream(void* data){return ((Reader*)data)->total;}
static void closeStream(void* data){delete (Reader*)data;}
static void cancelStream(void* data){((Reader*)data)->cancelled=true;}
static int openStream(void*,char* uri,mpv_stream_cb_info* info){try{
  StreamRequest request;if(!parseStreamRequest(uri,request))return MPV_ERROR_LOADING_FAILED;const auto& id=request.id;
  const Json meta=backend->call("open",Json::array({id,480,request.fresh,request.tier}));auto* r=new Reader();r->id=id;r->total=meta.at("total").get<int64_t>();if(r->total<=0){delete r;return MPV_ERROR_LOADING_FAILED;}
  info->cookie=r;info->read_fn=readStream;info->seek_fn=seekStream;info->size_fn=sizeStream;info->close_fn=closeStream;info->cancel_fn=cancelStream;return 0;
}catch(const std::exception& e){std::lock_guard<std::mutex> guard(playbackErrorMutex);const std::string uriValue(uri);StreamRequest failedRequest;playbackErrorId=parseStreamRequest(uriValue,failedRequest)?failedRequest.id:"";playbackError=e.what();return MPV_ERROR_LOADING_FAILED;}catch(...){return MPV_ERROR_LOADING_FAILED;}}
static void* getProc(void*,const char* name){return (void*)glfwGetProcAddress(name);}
// Continuous touch tracking is needed for release-to-restore, including cancellation.
class PlaybackTouchGesture:public brls::GestureRecognizer {
  std::function<void(brls::GestureState,brls::Point)> callback;bool exclusive,active=false;int finger=0;
public:
  PlaybackTouchGesture(std::function<void(brls::GestureState,brls::Point)> fn,bool claim=false):callback(fn),exclusive(claim){}
  brls::GestureState recognitionLoop(brls::TouchState touch,brls::MouseState mouse,brls::View*,brls::Sound* sound)override{
    auto phase=touch.phase;auto point=touch.position;if(phase==brls::TouchPhase::NONE){phase=mouse.leftButton;point=mouse.position;}*sound=brls::SOUND_NONE;
    if(phase==brls::TouchPhase::START){active=true;finger=touch.fingerId;state=exclusive?brls::GestureState::START:brls::GestureState::UNSURE;callback(state,point);return state;}
    if(!active)return brls::GestureState::FAILED;
    if(state==brls::GestureState::INTERRUPTED||phase==brls::TouchPhase::NONE||finger!=touch.fingerId){active=false;callback(brls::GestureState::INTERRUPTED,point);return state=brls::GestureState::FAILED;}
    if(phase==brls::TouchPhase::END){active=false;callback(brls::GestureState::END,point);return state=brls::GestureState::END;}
    callback(brls::GestureState::STAY,point);return state=exclusive?brls::GestureState::STAY:brls::GestureState::UNSURE;
  }
};
class PlaybackTimeline:public brls::View {
  double current=0,total=0,preview=0;bool dragging=false,available=false;
  std::function<void(double)> seek;
  void updateTouch(brls::Point p){preview=playbackSeekAt(p.x,getX()+16,getWidth()-32,total);}
public:
  explicit PlaybackTimeline(std::function<void(double)> fn):seek(fn){setFocusable(true);setHideHighlight(false);setHeight(64);
    addGestureRecognizer(new PlaybackTouchGesture([this](brls::GestureState state,brls::Point point){if(state==brls::GestureState::START){dragging=available;updateTouch(point);brls::Application::giveFocus(this);}else if(state==brls::GestureState::STAY&&dragging)updateTouch(point);else if(state==brls::GestureState::END){if(dragging){updateTouch(point);seek(preview);}dragging=false;}else if(state==brls::GestureState::INTERRUPTED)dragging=false;},true));
    registerAction("后退5秒",brls::BUTTON_LEFT,[this](brls::View*){if(available)seek(std::max(0.0,current-5));return true;});registerAction("前进5秒",brls::BUTTON_RIGHT,[this](brls::View*){if(available)seek(std::min(total,current+5));return true;});
  }
  void update(double at,double length,bool enabled){current=at;total=length;available=enabled&&length>0;if(!available)dragging=false;}
  void reset(){dragging=false;available=false;current=total=preview=0;}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style,brls::FrameContext*)override{
    const double shown=dragging?preview:current;const float left=x+16,width=std::max(1.0f,w-32),at=left+(float)playbackSeekAt(shown,0,total,1)*width;
    nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,8);nvgFillColor(vg,nvgRGBA(15,17,23,215));nvgFill(vg);
    nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,16);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgFillColor(vg,nvgRGB(245,245,245));
    auto clock=[](double t){int s=std::max(0,(int)t);char b[32];std::snprintf(b,sizeof(b),"%d:%02d",s/60,s%60);return std::string(b);};const std::string label=clock(shown)+" / "+clock(total)+(dragging?"  松开跳转":available?"  拖动进度条跳转":"  等待视频加载");nvgText(vg,left,y+7,label.c_str(),nullptr);
    nvgBeginPath(vg);nvgRoundedRect(vg,left,y+40,width,6,3);nvgFillColor(vg,nvgRGB(105,110,122));nvgFill(vg);
    nvgBeginPath(vg);nvgRoundedRect(vg,left,y+40,std::max(0.0f,at-left),6,3);nvgFillColor(vg,nvgRGB(242,85,103));nvgFill(vg);
    nvgBeginPath(vg);nvgCircle(vg,at,y+43,dragging?11:8);nvgFillColor(vg,nvgRGB(255,255,255));nvgFill(vg);
  }
};

class Player:public brls::Box {
  mpv_handle* mpv=nullptr;mpv_render_context* renderer=nullptr;VideoSurface videoSurface;
  Json series,episodes;int index=0,speedIndex=1,rotation=0;
  bool failed=false,nextQueued=false,paused=false,errorState=false,menuVisible=false,loaded=false,holdApplied=false;
  double position=0,duration=0,resumeTarget=0;bool opened=false;uint64_t lastSave=0;size_t retryDelay=0;
  std::string status="正在连接播放源…",lastProgress,selectedTier,selectedQuality="自动";
  bool warmScheduled=false,prefetchEnabled=true,resumePaused=false,videoDiagnosed=false;uint64_t loadStarted=0;
  std::shared_ptr<std::atomic<uint64_t>> warmGeneration=std::make_shared<std::atomic<uint64_t>>(0);
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
  PlaybackHold hold;PlaybackTimeline* timeline=nullptr;
  PlaybackPolicy policy;brls::Box* controls=nullptr;brls::View* firstControl=nullptr;brls::Label* progress=nullptr;
  static std::string clockText(double value){int seconds=std::max(0,(int)value);char text[32];std::snprintf(text,sizeof(text),"%d:%02d",seconds/60,seconds%60);return text;}
  static uint64_t touchTime(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
  void stopHold(){if(holdApplied&&mpv){double rate=hold.previousSpeed;mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate);}holdApplied=false;hold.cancel();}
  void seekTo(double at){if(!mpv||failed||errorState||!loaded||duration<=0)return;stopHold();const std::string value=std::to_string(std::clamp(at,0.0,duration));const char* cmd[]={"seek",value.c_str(),"absolute",nullptr};if(mpv_command(mpv,cmd)>=0){position=std::clamp(at,0.0,duration);persist();}}
  void screenTouch(brls::GestureState state,brls::Point point){if(state==brls::GestureState::UNSURE){stopHold();const bool excluded=menuVisible&&(controls->getFrame().pointInside(point)||timeline->getFrame().pointInside(point));hold.begin(!excluded,touchTime(),point.x,point.y);}else if(state==brls::GestureState::STAY){if(!getFrame().pointInside(point)){stopHold();return;}if(hold.tick(touchTime(),point.x,point.y)&&mpv&&!failed&&!errorState&&loaded&&!paused){hold.previousSpeed=1;mpv_get_property(mpv,"speed",MPV_FORMAT_DOUBLE,&hold.previousSpeed);double rate=2;holdApplied=mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate)>=0;}}else if(state==brls::GestureState::END){const bool tap=hold.isTap();stopHold();if(tap)showMenu(!menuVisible);}else if(state==brls::GestureState::INTERRUPTED)stopHold();}
  void persist(){if(opened&&(position>0||duration>0)&&index>=0&&index<(int)episodes.size())record(series,episodes[index],episodes[index].value("number",index+1)-1,position,duration);}
  void showMenu(bool visible){stopHold();menuVisible=visible;if(!visible)brls::Application::giveFocus(this);controls->setVisibility(visible?brls::Visibility::VISIBLE:brls::Visibility::GONE);timeline->setVisibility(visible?brls::Visibility::VISIBLE:brls::Visibility::GONE);if(visible&&firstControl)brls::Application::giveFocus(firstControl);}
  void togglePause(){stopHold();if(errorState){retryNow();return;}if(!mpv||failed)return;paused=!paused;int value=paused;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&value);}
  void seek(int seconds){if(!mpv||failed||errorState)return;const std::string value=std::to_string(seconds);const char* cmd[]={"seek",value.c_str(),"relative",nullptr};mpv_command(mpv,cmd);}
  void cycleSpeed(){stopHold();if(!mpv||failed)return;const double rates[]={0.75,1,1.25,1.5,2};speedIndex=(speedIndex+1)%5;double rate=rates[speedIndex];mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate);}
  void rotateVideo(){stopHold();if(!mpv||failed)return;int64_t angle=rotation==0?90:0;if(mpv_set_property(mpv,"video-rotate",MPV_FORMAT_INT64,&angle)>=0)rotation=(int)angle;else status="当前视频暂不支持方向切换";}
  void retryNow(){if(mpv&&renderer&&!failed)startEpisode(index,false,true);}
  void scheduleRetry(){stopHold();timeline->reset();errorState=true;if(policy.pending)return;auto wait=policy.scheduleRetry();if(!wait){status="本集暂时无法播放，可重试或选择其他分集。";showMenu(true);return;}trace("playback:retry_scheduled:"+std::to_string(policy.retries));status="正在重新获取播放源（重试 "+std::to_string(policy.retries)+"/3）…";auto flag=alive;const auto token=policy.generation;retryDelay=brls::delay(*wait,[this,flag,token]{if(!flag->load())return;if(policy.consumeRetry(token)){retryDelay=0;startEpisode(index,true);}});}
  void startEpisode(int target,bool retry=false,bool fresh=false,bool preservePause=false){if(!mpv||!renderer||failed||target<0||target>=(int)episodes.size())return;stopHold();timeline->reset();if(retryDelay){brls::cancelDelay(retryDelay);retryDelay=0;}resumePaused=preservePause?paused:(retry?resumePaused:false);if(target!=index){selectedTier.clear();selectedQuality="自动";}policy.begin(retry);warmGeneration->store(policy.generation);warmScheduled=false;loadStarted=touchTime();persist();index=target;position=0;duration=0;opened=true;resumeTarget=0;for(const auto& row:library["history"])if(row.value("episodeId",std::string())==episodes[index].value("id",std::string()))resumeTarget=row.value("position",0.0)/1000;nextQueued=false;paused=false;errorState=false;loaded=false;int unpause=0;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&unpause);status=retry?"正在重新连接（重试 "+std::to_string(policy.retries)+"/3）…":"正在连接播放源…";{std::lock_guard<std::mutex> lock(playbackErrorMutex);playbackErrorId.clear();playbackError.clear();}const auto url=episodes[index].value("localPath",std::string()).empty()?"saf://"+episodes[index].at("id").get<std::string>()+"/"+selectedTier+"/"+((retry||fresh)?"1":"0"):episodes[index].at("localPath").get<std::string>();const char* command[]={"loadfile",url.c_str(),"replace",nullptr};trace("playback:load_begin");if(mpv_command(mpv,command)<0)scheduleRetry();}
  void showQualities(){
    if(!mpv||failed)return;stopHold();const auto id=episodes[index].at("id").get<std::string>();auto flag=alive;
    auto* list=new brls::Box(brls::Axis::COLUMN);auto* picker=::frame(list,"选择清晰度");back(picker);auto pickerFlag=picker->alive;
    auto select=[this,flag,id](const std::string& tier,const std::string& label){if(!*flag)return;const bool current=episodes[index].at("id").get<std::string>()==id;brls::Application::popActivity();if(current&&tier!=selectedTier){selectedTier=tier;selectedQuality=label;startEpisode(index,false,false,true);}};
    auto* automatic=cell("自动","优先使用兼容画质",[select]{select("","自动");});list->addView(automatic);auto* waiting=new brls::Label();waiting->setText("正在读取片源可用画质…");list->addView(waiting);brls::Application::pushActivity(new brls::Activity(picker));
    apiTask([flag,pickerFlag,list,id,select]{Json choices=Json::array();bool ok=false;try{choices=backend->call("qualities",Json::array({id}));ok=choices.is_array();}catch(...){}brls::sync([flag,pickerFlag,list,choices,ok,select]{if(!*flag||!*pickerFlag)return;list->clearViews();auto* automatic=cell("自动","优先使用兼容画质",[select]{select("","自动");});list->addView(automatic);
      if(ok)for(const auto& row:choices){auto tier=row.value("key",std::string()),label=row.value("label",std::string());if(tier.empty()||!validTier(tier))continue;const auto codec=row.value("codec",std::string());list->addView(cell(label,"切换后继续当前进度",[select,tier,label]{select(tier,label);}));}
      if(!ok||choices.empty()){auto* note=new brls::Label();note->setText(ok?"本片源暂未提供其他清晰度":"清晰度读取失败，请返回后重试");list->addView(note);}brls::Application::giveFocus(automatic);
    });});
  }
  void buildMenu(){controls=new brls::Box(brls::Axis::COLUMN);controls->detach();controls->setDetachedPosition(24,290);controls->setSize(brls::Size(1232,342));controls->setPadding(16);controls->setBackgroundColor(nvgRGBA(15,17,23,235));progress=new brls::Label();progress->setWidth(1200);progress->setHeight(24);progress->setFontSize(18);progress->setHorizontalAlign(brls::HorizontalAlign::LEFT);progress->setMarginBottom(12);controls->addView(progress);std::vector<brls::View*> buttons;
    auto add=[&](const std::string& title,std::function<void()> fn){auto* button=new ClickableBox(brls::Axis::COLUMN);button->setWidth(284);button->setHeight(56);button->setPadding(10);button->setBackgroundColor(nvgRGB(42,46,58));auto* label=new brls::Label();label->setText(title);label->setFontSize(18);label->setHorizontalAlign(brls::HorizontalAlign::CENTER);button->addView(label);button->registerClickAction([fn](brls::View*){fn();return true;});buttons.push_back(button);};
    add("播放 / 暂停",[this]{togglePause();});add("后退 10 秒",[this]{seek(-10);});add("前进 10 秒",[this]{seek(10);});add("横竖切换",[this]{rotateVideo();});
    add("上一集",[this]{startEpisode(index-1);});add("下一集",[this]{startEpisode(index+1);});add("倍速",[this]{cycleSpeed();});add("自动连播 开 / 关",[this]{policy.continuous=!policy.continuous;library["autoplay"]=policy.continuous;save();});
    add("下载本集",[this]{startDownload(series,index);});add("清晰度",[this]{showQualities();});add("重试本集",[this]{retryNow();});add("隐藏菜单",[this]{showMenu(false);});add("返回列表",[]{brls::Application::popActivity();});addGrid(controls,buttons,4,12,8);firstControl=buttons.front();controls->setVisibility(brls::Visibility::GONE);addView(controls);timeline=new PlaybackTimeline([this](double at){seekTo(at);});timeline->detach();timeline->setDetachedPosition(24,644);timeline->setSize(brls::Size(1232,64));timeline->setVisibility(brls::Visibility::GONE);timeline->registerAction("控制菜单",brls::BUTTON_UP,[this](brls::View*){showMenu(true);return true;});addView(timeline);
  }
public:
#ifdef HONGGUO_PREVIEW
  void previewMenu(){showMenu(true);}
#endif
  Player(Json detail,int episode):brls::Box(brls::Axis::COLUMN),series(detail),episodes(detail.at("episodes")),index(episode){
    setGrow(1);setBackgroundColor(nvgRGB(0,0,0));setFocusable(true);setHideHighlight(true);appletSetMediaPlaybackState(true);if(library.contains("autoplay")&&library["autoplay"].is_boolean())policy.continuous=library["autoplay"].get<bool>();buildMenu();
    addGestureRecognizer(new PlaybackTouchGesture([this](brls::GestureState state,brls::Point point){screenTouch(state,point);}));
    registerAction("返回",brls::BUTTON_B,[this](brls::View*){if(menuVisible)showMenu(false);else brls::Application::popActivity();return true;});
    registerAction("播放/暂停",brls::BUTTON_A,[this](brls::View*){togglePause();return true;});registerAction("菜单",brls::BUTTON_X,[this](brls::View*){showMenu(!menuVisible);return true;});
    registerAction("后退10秒",brls::BUTTON_LEFT,[this](brls::View*){if(menuVisible)return false;seek(-10);return true;});registerAction("前进10秒",brls::BUTTON_RIGHT,[this](brls::View*){if(menuVisible)return false;seek(10);return true;});
    registerAction("下一集",brls::BUTTON_RB,[this](brls::View*){startEpisode(index+1);return true;});registerAction("上一集",brls::BUTTON_LB,[this](brls::View*){startEpisode(index-1);return true;});registerAction("倍速",brls::BUTTON_Y,[this](brls::View*){cycleSpeed();return true;});registerAction("横竖切换",brls::BUTTON_RT,[this](brls::View*){rotateVideo();return true;},false,false);
    mpv=mpv_create();if(!mpv){failed=true;status="播放器初始化失败";return;}
    mpv_set_option_string(mpv,"config","no");mpv_set_option_string(mpv,"terminal","no");mpv_request_log_messages(mpv,"warn");mpv_set_option_string(mpv,"vo","libmpv");auto config=AppConfig::load();prefetchEnabled=config.get("playback.prefetch","yes")=="yes";auto hwdec=config.get("hwdec","auto");if(hwdec!="auto"&&hwdec!="no"&&hwdec!="nvdec"&&hwdec!="nvdec-copy")hwdec="auto";mpv_set_option_string(mpv,"hwdec",hwdec.c_str());mpv_set_option_string(mpv,"audio-channels","stereo");mpv_set_option_string(mpv,"cache","yes");mpv_set_option_string(mpv,"demuxer-max-bytes","16MiB");mpv_set_option_string(mpv,"opengl-glfinish","yes");mpv_set_option_string(mpv,"vd-lavc-dr","yes");
#ifdef HONGGUO_PREVIEW
    // Headless CI has neither a hardware decoder nor an audio device. Use a
    // deterministic software decoder and a clocked null audio sink there.
    mpv_set_option_string(mpv,"hwdec","no");mpv_set_option_string(mpv,"vd-lavc-dr","no");mpv_set_option_string(mpv,"ao","null");
#endif
    if(mpv_stream_cb_add_ro(mpv,"saf",nullptr,openStream)<0||mpv_initialize(mpv)<0){failed=true;status="播放器初始化失败";return;}
    mpv_opengl_init_params gl{getProc,nullptr};mpv_render_param params[]={{MPV_RENDER_PARAM_API_TYPE,(void*)MPV_RENDER_API_TYPE_OPENGL},{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,&gl},{MPV_RENDER_PARAM_INVALID,nullptr}};
    if(mpv_render_context_create(&renderer,mpv,params)<0){failed=true;status="视频渲染初始化失败";return;}startEpisode(index);
  }
  ~Player(){stopHold();*alive=false;if(retryDelay)brls::cancelDelay(retryDelay);persist();if(renderer)mpv_render_context_free(renderer);if(mpv)mpv_terminate_destroy(mpv);appletSetMediaPlaybackState(false);}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style style,brls::FrameContext* ctx)override{
    if(mpv&&!failed){for(int count=0;count<30;count++){mpv_event* event=mpv_wait_event(mpv,0);if(event->event_id==MPV_EVENT_NONE)break;if(event->event_id==MPV_EVENT_FILE_LOADED){status="";errorState=false;loaded=true;trace("playback:loaded:ms="+std::to_string(touchTime()-loadStarted));if(resumeTarget>0){const std::string at=std::to_string(resumeTarget);const char* seekCommand[]={"seek",at.c_str(),"absolute",nullptr};mpv_command(mpv,seekCommand);resumeTarget=0;}if(resumePaused){int p=1;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&p);paused=true;resumePaused=false;}}
      if(event->event_id==MPV_EVENT_LOG_MESSAGE){auto* log=(mpv_event_log_message*)event->data;std::string line=log->text?log->text:"";for(size_t at=0;(at=line.find("http",at))!=std::string::npos;){auto end=line.find_first_of(" \t\r\n",at);line.replace(at,end==std::string::npos?line.size()-at:end-at,"[URL]");at+=5;}trace("mpv:"+std::string(log->prefix?log->prefix:"")+":"+line.substr(0,240));}
      if(event->event_id==MPV_EVENT_END_FILE){auto* end=(mpv_event_end_file*)event->data;if(end&&end->reason==MPV_END_FILE_REASON_ERROR){trace("playback:failed");scheduleRetry();}else if(end&&end->reason==MPV_END_FILE_REASON_EOF&&!nextQueued){nextQueued=true;if(duration>0)position=duration;persist();const int next=policy.nextEpisode(index,(int)episodes.size());if(next>=0){auto flag=alive;const auto generation=policy.generation;brls::sync([this,flag,next,generation]{if(flag->load()&&generation==policy.generation)startEpisode(next);});}else{status=index+1==(int)episodes.size()?"本剧已播放完毕":"本集已结束，自动连播已关闭";showMenu(true);}}}
    }mpv_get_property(mpv,"time-pos",MPV_FORMAT_DOUBLE,&position);mpv_get_property(mpv,"duration",MPV_FORMAT_DOUBLE,&duration);if(armGetSystemTick()-lastSave>armNsToTicks(10000000000ULL)){lastSave=armGetSystemTick();persist();}}
    if(loaded&&position>3&&!videoDiagnosed){videoDiagnosed=true;for(const char* prop:{"video-codec","video-format","video-params/w","video-params/h","video-out-params/w","video-out-params/h","vid","estimated-vf-fps","frame-drop-count","decoder-frame-drop-count","hwdec-current"}){char* value=mpv_get_property_string(mpv,prop);trace("video:"+std::string(prop)+":"+(value?value:"unavailable"));mpv_free(value);}}
    if(!episodes[index].contains("localPath")&&prefetchEnabled&&loaded&&!errorState&&!warmScheduled&&duration-position>12&&position>2&&index+1<(int)episodes.size()){
      double buffered=0;mpv_get_property(mpv,"demuxer-cache-duration",MPV_FORMAT_DOUBLE,&buffered);
      if(buffered>=10){auto flag=alive;auto generation=warmGeneration;const auto token=policy.generation;const auto id=episodes[index+1].at("id").get<std::string>();const std::string tier;
        warmScheduled=apiBackgroundTask([flag,generation,token,id,tier]{if(!*flag||generation->load()!=token)return;try{const auto r=backend->call("prefetch",Json::array({id,480,tier}));trace(r.value("ready",false)?"prefetch:ready":"prefetch:deferred");}catch(...){trace("prefetch:deferred");}});
      }
    }
    // Compose the video as a NanoVG image. Mixing immediate mpv output with
    // deferred UI draws on the window framebuffer can erase an otherwise
    // correctly decoded frame, depending on the active overlay/GL state.
    nvgEndFrame(vg);GLint target=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&target);
    const int physicalWidth=brls::Application::windowWidth,physicalHeight=brls::Application::windowHeight;
    const bool surfaceReady=renderer&&!failed&&videoSurface.ensure(vg,physicalWidth,physicalHeight);
    if(surfaceReady){
      glBindFramebuffer(GL_FRAMEBUFFER,videoSurface.target());glViewport(0,0,physicalWidth,physicalHeight);
      glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
      mpv_opengl_fbo surface{(int)videoSurface.target(),physicalWidth,physicalHeight,GL_RGBA8};int flip=1;
      mpv_render_param params[]={{MPV_RENDER_PARAM_OPENGL_FBO,&surface},{MPV_RENDER_PARAM_FLIP_Y,&flip},{MPV_RENDER_PARAM_INVALID,nullptr}};
      mpv_render_context_render(renderer,params);
      glBindFramebuffer(GL_FRAMEBUFFER,videoSurface.target());glDisable(GL_SCISSOR_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
      mpv_render_context_report_swap(renderer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,target);glViewport(0,0,physicalWidth,physicalHeight);
    const auto factor=brls::Application::getPlatform()->getVideoContext()->getScaleFactor();nvgBeginFrame(vg,physicalWidth,physicalHeight,factor);nvgScale(vg,brls::Application::windowScale,brls::Application::windowScale);
    if(surfaceReady)videoSurface.paint(vg,x,y,w,h);
    nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,22);nvgFillColor(vg,nvgRGBA(255,255,255,240));nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
    const std::string title=series.value("title",std::string())+" · 第 "+std::to_string(index+1)+" 集";nvgTextBox(vg,28,20,1224,title.c_str(),nullptr);if(!status.empty())nvgTextBox(vg,60,180,1160,status.c_str(),nullptr);
    timeline->update(position,duration,loaded&&!errorState&&!failed);if(holdApplied){nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_TOP);nvgText(vg,640,90,"2倍速播放中 · 松开恢复",nullptr);}
    if(menuVisible){const double rates[]={0.75,1,1.25,1.5,2};const std::string text="第 "+std::to_string(index+1)+" / "+std::to_string(episodes.size())+" 集  ·  "+clockText(position)+" / "+clockText(duration)+"  ·  "+(errorState?"播放失败":!loaded?"连接中":paused?"已暂停":"播放中")+"  ·  连播"+(policy.continuous?"开":"关")+"  ·  倍速 "+std::to_string(rates[speedIndex]).substr(0,4)+"  ·  清晰度 "+selectedQuality+"  ·  方向 "+std::to_string(rotation)+"°";if(text!=lastProgress){lastProgress=text;progress->setText(text);}}else{nvgFontSize(vg,16);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_BOTTOM);nvgText(vg,28,brls::Application::contentHeight-4,"点屏幕 / X 菜单  ·  长按画面 2倍速  ·  A 暂停  ·  L/R 选集  ·  ZR 切屏  ·  B 返回",nullptr);}
    brls::Box::draw(vg,x,y,w,h,style,ctx);
  }
};

static void showDetail(const Json& series);
static brls::View* posterTile(const Json& row,float width=180);
static void addGrid(brls::Box* list,const std::vector<brls::View*>& tiles,size_t columns,float gap,float rowGap){brls::Box* row=nullptr;for(size_t i=0;i<tiles.size();i++){if(i%columns==0){row=new brls::Box(brls::Axis::ROW);row->setMarginBottom(rowGap);list->addView(row);}auto* tile=tiles[i];if(i%columns!=columns-1)tile->setMarginRight(gap);row->addView(tile);if(i>=columns)tile->setCustomNavigationRoute(brls::FocusDirection::UP,tiles[i-columns]);const size_t nextRow=(i/columns+1)*columns;if(nextRow<tiles.size())tile->setCustomNavigationRoute(brls::FocusDirection::DOWN,tiles[std::min(i+columns,tiles.size()-1)]);}}
class Page:public brls::Box {
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);brls::Box* list;
public:
  Page(const std::string& method,const Json& args):brls::Box(brls::Axis::COLUMN){setBackgroundColor(nvgRGB(16,17,21));this->setGrow(1);list=new brls::Box(brls::Axis::COLUMN);list->setPadding(20,50,30,50);auto* label=new brls::Label();label->setText("正在加载…");list->addView(label);auto* scroll=new brls::ScrollingFrame();scroll->setGrow(1);scroll->setContentView(list);this->addView(scroll);back(this);auto flag=alive;
    apiTask([this,flag,method,args]{Json response;bool ok=false;try{response=backend->call(method,args);ok=true;}catch(...){}brls::sync([this,flag,ok,response,method,args]{if(!*flag)return;list->clearViews();if(!ok){list->addView(cell("内容源或网络暂不可用","按 B 返回后重试",[]{}));return;}auto* heading=new brls::Label();heading->setFontSize(25);heading->setTextColor(nvgRGB(242,241,246));heading->setText(method=="search"?"搜索结果":args[1]=="comic"?"人气漫剧":"热播短剧");heading->setMarginBottom(18);list->addView(heading);std::vector<brls::View*> tiles;for(const auto& row:response["items"]){if(!row.is_object()||!row.contains("id")||!row.contains("title"))continue;tiles.push_back(posterTile(row));}addGrid(list,tiles,6,16,20);if(tiles.empty())list->addView(cell("暂无内容","按 B 返回",[]{}));else brls::Application::giveFocus(tiles.front());if(response.value("hasMore",false)){list->addView(cell("下一页","继续加载",[method,args]{Json next=args;const int at=method=="home"?0:1;next[at]=next[at].get<int>()+1;brls::Application::pushActivity(new brls::Activity(new Page(method,next)));}));}});});
  }
  ~Page(){*alive=false;}
};
static void showDetail(const Json& series){auto* list=new brls::Box(brls::Axis::COLUMN);auto* title=new brls::Label();title->setText("正在加载分集…");list->addView(title);auto* f=frame(list,series.value("title",std::string()));brls::Application::pushActivity(new brls::Activity(f));
  // The frame owns the flag; work never touches a destroyed view.
  auto flag=f->alive;
  apiTask([list,flag,series]{Json detail;bool ok=false;try{detail=backend->call("detail",Json::array({series.at("id")}));ok=true;}catch(...){}brls::sync([list,flag,ok,detail]{if(!*flag)return;list->clearViews();if(!ok){list->addView(cell("分集暂不可用","按 B 返回",[]{}));return;}
    list->addView(posterCell(detail,[]{}));
    list->addView(cell("收藏 / 取消收藏","保存在当前主机",[detail]{auto& rows=library["favorites"];auto it=std::find_if(rows.begin(),rows.end(),[&](const Json& row){return row.value("id",std::string())==detail.value("id",std::string());});if(it==rows.end()){Json row=detail;row.erase("episodes");rows.push_back(row);message("已收藏");}else{rows.erase(it);message("已取消收藏");}save();}));
    auto sharedDetail=std::make_shared<const Json>(detail);
    const size_t total=detail["episodes"].size(),groups=episodeGroupCount(total);
    auto* label=new brls::Label();label->setFontSize(24);label->setMarginTop(20);label->setMarginBottom(16);list->addView(label);
    auto ranges=std::make_shared<std::vector<brls::View*>>();
    auto* grid=new brls::Box(brls::Axis::COLUMN);
    auto render=[grid,label,sharedDetail,total,ranges](size_t group){
      const auto bounds=episodeGroupBounds(total,group);
      label->setText("选择分集 · 共 "+std::to_string(total)+" 集"+(total>100?" · 当前 "+std::to_string(bounds.first+1)+"-"+std::to_string(bounds.second)+" 集":""));
      grid->clearViews();std::vector<brls::View*> buttons;
      for(size_t i=bounds.first;i<bounds.second;i++){
        auto* button=new ClickableBox(brls::Axis::COLUMN);button->setWidth(106);button->setHeight(56);button->setPadding(12);button->setBackgroundColor(nvgRGB(35,38,49));
        auto* number=new brls::Label();number->setText(std::to_string(i+1));number->setFontSize(22);number->setHorizontalAlign(brls::HorizontalAlign::CENTER);button->addView(number);
        button->registerAction("下载本集",brls::BUTTON_X,[sharedDetail,i](brls::View*){startDownload(*sharedDetail,int(i));return true;});button->registerClickAction([sharedDetail,i](brls::View*){brls::Application::pushActivity(new brls::Activity(new Player(*sharedDetail,(int)i)));return true;});buttons.push_back(button);
      }
      addGrid(grid,buttons,10,10,12);
      for(size_t i=0;i<ranges->size();i++){(*ranges)[i]->setBackgroundColor(i==group?nvgRGB(180,50,70):nvgRGB(35,38,49));if(!buttons.empty())(*ranges)[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN,buttons.front());}
      if(group<ranges->size())for(size_t i=0;i<std::min<size_t>(10,buttons.size());i++)buttons[i]->setCustomNavigationRoute(brls::FocusDirection::UP,(*ranges)[group]);
      if(!buttons.empty())brls::Application::giveFocus(buttons.front());
    };
    if(groups>1){
      auto* scroll=new brls::HScrollingFrame();scroll->setHeight(72);auto* row=new brls::Box(brls::Axis::ROW);
      for(size_t group=0;group<groups;group++){
        const auto bounds=episodeGroupBounds(total,group);auto* button=new ClickableBox(brls::Axis::COLUMN);button->setWidth(150);button->setHeight(56);button->setPadding(12);button->setMarginRight(12);button->setShrink(0);
        auto* text=new brls::Label();text->setText(std::to_string(bounds.first+1)+"-"+std::to_string(bounds.second)+" 集");text->setFontSize(18);text->setHorizontalAlign(brls::HorizontalAlign::CENTER);button->addView(text);
        button->registerClickAction([render,group](brls::View*){render(group);return true;});row->addView(button);ranges->push_back(button);
        if(group){button->setCustomNavigationRoute(brls::FocusDirection::LEFT,(*ranges)[group-1]);(*ranges)[group-1]->setCustomNavigationRoute(brls::FocusDirection::RIGHT,button);}
      }
      scroll->setContentView(row);list->addView(scroll);
    }
    list->addView(grid);render(0);

  });});
}
static void openSearch(){SwkbdConfig keyboard;if(R_FAILED(swkbdCreate(&keyboard,0)))return;swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetStringLenMax(&keyboard,80);swkbdConfigSetGuideText(&keyboard,"输入剧名");char text[512]={};Result result=swkbdShow(&keyboard,text,sizeof(text));swkbdClose(&keyboard);if(R_SUCCEEDED(result)&&text[0])brls::Application::pushActivity(new brls::Activity(new Page("search",Json::array({std::string(text),1,"short"}))));}
static void openLibrary(bool favorites){auto* box=new brls::Box(brls::Axis::COLUMN);if(favorites){for(const auto& row:library["favorites"])box->addView(posterCell(row,[row]{showDetail(row);}));}else{for(const auto& row:library["history"]){const auto series=row.value("series",Json::object());box->addView(posterCell(series,[series]{showDetail(series);}));}}brls::Application::pushActivity(new brls::Activity(frame(box,favorites?"我的收藏":"最近观看")));}
static brls::View* posterTile(const Json& row,float width){
  constexpr float titleHeight=56,noteHeight=24;
  const float coverHeight=width*1.5f;
  auto* tile=new ClickableBox(brls::Axis::COLUMN);
  tile->setWidth(width);tile->setHeight(coverHeight+12+titleHeight+6+noteHeight);
  tile->setPadding(0);tile->setShrink(0);tile->setCornerRadius(14);tile->setHideHighlightBackground(true);tile->setHighlightCornerRadius(14);
  tile->setBackgroundColor(nvgRGBA(0,0,0,0));
  auto* cover=new PosterImage(row.value("cover",std::string()),width,coverHeight,row.value("badge",std::string()));
  cover->setShrink(0);cover->setCornerRadius(14);cover->setScalingType(brls::ImageScalingType::FILL);tile->addView(cover);
  auto* title=new brls::Label();title->setText(row.value("title",std::string("红果短剧")));
  title->setWidth(width);title->setHeight(titleHeight);title->setShrink(0);title->setMarginTop(12);
  title->setFontSize(20);title->setLineHeight(1.25f);title->setSingleLine(false);title->setClipsToBounds(true);
  title->setTextColor(nvgRGB(242,241,246));title->setHorizontalAlign(brls::HorizontalAlign::LEFT);
  title->setVerticalAlign(brls::VerticalAlign::TOP);tile->addView(title);
  auto* note=new brls::Label();note->setText(row.value("tags",std::string()));
  note->setWidth(width);note->setHeight(noteHeight);note->setShrink(0);note->setMarginTop(6);
  note->setSingleLine(true);note->setFontSize(16);note->setTextColor(nvgRGB(155,153,165));
  note->setHorizontalAlign(brls::HorizontalAlign::LEFT);note->setVerticalAlign(brls::VerticalAlign::TOP);tile->addView(note);
  tile->registerClickAction([row](brls::View*){showDetail(row);return true;});
  tile->registerAction("完整剧名",brls::BUTTON_X,[row](brls::View*){message(row.value("title",std::string()));return true;});return tile;
}

static brls::Label* sectionTitle(const std::string& text){auto* label=new brls::Label();label->setText(text);label->setFontSize(25);label->setTextColor(nvgRGB(255,255,255));label->setHorizontalAlign(brls::HorizontalAlign::LEFT);label->setMarginTop(20);label->setMarginBottom(10);return label;}
static void startDownload(const Json& detail,int index){
  if(!downloads||index<0||index>=(int)detail.at("episodes").size())return;
  const auto episode=detail.at("episodes").at(index);const auto id=episode.at("id").get<std::string>();
  const std::string title=detail.value("title",std::string())+" · 第 "+std::to_string(index+1)+" 集";
  if(!library.contains("downloads"))library["downloads"]=Json::array();for(const auto& row:library["downloads"])if(row.value("id",std::string())==id){message("本集已下载，可在下载页播放");return;}
  message(downloads->start(id,title,detail.value("id",std::string()),index+1)?"已加入后台下载，前台播放优先。可在下载页查看进度或取消。":"无法加入下载，请检查 SD 卡或已有任务");
}
static void openDownloads(){
  auto* root=new brls::Box(brls::Axis::COLUMN);auto* list=new brls::Box(brls::Axis::COLUMN);auto* page=frame(root,"下载 · 已完成项目可离线播放");back(page);auto flag=page->alive;
  auto render=std::make_shared<std::function<void()>>();
  auto* refresh=cell("刷新下载进度","进行中的任务可点击取消",[render]{if(*render)(*render)();});root->addView(refresh);root->addView(list);
  *render=[list,flag,refresh]{if(!*flag)return;brls::Application::giveFocus(refresh);auto states=downloads?downloads->snapshot():std::vector<DownloadStatus>{};list->clearViews();
    if(library.contains("downloads"))for(const auto& row:library["downloads"]){list->addView(cell(row.value("title",std::string("已下载视频")),"已完成 · 点击离线播放",[row]{Json detail={{"id",row.value("seriesId",row.at("id").get<std::string>())},{"title",row.at("title")},{"episodes",Json::array({Json{{"id",row.at("id")},{"localPath",row.at("path")},{"number",row.value("number",1)},{"title",row.at("title")}}})}};brls::Application::pushActivity(new brls::Activity(new Player(detail,0)));}));}
    for(const auto& state:states)if(state.status!="已完成"){const auto progress=state.total?std::to_string(state.received*100/state.total)+"%":"等待片源";list->addView(cell(state.title,state.status+" · "+progress,[id=state.id]{downloads->cancel(id);message("已请求取消，当前分段结束后停止");}));}
    if(states.empty()&&(!library.contains("downloads")||library["downloads"].empty()))list->addView(cell("暂无下载","分集按钮按 X，或在播放器菜单选择下载本集",[]{}));
  };(*render)();brls::Application::pushActivity(new brls::Activity(page));
}

class NavigationIcon:public brls::View{
  int kind;bool active=false;
public:
  explicit NavigationIcon(int kind):kind(kind){setHeight(32);setWidth(96);setShrink(0);}
  void setActive(bool value){active=value;}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style,brls::FrameContext*)override{
    const float cx=x+w/2,cy=y+h/2;nvgStrokeColor(vg,active?nvgRGB(255,121,101):nvgRGB(169,167,179));nvgFillColor(vg,active?nvgRGB(255,121,101):nvgRGB(169,167,179));nvgStrokeWidth(vg,2.5f);nvgBeginPath(vg);
    if(kind==0){nvgCircle(vg,cx,cy,13);nvgStroke(vg);nvgBeginPath(vg);nvgMoveTo(vg,cx+6,cy-6);nvgLineTo(vg,cx+2,cy+2);nvgLineTo(vg,cx-6,cy+6);nvgLineTo(vg,cx-2,cy-2);nvgClosePath(vg);nvgFill(vg);}
    else if(kind==1){nvgMoveTo(vg,cx-9,cy+13);nvgLineTo(vg,cx-9,cy-12);nvgLineTo(vg,cx+9,cy-12);nvgLineTo(vg,cx+9,cy+13);nvgLineTo(vg,cx,cy+6);nvgClosePath(vg);nvgStroke(vg);}
    else if(kind==3){nvgMoveTo(vg,cx,cy-12);nvgLineTo(vg,cx,cy+6);nvgLineTo(vg,cx-7,cy-1);nvgMoveTo(vg,cx,cy+6);nvgLineTo(vg,cx+7,cy-1);nvgMoveTo(vg,cx-11,cy+11);nvgLineTo(vg,cx+11,cy+11);nvgStroke(vg);}
    else{nvgArc(vg,cx,cy,13,-2.7f,2.7f,NVG_CW);nvgStroke(vg);nvgBeginPath(vg);nvgMoveTo(vg,cx,cy-8);nvgLineTo(vg,cx,cy);nvgLineTo(vg,cx+7,cy+4);nvgStroke(vg);nvgBeginPath(vg);nvgMoveTo(vg,cx-16,cy-5);nvgLineTo(vg,cx-11,cy-2);nvgLineTo(vg,cx-7,cy-7);nvgStroke(vg);}
  }
};
class HomeDashboard:public brls::Box{
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
  brls::Box* content=nullptr; brls::HScrollingFrame* filters=nullptr;
  std::vector<ClickableBox*> chips,rail;
  std::vector<brls::Label*> railLabels;std::vector<NavigationIcon*> railIcons;
  std::string category="short";int section=0,page=1;size_t selectedTab=0;uint64_t generation=0;
  std::vector<brls::View*> tiles;
  static brls::Label* label(const std::string& text,int size,NVGcolor color){auto* view=new brls::Label();view->setText(text);view->setFontSize(size);view->setTextColor(color);view->setHorizontalAlign(brls::HorizontalAlign::LEFT);return view;}
  static ClickableBox* button(const std::string& text,float width,std::function<void()> fn){auto* box=new ClickableBox(brls::Axis::COLUMN);box->setWidth(width);box->setHeight(46);box->setShrink(0);box->setHideHighlightBackground(true);box->setHighlightCornerRadius(12);box->setPadding(10);box->setCornerRadius(12);auto* textView=label(text,18,nvgRGB(223,221,229));textView->setHorizontalAlign(brls::HorizontalAlign::CENTER);box->addView(textView);box->registerClickAction([fn](brls::View*){fn();return true;});return box;}
  void selectRail(int selected){section=selected;for(size_t i=0;i<rail.size();i++){rail[i]->setBackgroundColor(i==size_t(section)?nvgRGB(44,29,29):nvgRGBA(0,0,0,0));railLabels[i]->setTextColor(i==size_t(section)?nvgRGB(255,121,101):nvgRGB(169,167,179));railIcons[i]->setActive(i==size_t(section));}filters->setVisibility(section==0?brls::Visibility::VISIBLE:brls::Visibility::GONE);}
  void wireNavigation(){for(size_t i=0;i<tiles.size();i++){if(i<4&&!chips.empty()&&section==0)tiles[i]->setCustomNavigationRoute(brls::FocusDirection::UP,chips[selectedTab]);if(i%4==0)tiles[i]->setCustomNavigationRoute(brls::FocusDirection::LEFT,rail[section]);}if(!tiles.empty()){rail[section]->setCustomNavigationRoute(brls::FocusDirection::RIGHT,tiles.front());for(auto* chip:chips)chip->setCustomNavigationRoute(brls::FocusDirection::DOWN,tiles.front());}}
  void disconnectGrid(){for(auto* chip:chips)chip->setCustomNavigationRoute(brls::FocusDirection::DOWN,chip);for(auto* item:rail)item->setCustomNavigationRoute(brls::FocusDirection::RIGHT,section==0?static_cast<brls::View*>(chips[selectedTab]):static_cast<brls::View*>(item));}
  void render(const Json& items){disconnectGrid();content->clearViews();tiles.clear();for(const auto& item:items)if(item.is_object()&&item.contains("id")&&item.contains("title"))tiles.push_back(posterTile(item,238));addGrid(content,tiles,4,24,24);wireNavigation();}
  void loadCatalog(){
    selectRail(0);const auto token=++generation;const int requested=page;const auto type=category;
    // Focus must leave children before replacing their owning grid.
    brls::Application::giveFocus(chips[selectedTab]);
    disconnectGrid();content->clearViews();tiles.clear();content->addView(label("正在加载剧集…",20,nvgRGB(168,165,178)));
    auto flag=alive;apiTask([this,flag,token,requested,type]{Json result;bool ok=false;try{result=backend->call("home",Json::array({requested,type}));ok=true;}catch(...){}brls::sync([this,flag,token,requested,ok,result=std::move(result)]{
      if(!*flag||token!=generation)return;
      if(!ok){content->clearViews();tiles.clear();content->addView(label("剧集暂时无法加载，请按 Y 刷新重试",20,nvgRGB(182,178,191)));return;}
      Json items=result.value("items",Json::array());
      render(items);
      if(tiles.empty())content->addView(label("暂无内容，请切换分类",20,nvgRGB(182,178,191)));
      if(result.value("hasMore",false)){auto* more=button("下一页",220,[this,requested]{page=requested+1;loadCatalog();});more->setBackgroundColor(nvgRGB(32,32,39));content->addView(more);}
      trace("home:grid_ready");
    });});
  }
  void showSaved(int which){brls::Application::giveFocus(rail[which]);++generation;selectRail(which);Json items=Json::array();if(which==1)items=library["favorites"];else for(const auto& row:library["history"])items.push_back(row.value("series",Json::object()));render(items);if(tiles.empty())content->addView(label(which==1?"还没有追剧，进入剧集详情即可收藏":"暂无观看记录，选一部好剧开始吧",20,nvgRGB(182,178,191)));}
  void refresh(){if(section==0){page=1;loadCatalog();}else showSaved(section);}
  void clearCache(){auto flag=alive;apiTask([flag]{bool ok=false;try{backend->call("clearCache",Json::array());ok=true;}catch(...){}brls::sync([flag,ok]{if(*flag)message(ok?"视频缓存已清除，收藏和观看记录已保留":"清理失败，请重试");});});}
public:
  HomeDashboard():brls::Box(brls::Axis::COLUMN){
    setGrow(1);setPadding(20,28,16,28);setBackgroundColor(nvgRGB(16,17,21));
    auto* toolbar=new brls::Box(brls::Axis::ROW);toolbar->setHeight(68);toolbar->setShrink(0);toolbar->setMarginBottom(12);
    auto* brand=label("红果",32,nvgRGB(246,244,249));brand->setGrow(1);brand->setMarginTop(6);toolbar->addView(brand);
    auto* search=button("搜索",92,openSearch);toolbar->addView(search);toolbar->addView(button("刷新",92,[this]{refresh();}));toolbar->addView(button("清缓存",112,[this]{clearCache();}));addView(toolbar);
    auto* body=new brls::Box(brls::Axis::ROW);body->setGrow(1);body->setShrink(1);addView(body);
    auto* sidebar=new brls::Box(brls::Axis::COLUMN);sidebar->setWidth(116);sidebar->setShrink(0);sidebar->setMarginRight(12);body->addView(sidebar);auto* divider=new brls::Box(brls::Axis::COLUMN);divider->setWidth(1);divider->setShrink(0);divider->setMarginRight(19);divider->setBackgroundColor(nvgRGB(47,48,56));body->addView(divider);
    const std::vector<std::string> names={"发现","追剧","最近观看","下载"};
    for(size_t i=0;i<names.size();i++){auto* item=new ClickableBox(brls::Axis::COLUMN);item->setWidth(116);item->setHideHighlightBackground(true);item->setHighlightCornerRadius(14);item->setHeight(108);item->setPadding(12,10,12,10);item->setMarginBottom(12);item->setCornerRadius(14);auto* icon=new NavigationIcon(int(i));item->addView(icon);railIcons.push_back(icon);auto* textView=label(names[i],i==2?18:22,nvgRGB(169,167,179));textView->setHorizontalAlign(brls::HorizontalAlign::CENTER);textView->setMarginTop(8);item->addView(textView);rail.push_back(item);railLabels.push_back(textView);item->registerClickAction([this,i](brls::View*){if(i==0){page=1;loadCatalog();}else if(i==3)openDownloads();else showSaved(int(i));return true;});sidebar->addView(item);}
    auto* main=new brls::Box(brls::Axis::COLUMN);main->setGrow(1);main->setShrink(1);body->addView(main);
    filters=new brls::HScrollingFrame();filters->setHeight(64);filters->setShrink(0);auto* categories=new brls::Box(brls::Axis::ROW);
    const std::vector<std::pair<std::string,std::string>> tabs={{"全部","short"},{"最新","latest"},{"真人剧","human"},{"漫剧","comic"},{"AI 剧","ai"},{"剧情","drama"},{"古装","ancient"},{"喜剧","comedy"},{"奇幻","fantasy"}};
    for(size_t i=0;i<tabs.size();i++){const auto type=tabs[i].second;auto* chip=button(tabs[i].first,112,[this,type,i]{category=type;selectedTab=i;page=1;for(size_t n=0;n<chips.size();n++)chips[n]->setBackgroundColor(n==i?nvgRGB(99,66,60):nvgRGB(25,26,32));loadCatalog();});chip->setHeight(50);chip->setMarginRight(10);chip->setBackgroundColor(i==0?nvgRGB(99,66,60):nvgRGB(25,26,32));chips.push_back(chip);categories->addView(chip);}filters->setContentView(categories);main->addView(filters);
    auto* scroll=new brls::ScrollingFrame();scroll->setGrow(1);scroll->setShrink(1);content=new brls::Box(brls::Axis::COLUMN);content->setPadding(4,0,28,0);scroll->setContentView(content);main->addView(scroll);
    registerAction("搜索",brls::BUTTON_X,[](brls::View*){openSearch();return true;});registerAction("刷新",brls::BUTTON_Y,[this](brls::View*){refresh();return true;});
    loadCatalog();trace("startup:home_ready");
  }
#ifdef HONGGUO_PREVIEW
  void previewScreen(const std::string& screen){if(screen=="favorites")showSaved(1);else if(screen=="history")showSaved(2);else if(screen=="category"){category="comic";selectedTab=3;loadCatalog();}}
#endif
  ~HomeDashboard(){*alive=false;}
};
static brls::View* home(){return new HomeDashboard();}

int main(int,char**){curl_global_init(CURL_GLOBAL_DEFAULT);if(!brls::Application::init())return 1;brls::Application::createWindow("红果短剧");brls::Application::setGlobalQuit(true);brls::Theme::getLightTheme()=brls::Theme::getDarkTheme();for(auto* theme:{&brls::Theme::getLightTheme(),&brls::Theme::getDarkTheme()}){theme->addColor("brls/highlight/color1",nvgRGB(255,121,101));theme->addColor("brls/highlight/color2",nvgRGB(255,173,146));theme->addColor("brls/click_pulse",nvgRGBA(255,121,101,45));}load();
#ifdef HONGGUO_PREVIEW
  glfwSetWindowSize(glfwGetCurrentContext(),1280,720);if(!std::getenv("HONGGUO_LIVE")){library["favorites"]=previewItems();library["history"]=Json::array();for(const auto& row:previewItems())library["history"].push_back(Json{{"series",row}});library["downloads"]=Json::array({Json{{"id","1000"},{"seriesId","1000"},{"title","暮色回响 · 第 1 集"},{"number",1},{"path","build/preview-video.mp4"}}});}
#endif
resetTrace();trace("startup:ui_ready");
  try{if(!startWorkers())throw std::runtime_error("Worker initialization failed");backend=new Backend();mkdir("sdmc:/switch/hongguo-saf/downloads",0777);downloads=std::make_unique<DownloadQueue>("sdmc:/switch/hongguo-saf/downloads",[](const std::string& id,int64_t offset){auto result=backend->call("downloadSlice",Json::array({id,offset}));return DownloadChunk{result.at("total").get<int64_t>(),unhex(result.at("hex").get<std::string>())};},apiBackgroundTask,[](const DownloadStatus& item){brls::sync([item]{if(!library.contains("downloads"))library["downloads"]=Json::array();library["downloads"].push_back(Json{{"id",item.id},{"title",item.title},{"path",item.path},{"seriesId",item.seriesId},{"number",item.number}});save();});});trace("startup:backend_ready");const auto result=backend->call("selftest",Json::array());if(result.value("sm3",std::string())!="66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0")throw std::runtime_error("Self test");auto* dashboard=home();brls::Application::pushActivity(new brls::Activity(dashboard));
#ifdef HONGGUO_PREVIEW
    const std::string screen=std::getenv("HONGGUO_PREVIEW_SCREEN")?std::getenv("HONGGUO_PREVIEW_SCREEN"):"home";
    const bool live=std::getenv("HONGGUO_LIVE");
    if(live&&(screen=="player"||screen=="player-menu")){auto detail=backend->call("detail",Json::array({"7686713195246930968"}));auto* player=new Player(detail,30);brls::Application::pushActivity(new brls::Activity(player));if(screen=="player-menu")player->previewMenu();}
    else if(live&&screen=="detail")showDetail(Json{{"id","7686713195246930968"},{"title","首辅娇娘"}});
    else if(screen=="search")brls::Application::pushActivity(new brls::Activity(new Page("search",Json::array({"演示搜索",1,"short"}))));
    else if(screen=="detail")showDetail(previewItems()[0]);
    else if(screen=="downloads")openDownloads();
    else if(screen=="player"||screen=="player-menu"){auto* player=new Player(previewDetail("1000"),0);brls::Application::pushActivity(new brls::Activity(player));if(screen=="player-menu")player->previewMenu();}
    else static_cast<HomeDashboard*>(dashboard)->previewScreen(screen);
#endif
  }
  catch(...){auto* text=new brls::Label();text->setText("应用初始化失败，请确认安装包完整。");brls::Application::pushActivity(new brls::Activity(text));}
  auto previewStarted=std::chrono::steady_clock::now();while(brls::Application::mainLoop()){
#ifdef HONGGUO_PREVIEW
    if(std::chrono::steady_clock::now()-previewStarted>std::chrono::seconds(std::getenv("HONGGUO_LIVE")?180:20))break;
#endif
  }trace("shutdown:begin");stopWorkers();save();return 0;
}
