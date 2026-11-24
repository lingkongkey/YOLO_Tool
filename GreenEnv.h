// GreenEnv.h: YOLO_Green
//
#pragma once


CString GetGreenRoot();


CString GetGreenPython();

CString GetGreenGpuRoot();

CString GetGreenGpuPython();

// 指定绿色包目录下的 python.exe，不存在返回空
CString GetEnvPython(const CString& strRoot);

// 是否存在任意 GPU 绿色包（只查目录，不做探测）
bool HasAnyGpuEnv();

// 探测后确认可用的 GPU 环境根目录（主包 / CU128 / CU118 中能跑的那个），空表示不可用
CString GetGpuEnvRoot();

// GPU 可用性探测：在显卡上实际跑一次小计算，结果在进程内缓存。
// 返回 true 表示可以放心用 GPU 训练/预标注；false 时 pReason 给出原因。
bool IsGpuUsable(CString* pReason = nullptr, CString* pDeviceName = nullptr);

// 清除探测缓存，下次调用重新探测
void InvalidateGpuProbe();


CString GetProjectRoot();

bool SetProjectRoot(LPCTSTR lpszPath);


bool EnsureProjectDirs(LPCTSTR lpszRoot);

bool RepairDataYamlPaths();

CString GetProjectDatasetDir();

CString GetProjectImagesDir();

CString GetProjectDataYaml();

CString GetProjectRunsDir();

CString GetProjectModelsDir();

CString GetProjectOutputDir();

CString GetBestWeightPath();

CString GetLastTestModel();

void SetLastTestModel(LPCTSTR lpszPath);

CString GetDatasetDir();

// 项目图片列表：启动或更换目录时扫描一次，之后靠增删维护，避免反复扫盘
CStringArray& ProjectImages();

std::vector<YOLOPT>& ProjectWeights();

CString GetRunWeightDisplayName(LPCTSTR lpszRunName);

void ReloadProjectImages(LPCTSTR lpszDir);

void EnsureProjectImages(LPCTSTR lpszDir);

int GetProjectImageCount();

void InsertProjectImageFront(LPCTSTR lpszPath);

void RemoveProjectImageAt(int nIndex);
