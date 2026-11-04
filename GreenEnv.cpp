// GreenEnv.cpp: YOLO_Green
//

#include "pch.h"
#include "GreenEnv.h"
#include "AppConfig.h"
#include <shlwapi.h>
#include <shlobj.h>
#pragma comment(lib, "shlwapi.lib")

CString GetGreenRoot()
{
	TCHAR szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	::PathRemoveFileSpec(szPath);

	
	CString strDir = szPath;
	for (int i = 0; i < 5; i++)
	{
		CString strCandidate = strDir + _T("\\YOLO_Green");
		if (::PathIsDirectory(strCandidate))
			return strCandidate;

		int nPos = strDir.ReverseFind(_T('\\'));
		if (nPos <= 2)
			break;
		strDir = strDir.Left(nPos);
	}
	return _T("");
}

CString GetGreenPython()
{
	CString strRoot = GetGreenRoot();
	if (strRoot.IsEmpty())
		return _T("");

	CString strPy = strRoot + _T("\\python\\python.exe");
	if (::PathFileExists(strPy))
		return strPy;

	return _T("");
}

CString GetGreenGpuRoot()
{
	TCHAR szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	::PathRemoveFileSpec(szPath);

	CString strDir = szPath;
	for (int i = 0; i < 5; i++)
	{
		CString strCandidate = strDir + _T("\\YOLO_Green_GPU");
		if (::PathIsDirectory(strCandidate))
			return strCandidate;

		int nPos = strDir.ReverseFind(_T('\\'));
		if (nPos <= 2)
			break;
		strDir = strDir.Left(nPos);
	}
	return _T("");
}

CString GetGreenGpuPython()
{
	CString strRoot = GetGreenGpuRoot();
	if (strRoot.IsEmpty())
		return _T("");

	CString strPy = strRoot + _T("\\python\\python.exe");
	if (::PathFileExists(strPy))
		return strPy;

	return _T("");
}


static bool ReadTextFileUtf8(const CString& strPath, CString& strText)
{
	strText = _T("");
	CFile file;
	if (!file.Open(strPath, CFile::modeRead | CFile::typeBinary))
		return false;

	ULONGLONG nLen = file.GetLength();
	if (nLen == 0 || nLen > 64 * 1024)
	{
		file.Close();
		return false;
	}

	CStringA strA;
	file.Read(strA.GetBuffer((int)nLen), (UINT)nLen);
	strA.ReleaseBuffer((int)nLen);
	file.Close();

	LPCSTR psz = strA;
	if (strA.GetLength() >= 3 && (BYTE)psz[0] == 0xEF && (BYTE)psz[1] == 0xBB && (BYTE)psz[2] == 0xBF)
		psz += 3;

	int nWide = ::MultiByteToWideChar(CP_UTF8, 0, psz, -1, NULL, 0);
	if (nWide <= 0)
		return false;

	::MultiByteToWideChar(CP_UTF8, 0, psz, -1, strText.GetBuffer(nWide), nWide);
	strText.ReleaseBuffer(nWide - 1);
	return true;
}

