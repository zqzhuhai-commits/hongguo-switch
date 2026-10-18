// SPDX-License-Identifier: GPL-3.0-only
#include "poster.hpp"
#include "heic.hpp"
#include <memory>
#include <algorithm>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
PosterPixels decodeModernPoster(const std::vector<unsigned char>& bytes) {
  PosterPixels result;
  try {
    heic::Image image;AVCodecID codec;
    if(heic::matches(bytes)){image=heic::extract(bytes);codec=AV_CODEC_ID_HEVC;}
    else if(bytes.size()>=12&&std::string(bytes.begin(),bytes.begin()+4)=="RIFF"&&std::string(bytes.begin()+8,bytes.begin()+12)=="WEBP"){image.annexB=bytes;codec=AV_CODEC_ID_WEBP;}
    else return result;
    const AVCodec* decoder=avcodec_find_decoder(codec);if(!decoder)return result;
    auto releaseContext=[](AVCodecContext* p){avcodec_free_context(&p);};
    auto releaseFrame=[](AVFrame* p){av_frame_free(&p);};auto releasePacket=[](AVPacket* p){av_packet_free(&p);};
    std::unique_ptr<AVCodecContext,decltype(releaseContext)> context(avcodec_alloc_context3(decoder),releaseContext);
    std::unique_ptr<AVFrame,decltype(releaseFrame)> frame(av_frame_alloc(),releaseFrame);
    std::unique_ptr<AVPacket,decltype(releasePacket)> packet(av_packet_alloc(),releasePacket);
    if(!context||!frame||!packet)return result;
    context->thread_count=1;context->max_pixels=6*1024*1024;
    if(avcodec_open2(context.get(),decoder,nullptr)<0||av_new_packet(packet.get(),image.annexB.size())<0)return result;
    std::copy(image.annexB.begin(),image.annexB.end(),packet->data);
    if(avcodec_send_packet(context.get(),packet.get())<0)return result;
    int status=avcodec_receive_frame(context.get(),frame.get());
    if(status==AVERROR(EAGAIN)){if(avcodec_send_packet(context.get(),nullptr)<0)return result;status=avcodec_receive_frame(context.get(),frame.get());}
    if(status<0||frame->width<1||frame->height<1||(int64_t)frame->width*frame->height>6*1024*1024)return result;
    int w=256,h=std::max(1,std::min(512,256*frame->height/frame->width));
    std::unique_ptr<SwsContext,decltype(&sws_freeContext)> scaler(sws_getContext(frame->width,frame->height,(AVPixelFormat)frame->format,w,h,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr),sws_freeContext);
    if(!scaler)return result;std::vector<unsigned char> pixels(w*h*4);uint8_t* output[]={pixels.data(),nullptr,nullptr,nullptr};int strides[]={w*4,0,0,0};
    if(sws_scale(scaler.get(),frame->data,frame->linesize,0,frame->height,output,strides)!=h)return result;
    for(int transform:image.transforms){int nw=(transform==1||transform==3)?h:w,nh=(transform==1||transform==3)?w:h;std::vector<unsigned char> next(nw*nh*4);for(int y=0;y<h;y++)for(int x=0;x<w;x++){int dx=x,dy=y;switch(transform){case 1:dx=y;dy=w-1-x;break;case 2:dx=w-1-x;dy=h-1-y;break;case 3:dx=h-1-y;dy=x;break;case 4:dx=w-1-x;break;case 5:dy=h-1-y;break;}std::copy_n(pixels.data()+(y*w+x)*4,4,next.data()+(dy*nw+dx)*4);}pixels.swap(next);w=nw;h=nh;}
    result.width=w;result.height=h;result.rgba=std::move(pixels);
  }catch(...){result.rgba.clear();}
  return result;
}
