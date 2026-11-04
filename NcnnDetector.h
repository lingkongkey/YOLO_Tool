
#pragma once

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>


struct NcnnObject
{
	cv::Rect_<float> rect;  
	int            label;   
	float          prob;    
};

class NcnnDetector
{
public:
	NcnnDetector();
	~NcnnDetector();

	bool Load(const std::string& paramPath, const std::string& binPath);

	bool IsLoaded() const { return m_loaded; }

	std::vector<NcnnObject> Detect(const cv::Mat& bgr);

	cv::Mat DetectAndDraw(const cv::Mat& bgr);

	void  SetInputSize(int size)        { m_inputSize = size; }
	void  SetConfThreshold(float t)     { m_confThreshold = t; }
	void  SetNmsThreshold(float t)      { m_nmsThreshold = t; }
	void  SetClassNames(const std::vector<std::string>& names) { m_classNames = names; }

	int   GetClassCount() const { return m_classCount; }

private:
	void*  m_net;            
	bool   m_loaded;
	int    m_inputSize;      
	float  m_confThreshold;  
	float  m_nmsThreshold;   
	int    m_classCount;     
	std::vector<std::string> m_classNames;
};
