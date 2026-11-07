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

static CString NormalizeDirPath(CString strPath)
{
	strPath.Trim();
	strPath.Replace(_T("/"), _T("\\"));
	while (strPath.GetLength() > 3 && strPath[strPath.GetLength() - 1] == _T('\\'))
		strPath = strPath.Left(strPath.GetLength() - 1);
	return strPath;
}

static bool IsAbsDirPath(const CString& strPath)
{
	if (strPath.IsEmpty())
		return false;
	if (strPath[0] == _T('\\') || strPath[0] == _T('/'))
		return true;
	return strPath.GetLength() >= 2 && strPath[1] == _T(':');
}

// 校验并修正 data.yaml 中的 path/train/val：项目目录被复制到别处后绝对路径会失效
static bool RepairDataYamlFile(const CString& strDataset)
{
	if (strDataset.IsEmpty())
		return false;

	CString strYaml = strDataset + _T("\\data.yaml");
	if (!::PathFileExists(strYaml))
		return false;

	CString strText;
	if (!ReadTextFileUtf8(strYaml, strText))
		return false;

	CString strExpect = strDataset;
	strExpect.Replace(_T("\\"), _T("/"));
	CString strExpectNorm = NormalizeDirPath(strDataset);

	CString strOut;
	bool bChanged = false;
	bool bHasPath = false;

	INT_PTR nPos = 0;
	while (nPos <= strText.GetLength())
	{
		INT_PTR nEnd = strText.Find(_T('\n'), (int)nPos);
		CString strLine = (nEnd < 0) ? strText.Mid((int)nPos) : strText.Mid((int)nPos, (int)(nEnd - nPos));
		CString strEol = (nEnd < 0) ? CString(_T("")) : CString(_T("\n"));

		if (!strLine.IsEmpty() && strLine[strLine.GetLength() - 1] == _T('\r'))
		{
			strLine = strLine.Left(strLine.GetLength() - 1);
			strEol = _T("\r\n");
		}

		CString strTrim = strLine;
		strTrim.Trim();

		CString strKey, strValue;
		int nColon = strTrim.Find(_T(':'));
		if (nColon > 0)
		{
			strKey = strTrim.Left(nColon);
			strKey.Trim();
			strValue = strTrim.Mid(nColon + 1);
			strValue.Trim();
			if (strValue.GetLength() >= 2 &&
				((strValue[0] == _T('"') && strValue[strValue.GetLength() - 1] == _T('"')) ||
				 (strValue[0] == _T('\'') && strValue[strValue.GetLength() - 1] == _T('\''))))
				strValue = strValue.Mid(1, strValue.GetLength() - 2);
		}

		if (!strKey.IsEmpty() && strKey.CompareNoCase(_T("path")) == 0)
		{
			bHasPath = true;
			bool bValid = NormalizeDirPath(strValue).CompareNoCase(strExpectNorm) == 0
				&& ::PathIsDirectory(strValue);
			if (!bValid)
			{
				CString strNewLine;
				strNewLine.Format(_T("path: %s"), (LPCTSTR)strExpect);
				if (strNewLine != strLine)
				{
					strLine = strNewLine;
					bChanged = true;
				}
			}
		}
		else if (!strKey.IsEmpty() &&
			(strKey.CompareNoCase(_T("train")) == 0 || strKey.CompareNoCase(_T("val")) == 0))
		{
			CString strAbs = strValue;
			strAbs.Replace(_T("/"), _T("\\"));
			if (!IsAbsDirPath(strValue))
				strAbs = strDataset + _T("\\") + strAbs;

			if (!::PathIsDirectory(strAbs))
			{
				bool bTrain = (strKey.CompareNoCase(_T("train")) == 0);
				CString strNewLine;
				strNewLine.Format(_T("%s: %s"), (LPCTSTR)strKey, bTrain ? _T("images/train") : _T("images/val"));
				if (strNewLine != strLine)
				{
					strLine = strNewLine;
					bChanged = true;
				}
			}
		}

		strOut += strLine;
		strOut += strEol;

		if (nEnd < 0)
			break;
		nPos = nEnd + 1;
	}

	if (!bHasPath)
	{
		strOut = CString(_T("path: ")) + strExpect + _T("\n") + strOut;
		bChanged = true;
	}

	if (!bChanged)
		return false;

	return WriteTextFileUtf8(strYaml, strOut);
}

bool RepairDataYamlPaths()
{
	return RepairDataYamlFile(GetProjectDatasetDir());
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
	RepairDataYamlFile(strRoot + _T("\\dataset"));
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

// 从目录名解析训练序号：train -> 1，train2 -> 2，train3 -> 3；不匹配返回 0
static int ParseTrainRunNumber(const CString& strName)
{
	if (strName == _T("train"))
		return 1;
	if (strName.GetLength() <= 5 || strName.Left(5) != _T("train"))
		return 0;
	CString strNum = strName.Mid(5);
	if (strNum.IsEmpty())
		return 0;
	for (int i = 0; i < strNum.GetLength(); i++)
	{
		TCHAR ch = strNum[i];
		if (ch < _T('0') || ch > _T('9'))
			return 0;
	}
	return _ttoi(strNum);
}

CString GetBestWeightPath()
{
	CString strRuns = GetProjectRunsDir();
	if (strRuns.IsEmpty())
		return _T("");

	// 遍历 runs/detect 下所有子目录，找出编号最大的 trainN（train 视为 1）
	CString strLatest;
	int nMaxNum = 0;
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(strRuns + _T("\\*"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory() || finder.IsDots())
				continue;
			CString strName = finder.GetFileName();
			int nNum = ParseTrainRunNumber(strName);
			if (nNum <= 0)
				continue;
			if (nNum > nMaxNum)
			{
				nMaxNum = nNum;
				strLatest = strName;
			}
		}
		finder.Close();
	}

	// 未找到任何 train 目录时回退到 train（保持原行为，调用方会再做存在性校验）
	if (strLatest.IsEmpty())
		strLatest = _T("train");

	return strRuns + _T("\\") + strLatest + _T("\\weights\\best.pt");
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