static bool WriteTextFileUtf8(const CString& strPath, const CString& strText)
{
	CStringA strA;
	int nLen = ::WideCharToMultiByte(CP_UTF8, 0, strText, -1, NULL, 0, NULL, NULL);
	if (nLen <= 0)
		return false;
	::WideCharToMultiByte(CP_UTF8, 0, strText, -1, strA.GetBuffer(nLen), nLen, NULL, NULL);
	strA.ReleaseBuffer(nLen - 1);

	CFile file;
	if (!file.Open(strPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		return false;
	file.Write(strA, (UINT)strA.GetLength());
	file.Close();
	return true;
}



static CString GetLegacyConfigFilePath(LPCTSTR lpszName)
{
	CString strGreen = GetGreenRoot();
	if (strGreen.IsEmpty())
		return _T("");
	return strGreen + _T("\\config\\") + lpszName;
}

CString GetProjectRoot()
{
	CAppConfig& cfg = CAppConfig::Instance();
	CString strRoot = cfg.strProjectRoot;
	strRoot.Trim();
	if (!strRoot.IsEmpty() && ::PathIsDirectory(strRoot))
		return strRoot;

	CString strFile = GetLegacyConfigFilePath(_T("project_root.txt"));
	CString strText;
	if (strFile.IsEmpty() || !::PathFileExists(strFile) || !ReadTextFileUtf8(strFile, strText))
		return _T("");

	strText.Trim();
	if (!::PathIsDirectory(strText))
		return _T("");

	cfg.strProjectRoot = strText;
	cfg.SaveAll();
	return strText;
}


static void CreateDefaultDataYaml(const CString& strRoot)
{
	CString strYaml = strRoot + _T("\\dataset\\data.yaml");
	if (::PathFileExists(strYaml))
		return;

	CStringArray arrNames;
	CString strGreen = GetGreenRoot();
	if (!strGreen.IsEmpty())
	{
		CString strClasses;
		if (ReadTextFileUtf8(strGreen + _T("\\config\\classes.txt"), strClasses))
		{
			INT_PTR nStart = 0;
			while (nStart <= strClasses.GetLength())
			{
				int nEnd = strClasses.Find(_T('\n'), (int)nStart);
				CString strLine = (nEnd < 0)
					? strClasses.Mid((int)nStart)
					: strClasses.Mid((int)nStart, nEnd - (int)nStart);
				strLine.Trim();
				if (!strLine.IsEmpty())
					arrNames.Add(strLine);
				if (nEnd < 0)
					break;
				nStart = nEnd + 1;
			}
		}
	}
	if (arrNames.GetSize() == 0)
	{
		arrNames.Add(_T("ziji"));
		arrNames.Add(_T("qingwa"));
	}

	CString strAbs = strRoot + _T("\\dataset");
	strAbs.Replace(_T("\\"), _T("/"));

	CString strText;
	strText.Format(_T("path: %s\ntrain: images/train\nval: images/val\n\nnc: %d\nnames:\n"),
		(LPCTSTR)strAbs, (int)arrNames.GetSize());
	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
	{
		CString strEntry;
		strEntry.Format(_T("  %d: %s\n"), (int)i, (LPCTSTR)arrNames[i]);
		strText += strEntry;
	}
	WriteTextFileUtf8(strYaml, strText);
}

bool EnsureProjectDirs(LPCTSTR lpszRoot)
{
	CString strRoot = lpszRoot ? lpszRoot : _T("");
	strRoot.Trim();
	if (strRoot.IsEmpty())
		return false;

	while (strRoot.GetLength() > 3 && strRoot[strRoot.GetLength() - 1] == _T('\\'))
		strRoot = strRoot.Left(strRoot.GetLength() - 1);

	if (!::PathIsDirectory(strRoot))
	{
		::SHCreateDirectoryEx(NULL, strRoot, NULL);
		if (!::PathIsDirectory(strRoot))
			return false;
	}

	const TCHAR* szSub[] = {
		_T("\\dataset\\images\\train"),
		_T("\\dataset\\images\\val"),
		_T("\\dataset\\labels\\train"),
		_T("\\dataset\\labels\\val"),
		_T("\\runs\\detect\\train"),
		_T("\\models"),
		_T("\\output\\ui")
	};
	for (int i = 0; i < _countof(szSub); i++)
		::SHCreateDirectoryEx(NULL, strRoot + szSub[i], NULL);

	CreateDefaultDataYaml(strRoot);
	return true;
}

bool SetProjectRoot(LPCTSTR lpszPath)
{
	if (!EnsureProjectDirs(lpszPath))
		return false;

	CString strRoot = lpszPath;
	strRoot.Trim();
	while (strRoot.GetLength() > 3 && strRoot[strRoot.GetLength() - 1] == _T('\\'))
		strRoot = strRoot.Left(strRoot.GetLength() - 1);

	CAppConfig& cfg = CAppConfig::Instance();
	cfg.strProjectRoot = strRoot;
	return cfg.SaveAll();
}



CString GetProjectDatasetDir()
{
	CString strRoot = GetProjectRoot();
	if (!strRoot.IsEmpty())
		return strRoot + _T("\\dataset");

	CString strGreen = GetGreenRoot();
	if (strGreen.IsEmpty())
		return _T("");
	return strGreen + _T("\\datasets\\my_dataset");
}

CString GetProjectImagesDir()
{
	CString strDataset = GetProjectDatasetDir();
	if (strDataset.IsEmpty())
		return _T("");
	return strDataset + _T("\\images\\train");
}

CString GetProjectDataYaml()
{
	CString strDataset = GetProjectDatasetDir();
	if (strDataset.IsEmpty())
		return _T("");
	return strDataset + _T("\\data.yaml");
}

CString GetProjectRunsDir()
{
	CString strRoot = GetProjectRoot();
	if (!strRoot.IsEmpty())
		return strRoot + _T("\\runs\\detect");

	CString strGreen = GetGreenRoot();
	if (strGreen.IsEmpty())
		return _T("");
	return strGreen + _T("\\output\\runs\\detect");
}

CString GetProjectModelsDir()
{
	CString strRoot = GetProjectRoot();
	if (!strRoot.IsEmpty())
		return strRoot + _T("\\models");

	CString strGreen = GetGreenRoot();
	if (strGreen.IsEmpty())
		return _T("");
	return strGreen + _T("\\models");
}

CString GetProjectOutputDir()
{
	CString strRoot = GetProjectRoot();
	if (!strRoot.IsEmpty())
		return strRoot + _T("\\output\\ui");

	CString strGreen = GetGreenRoot();
	if (strGreen.IsEmpty())
		return _T("");
	return strGreen + _T("\\output\\ui");
}

CString GetBestWeightPath()
{
	CString strRuns = GetProjectRunsDir();
	if (strRuns.IsEmpty())
		return _T("");
	return strRuns + _T("\\train\\weights\\best.pt");
}

CString GetDatasetDir()
{
	return GetProjectDatasetDir();
}


CString GetLastTestModel()
{
	CAppConfig& cfg = CAppConfig::Instance();
	CString strPath = cfg.strTestModel;
	strPath.Trim();
	if (!strPath.IsEmpty() && ::PathFileExists(strPath))
		return strPath;

	CString strFile = GetLegacyConfigFilePath(_T("last_test_model.txt"));
	CString strText;
	if (strFile.IsEmpty() || !::PathFileExists(strFile) || !ReadTextFileUtf8(strFile, strText))
		return _T("");

	strText.Trim();
	if (!::PathFileExists(strText))
		return _T("");

	cfg.strTestModel = strText;
	cfg.SaveAll();
	return strText;
}

void SetLastTestModel(LPCTSTR lpszPath)
{
	CAppConfig& cfg = CAppConfig::Instance();
	CString strText = lpszPath ? lpszPath : _T("");
	strText.Trim();
	cfg.strTestModel = strText;
	cfg.SaveAll();
}
