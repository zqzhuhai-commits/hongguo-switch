// SPDX-License-Identifier: GPL-3.0-only
#include "poster.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char** argv){
  if(argc<2)return 2;
  for(int i=1;i<argc;i++){
    std::ifstream in(argv[i],std::ios::binary);std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),{}};
    auto image=decodeModernPoster(bytes);
    if(image.rgba.size()!=size_t(image.width*image.height*4)||image.rgba.empty())return 1;
    std::ofstream out(std::string(argv[i])+".ppm",std::ios::binary);out<<"P6\n"<<image.width<<" "<<image.height<<"\n255\n";
    for(size_t j=0;j<image.rgba.size();j+=4)out.write((char*)image.rgba.data()+j,3);
    std::cout<<"decoded cover "<<i<<" "<<image.width<<"x"<<image.height<<"\n";
  }
}
