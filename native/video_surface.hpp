// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// All GL and NanoVG declarations are provided by the including UI translation unit.
#if defined(USE_GLES3)
extern "C" GLuint nvglImageHandleGLES3(NVGcontext*,int);
#elif defined(USE_GLES2)
extern "C" GLuint nvglImageHandleGLES2(NVGcontext*,int);
#elif defined(USE_GL2)
extern "C" GLuint nvglImageHandleGL2(NVGcontext*,int);
#else
extern "C" GLuint nvglImageHandleGL3(NVGcontext*,int);
#endif
class VideoSurface {
  NVGcontext* context=nullptr;int image=0,width=0,height=0;GLuint framebuffer=0;bool frameValid=false;
public:
  ~VideoSurface(){clear();}
  void clear(){if(framebuffer)glDeleteFramebuffers(1,&framebuffer);if(image&&context)nvgDeleteImage(context,image);framebuffer=0;image=0;width=height=0;frameValid=false;}
  bool ensure(NVGcontext* vg,int w,int h){
    if(image&&width==w&&height==h)return true;
    GLint previous=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);clear();context=vg;
    if(w<1||h<1||w>3840||h>2160)return false;
    image=nvgCreateImageRGBA(vg,w,h,NVG_IMAGE_FLIPY,nullptr);if(!image)return false;
#if defined(USE_GLES3)
    const GLuint texture=nvglImageHandleGLES3(vg,image);
#elif defined(USE_GLES2)
    const GLuint texture=nvglImageHandleGLES2(vg,image);
#elif defined(USE_GL2)
    const GLuint texture=nvglImageHandleGL2(vg,image);
#else
    const GLuint texture=nvglImageHandleGL3(vg,image);
#endif
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    const bool valid=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER,previous);
    if(!valid){clear();return false;}width=w;height=h;return true;
  }
  GLuint target()const{return framebuffer;}
  bool hasFrame()const{return frameValid;}
  void invalidate(){frameValid=false;}
  void frameRendered(){frameValid=true;}
  void paint(NVGcontext* vg,float x,float y,float w,float h)const{
    if(!image)return;nvgBeginPath(vg);nvgRect(vg,x,y,w,h);nvgFillPaint(vg,nvgImagePattern(vg,x,y,w,h,0,image,1));nvgFill(vg);
  }
};
