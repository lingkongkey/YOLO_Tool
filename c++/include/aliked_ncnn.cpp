// Copyright 2020 Tencent
// SPDX-License-Identifier: BSD-3-Clause

#include "net.h"

#if defined(USE_NCNN_SIMPLEOCV)
#include "simpleocv.h"
#else
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#endif
#include <stdlib.h>
#include <float.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <sstream>
#include "../../../../resource1.h"

// 复用原有Object结构，扩展模板匹配专用字段
struct MatchTemplate
{
	cv::Mat bmp_img;
	uint8_t transparent_rgb[3];
	bool has_transparent = false;
};

struct Object
{
	cv::Rect_<float> rect;
	int label;
	float prob;
};

static inline float intersection_area(const Object& a, const Object& b)
{
	cv::Rect_<float> inter = a.rect & b.rect;
	return inter.area();
}

static void qsort_descent_inplace(std::vector<Object>& faceobjects, int left, int right)
{
	int i = left;
	int j = right;
	float p = faceobjects[(left + right) / 2].prob;

	while (i <= j)
	{
		while (faceobjects[i].prob > p)
			i++;
		while (faceobjects[j].prob < p)
			j--;
		if (i <= j)
		{
			std::swap(faceobjects[i], faceobjects[j]);
			i++;
			j--;
		}
	}
#pragma omp parallel sections
	{
#pragma omp section
		{
			if (left < j) qsort_descent_inplace(faceobjects, left, j);
		}
#pragma omp section
		{
			if (i < right) qsort_descent_inplace(faceobjects, i, right);
		}
	}
}

static void qsort_descent_inplace(std::vector<Object>& faceobjects)
{
	if (faceobjects.empty())
		return;
	qsort_descent_inplace(faceobjects, 0, faceobjects.size() - 1);
}

static void nms_sorted_bboxes(const std::vector<Object>& faceobjects, std::vector<int>& picked, float nms_threshold, bool agnostic = false)
{
	picked.clear();
	const int n = faceobjects.size();
	std::vector<float> areas(n);
	for (int i = 0; i < n; i++)
		areas[i] = faceobjects[i].rect.area();

	for (int i = 0; i < n; i++)
	{
		const Object& a = faceobjects[i];
		int keep = 1;
		for (int j = 0; j < (int)picked.size(); j++)
		{
			const Object& b = faceobjects[picked[j]];
			if (!agnostic && a.label != b.label)
				continue;
			float inter_area = intersection_area(a, b);
			float union_area = areas[i] + areas[picked[j]] - inter_area;
			if (inter_area / union_area > nms_threshold)
				keep = 0;
		}
		if (keep)
			picked.push_back(i);
	}
}

// 修复原示例代码的网格计算BUG
static void generate_proposals(const ncnn::Mat& cls_pred, const ncnn::Mat& dis_pred, int stride, const ncnn::Mat& in_pad, float prob_threshold, std::vector<Object>& objects)
{
	int num_grid_x = in_pad.w / stride;
	int num_grid_y = in_pad.h / stride;
	const int num_class = cls_pred.w;
	const int reg_max_1 = dis_pred.w / 4;

	for (int i = 0; i < num_grid_y; i++)
	{
		for (int j = 0; j < num_grid_x; j++)
		{
			const int idx = i * num_grid_x + j;
			const float* scores = cls_pred.row(idx);
			int label = -1;
			float score = -FLT_MAX;
			for (int k = 0; k < num_class; k++)
			{
				if (scores[k] > score)
				{
					label = k;
					score = scores[k];
				}
			}
			if (score >= prob_threshold)
			{
				ncnn::Mat bbox_pred(reg_max_1, 4, (void*)dis_pred.row(idx));
				// 替换原动态创建Softmax层的低效实现
				for (int k = 0; k < 4; k++) {
					float* rowptr = bbox_pred.row(k);
					float max_val = rowptr[0];
					for (int l = 1; l < reg_max_1; l++) max_val = std::max(max_val, rowptr[l]);
					float sum = 0.f;
					for (int l = 0; l < reg_max_1; l++) {
						rowptr[l] = exp(rowptr[l] - max_val);
						sum += rowptr[l];
					}
					for (int l = 0; l < reg_max_1; l++) rowptr[l] /= sum;
				}

				float pred_ltrb[4];
				for (int k = 0; k < 4; k++)
				{
					float dis = 0.f;
					const float* dis_after_sm = bbox_pred.row(k);
					for (int l = 0; l < reg_max_1; l++)
						dis += l * dis_after_sm[l];
					pred_ltrb[k] = dis * stride;
				}

				float pb_cx = (j + 0.5f) * stride;
				float pb_cy = (i + 0.5f) * stride;
				float x0 = pb_cx - pred_ltrb[0];
				float y0 = pb_cy - pred_ltrb[1];
				float x1 = pb_cx + pred_ltrb[2];
				float y1 = pb_cy + pred_ltrb[3];

				Object obj;
				obj.rect.x = x0;
				obj.rect.y = y0;
				obj.rect.width = x1 - x0;
				obj.rect.height = y1 - y0;
				obj.label = label;
				obj.prob = score;
				objects.push_back(obj);
			}
		}
	}
}

