// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <string>
#include <vector>
struct PosterPixels { int width=256,height=384; std::vector<unsigned char> rgba; };
PosterPixels fetchPoster(const std::string& url,const std::atomic<bool>& alive);
PosterPixels decodeModernPoster(const std::vector<unsigned char>& bytes);
