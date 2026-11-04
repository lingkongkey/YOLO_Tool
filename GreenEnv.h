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