extern bool GetResourceData(int resId, const char* resType, uint8_t*& outData, size_t& outSize);

// 全局初始化一次ncnn模型，避免重复加载开销
static ncnn::Net* g_nanodet_net = nullptr;
static int init_ai_model_once()
{
	if (g_nanodet_net) return 0;
	g_nanodet_net = new ncnn::Net();
	g_nanodet_net->opt.use_vulkan_compute = true;

	uint8_t* param_data = nullptr;
	size_t param_size = 0;
	uint8_t* bin_data = nullptr;
	size_t bin_size = 0;

	GetResourceData(NCNN_PIC_P, "NCNN", param_data, param_size);
	if (g_nanodet_net->load_param_mem((const char*)param_data) != 0)
		return -1;
	GetResourceData(NCNN_PIC_B, "NCNN", bin_data, bin_size);
	if (g_nanodet_net->load_model_mem(bin_data) != 0)
		return -1;
	return 0;
}

// 预处理模板图，自动处理透明色规则
static MatchTemplate preprocess_bmp_template(const std::string& bmp_path)
{
	MatchTemplate tpl;
	tpl.bmp_img = cv::imread(bmp_path, 1);
	if (tpl.bmp_img.empty()) return tpl;

	// 提取四个顶点颜色
	cv::Point p1(0, 0);
	cv::Point p2(tpl.bmp_img.cols - 1, 0);
	cv::Point p3(0, tpl.bmp_img.rows - 1);
	cv::Point p4(tpl.bmp_img.cols - 1, tpl.bmp_img.rows - 1);

	cv::Vec3b c1 = tpl.bmp_img.at<cv::Vec3b>(p1);
	cv::Vec3b c2 = tpl.bmp_img.at<cv::Vec3b>(p2);
	cv::Vec3b c3 = tpl.bmp_img.at<cv::Vec3b>(p3);
	cv::Vec3b c4 = tpl.bmp_img.at<cv::Vec3b>(p4);

	// 四个顶点颜色完全一致则标记为透明色
	if (c1 == c2 && c2 == c3 && c3 == c4) {
		tpl.has_transparent = true;
		tpl.transparent_rgb[0] = c1[0];
		tpl.transparent_rgb[1] = c1[1];
		tpl.transparent_rgb[2] = c1[2];
	}
	return tpl;
}

long MyFindPicAi(char* bmp_data, int bmp_size, const std::string& pic_name, double sim, long dir, int x1, int y1, long& intX, long& intY)
{
	std::vector<uchar> buf(bmp_data, bmp_data + bmp_size);
	cv::Mat full_screen = cv::imdecode(buf, cv::IMREAD_COLOR);

	if (full_screen.empty()) {
		return -1;
	}
	cv::Mat& roi = full_screen;


	std::vector<std::string> tpl_paths;
	std::stringstream ss(pic_name);
	std::string path;
	while (std::getline(ss, path, '|')) {
		if (!path.empty()) {
			tpl_paths.push_back(path);
		}
	}

	if (tpl_paths.empty()) {
		return -1;
	}

	for (int tpl_idx = 0; tpl_idx < tpl_paths.size(); tpl_idx++)
	{
		MatchTemplate tpl = preprocess_bmp_template(tpl_paths[tpl_idx]);
		if (tpl.bmp_img.empty()) continue;

		if (tpl.bmp_img.cols > roi.cols || tpl.bmp_img.rows > roi.rows) {
			continue;
		}

		cv::Mat result;

		if (tpl.has_transparent) {
			cv::Mat mask;
			cv::inRange(tpl.bmp_img,
				cv::Scalar(tpl.transparent_rgb[0], tpl.transparent_rgb[1], tpl.transparent_rgb[2]),
				cv::Scalar(tpl.transparent_rgb[0], tpl.transparent_rgb[1], tpl.transparent_rgb[2]),
				mask);
			mask = 255 - mask;

			cv::matchTemplate(roi, tpl.bmp_img, result, cv::TM_CCOEFF_NORMED, mask);
		}
		else {
			cv::matchTemplate(roi, tpl.bmp_img, result, cv::TM_CCOEFF_NORMED);
		}

		double max_val = -1;
		cv::Point max_loc;

		switch (dir) {
		case 0:
			cv::minMaxLoc(result, NULL, &max_val, NULL, &max_loc);
			break;
		case 1: 
			cv::flip(result, result, 0);
			cv::minMaxLoc(result, NULL, &max_val, NULL, &max_loc);
			max_loc.y = result.rows - 1 - max_loc.y;
			break;
		case 2: 
			cv::flip(result, result, 1);
			cv::minMaxLoc(result, NULL, &max_val, NULL, &max_loc);
			max_loc.x = result.cols - 1 - max_loc.x;
			break;
		case 3: 
			cv::flip(result, result, -1);
			cv::minMaxLoc(result, NULL, &max_val, NULL, &max_loc);
			max_loc.x = result.cols - 1 - max_loc.x;
			max_loc.y = result.rows - 1 - max_loc.y;
			break;
		default:
			cv::minMaxLoc(result, NULL, &max_val, NULL, &max_loc);
			break;
		}


		if (max_val >= sim)
		{
			intX = x1 + max_loc.x;
			intY = y1 + max_loc.y;
			return tpl_idx;
		}
	}

	return -1;
}

