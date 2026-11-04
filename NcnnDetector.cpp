// NcnnDetector.cpp: ncnn YOLO
//
#include "pch.h"
#include "NcnnDetector.h"


#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif


#include "net.h"
#include "layer.h"

static const cv::Scalar kColors[] = {
	cv::Scalar(255, 56, 56),   cv::Scalar(255, 157, 56),  cv::Scalar(255, 222, 56),
	cv::Scalar(151, 255, 56),  cv::Scalar(56, 255, 113),  cv::Scalar(56, 255, 213),
	cv::Scalar(56, 193, 255),  cv::Scalar(56, 90, 255),   cv::Scalar(141, 56, 255),
	cv::Scalar(237, 56, 255),  cv::Scalar(255, 56, 169),  cv::Scalar(255, 56, 87),
};

NcnnDetector::NcnnDetector()
	: m_net(nullptr)
	, m_loaded(false)
	, m_inputSize(640)
	, m_confThreshold(0.25f)
	, m_nmsThreshold(0.45f)
	, m_classCount(0)
{
}

NcnnDetector::~NcnnDetector()
{
	if (m_net != nullptr)
	{
		delete (ncnn::Net*)m_net;
		m_net = nullptr;
	}
}

bool NcnnDetector::Load(const std::string& paramPath, const std::string& binPath)
{
	if (m_net == nullptr)
	{
		m_net = new ncnn::Net();
	}

	ncnn::Net* net = (ncnn::Net*)m_net;
	net->clear();


	net->opt.use_vulkan_compute = false;
	net->opt.num_threads = 4;

	if (net->load_param(paramPath.c_str()) != 0)
		return false;

	if (net->load_model(binPath.c_str()) != 0)
		return false;

	m_loaded = true;
	return true;
}


static bool cmpByProb(const NcnnObject& a, const NcnnObject& b)
{
	return a.prob > b.prob;
}


static float IoU(const cv::Rect_<float>& a, const cv::Rect_<float>& b)
{
	float xx1 = std::max(a.x, b.x);
	float yy1 = std::max(a.y, b.y);
	float xx2 = std::min(a.x + a.width, b.x + b.width);
	float yy2 = std::min(a.y + a.height, b.y + b.height);
	float w = std::max(0.0f, xx2 - xx1);
	float h = std::max(0.0f, yy2 - yy1);
	float inter = w * h;
	float areaA = a.width * a.height;
	float areaB = b.width * b.height;
	float uni = areaA + areaB - inter;
	return uni > 0 ? inter / uni : 0.0f;
}

std::vector<NcnnObject> NcnnDetector::Detect(const cv::Mat& bgr)
{
	std::vector<NcnnObject> objects;

	if (!m_loaded || m_net == nullptr || bgr.empty())
		return objects;

	ncnn::Net* net = (ncnn::Net*)m_net;

	int img_w = bgr.cols;
	int img_h = bgr.rows;

	
	ncnn::Mat in = ncnn::Mat::from_pixels_resize(
		(const unsigned char*)bgr.data, ncnn::Mat::PIXEL_BGR2RGB,
		img_w, img_h, m_inputSize, m_inputSize);

	const float norm_vals[3] = { 1.f / 255.f, 1.f / 255.f, 1.f / 255.f };
	in.substract_mean_normalize(0, norm_vals);

	ncnn::Extractor ex = net->create_extractor();
	ex.set_light_mode(true);
	ex.input("in0", in);

	ncnn::Mat out;
	if (ex.extract("out0", out) != 0)
		return objects;

	
	if (out.dims < 2)
		return objects;

	int channels    = (out.c > 1) ? out.c : out.h;
	int per_channel = (out.c > 1) ? (out.h * out.w) : out.w;
	int num_anchors = out.w;
	int num_classes = channels - 4;
	if (num_classes <= 0 || num_anchors <= 0)
		return objects;
	m_classCount = num_classes;

	const float* data = out;

	float scale_x = (float)img_w / m_inputSize;
	float scale_y = (float)img_h / m_inputSize;

	for (int i = 0; i < num_anchors; i++)
	{
		float cx = data[0 * per_channel + i];
		float cy = data[1 * per_channel + i];
		float w  = data[2 * per_channel + i];
		float h  = data[3 * per_channel + i];

		
		float max_score = 0.f;
		int   max_label = 0;
		for (int c = 0; c < num_classes; c++)
		{
			float s = data[(4 + c) * per_channel + i];
			if (s > max_score)
			{
				max_score = s;
				max_label = c;
			}
		}

		if (max_score > m_confThreshold)
		{
			NcnnObject obj;
			obj.rect.x      = (cx - w * 0.5f) * scale_x;
			obj.rect.y      = (cy - h * 0.5f) * scale_y;
			obj.rect.width  = w * scale_x;
			obj.rect.height = h * scale_y;
			obj.label       = max_label;
			obj.prob        = max_score;
			objects.push_back(obj);
		}
	}

	
	std::sort(objects.begin(), objects.end(), cmpByProb);

	
	std::vector<NcnnObject> nms_result;
	std::vector<bool> suppressed(objects.size(), false);
	for (size_t i = 0; i < objects.size(); i++)
	{
		if (suppressed[i])
			continue;
		nms_result.push_back(objects[i]);
		for (size_t j = i + 1; j < objects.size(); j++)
		{
			if (suppressed[j])
				continue;
			if (objects[i].label != objects[j].label)
				continue;
			if (IoU(objects[i].rect, objects[j].rect) > m_nmsThreshold)
				suppressed[j] = true;
		}
	}

	return nms_result;
}


