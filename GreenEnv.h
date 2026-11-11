// GreenEnv.h: YOLO_Green
//
#pragma once


CString GetGreenRoot();


CString GetGreenPython();

CString GetGreenGpuRoot();

CString GetGreenGpuPython();


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

long& ProjectWeightIndex();

void ReloadProjectImages(LPCTSTR lpszDir);

void EnsureProjectImages(LPCTSTR lpszDir);

int GetProjectImageCount();

void InsertProjectImageFront(LPCTSTR lpszPath);

void RemoveProjectImageAt(int nIndex);