const std::string MyFindPicExAi(char* bmp_data, int bmp_size, const std::string& pic_name, double sim, long dir, int x1, int y1)
{
	std::vector<uchar> buf(bmp_data, bmp_data + bmp_size);
	cv::Mat full_screen = cv::imdecode(buf, cv::IMREAD_COLOR);

	if (full_screen.empty())
	{
		return "";
	}
	cv::Mat roi = full_screen;

	std::vector<std::string> tpl_paths;
	std::stringstream ss(pic_name);
	std::string path;
	while (std::getline(ss, path, '|'))
	{
		if (!path.empty())
		{
			tpl_paths.push_back(path);
		}
	}

	if (tpl_paths.empty())
	{
		return "";
	}
	std::vector<std::string> resItems;
	for (int tpl_idx = 0; tpl_idx < tpl_paths.size(); tpl_idx++)
	{
		MatchTemplate tpl = preprocess_bmp_template(tpl_paths[tpl_idx]);
		if (tpl.bmp_img.empty()) continue;

		if (tpl.bmp_img.cols > roi.cols || tpl.bmp_img.rows > roi.rows) {
			continue;
		}

		cv::Mat result;

		if (tpl.has_transparent) {
			cv::Mat mask;
			cv::inRange(tpl.bmp_img,
				cv::Scalar(tpl.transparent_rgb[0], tpl.transparent_rgb[1], tpl.transparent_rgb[2]),
				cv::Scalar(tpl.transparent_rgb[0], tpl.transparent_rgb[1], tpl.transparent_rgb[2]),
				mask);
			mask = 255 - mask;

			cv::matchTemplate(roi, tpl.bmp_img, result, cv::TM_CCOEFF_NORMED, mask);
		}
		else {
			cv::matchTemplate(roi, tpl.bmp_img, result, cv::TM_CCOEFF_NORMED);
		}
		std::vector<cv::Point> matchPts;
		for (int y = 0; y < result.rows; y++)
		{
			for (int x = 0; x < result.cols; x++)
			{
				if (result.at<float>(y, x) >= sim)
				{
					matchPts.emplace_back(x, y);
				}
			}
		}
		auto sortFunc = [&](cv::Point a, cv::Point b) -> bool
		{
			switch (dir)
			{
			case 0: return (a.y < b.y) || (a.y == b.y && a.x < b.x);
			case 1: return (a.y > b.y) || (a.y == b.y && a.x < b.x);
			case 2: return (a.y < b.y) || (a.y == b.y && a.x > b.x);
			case 3: return (a.y > b.y) || (a.y == b.y && a.x > b.x);
			default: return (a.y < b.y) || (a.y == b.y && a.x < b.x);
			}
		};
		std::sort(matchPts.begin(), matchPts.end(), sortFunc);
		int maxCnt = 1500;
		int addCnt = 0;
		for (auto& pt : matchPts)
		{
			if (addCnt >= maxCnt) break;
			int fx = x1 + pt.x;
			int fy = y1 + pt.y;
			resItems.push_back(std::to_string(tpl_idx) + "," + std::to_string(fx) + "," + std::to_string(fy));
			addCnt++;
		}
	}
	std::string out;
	for (size_t i = 0; i < resItems.size(); i++)
	{
		if (i > 0) out += "|";
		out += resItems[i];
	}
	return out;
}


