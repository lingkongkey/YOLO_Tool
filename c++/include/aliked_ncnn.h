#pragma once

#include <string>

#include <opencv2/opencv.hpp>
#include <net.h>


long MyFindPicAi(char* bmp_data, int bmp_size, const std::string& pic_name, double sim, long dir, int x1, int y1, long& intX, long& intY);
const std::string MyFindPicExAi(char* bmp_data, int bmp_size, const std::string& pic_name, double sim, long dir, int x1, int y1);