static void PutTextUnicode(cv::Mat& bgr, const std::string& utf8, int x, int y,
	const cv::Scalar& color, int nHeight = 16)
{
	if (bgr.empty() || utf8.empty())
		return;

	// UTF-8 → UTF-16
	int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
	if (wlen <= 1)
		return;
	std::wstring wstr(wlen - 1, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);

	// BGR → BGRA（32 位 DIB 用）
	cv::Mat bgra;
	cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);

	HDC hdc = ::CreateCompatibleDC(NULL);
	BITMAPINFO bi = { 0 };
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = bgra.cols;
	bi.bmiHeader.biHeight = -bgra.rows;   //
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* pBits = NULL;
	HBITMAP hBmp = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
	if (hBmp == NULL || pBits == NULL)
	{
		if (hBmp) ::DeleteObject(hBmp);
		::DeleteDC(hdc);
		return;
	}

	memcpy(pBits, bgra.data, bgra.total() * 4);

	HGDIOBJ hOldBmp = ::SelectObject(hdc, hBmp);


	HFONT hFont = ::CreateFontW(-nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
	HGDIOBJ hOldFont = ::SelectObject(hdc, hFont);

	::SetBkMode(hdc, TRANSPARENT);
	::SetTextColor(hdc, RGB(color[2], color[1], color[0]));

	::TextOutW(hdc, x, y, wstr.c_str(), (int)wstr.size());

	
	memcpy(bgra.data, pBits, bgra.total() * 4);

	::SelectObject(hdc, hOldFont);
	::DeleteObject(hFont);
	::SelectObject(hdc, hOldBmp);
	::DeleteObject(hBmp);
	::DeleteDC(hdc);

	cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
}

static cv::Size MeasureTextUnicode(const std::string& utf8, int nHeight = 16)
{
	int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
	if (wlen <= 1)
		return cv::Size(0, 0);
	std::wstring wstr(wlen - 1, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);

	HDC hdc = ::CreateCompatibleDC(NULL);
	HFONT hFont = ::CreateFontW(-nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
	HGDIOBJ hOld = ::SelectObject(hdc, hFont);
	SIZE sz = { 0 };
	::GetTextExtentPoint32W(hdc, wstr.c_str(), (int)wstr.size(), &sz);
	::SelectObject(hdc, hOld);
	::DeleteObject(hFont);
	::DeleteDC(hdc);
	return cv::Size(sz.cx, sz.cy);
}

cv::Mat NcnnDetector::DetectAndDraw(const cv::Mat& bgr)
{
	cv::Mat result = bgr.clone();

	std::vector<NcnnObject> objects = Detect(bgr);

	const int kFontHeight = 16;

	for (size_t i = 0; i < objects.size(); i++)
	{
		const NcnnObject& obj = objects[i];

		cv::Scalar color = kColors[obj.label % (_countof(kColors))];

		
		cv::rectangle(result, obj.rect, color, 2);

		std::string name;
		if (obj.label < (int)m_classNames.size())
			name = m_classNames[obj.label];
		else
			name = std::to_string(obj.label);

		char szProb[32];
		sprintf_s(szProb, "%.2f", obj.prob);
		std::string text = name + " " + szProb;

		cv::Size ts = MeasureTextUnicode(text, kFontHeight);
		int tx = (int)obj.rect.x;
		int ty = (int)obj.rect.y - ts.height - 4;
		if (ty < 0)
			ty = 0;

		
		cv::rectangle(result, cv::Point(tx, ty),
			cv::Point(tx + ts.width + 6, ty + ts.height + 4), color, cv::FILLED);

		PutTextUnicode(result, text, tx + 3, ty + 2, cv::Scalar(255, 255, 255), kFontHeight);
	}

	return result;
}
