// GreenEnv.cpp: YOLO_Green
//

#include "pch.h"
#include "GreenEnv.h"
#include "AppConfig.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <mutex>
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

// 从 exe 所在目录向上查找指定名称的绿色包目录
static CString FindEnvRoot(const CString& strName)
{
	TCHAR szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	::PathRemoveFileSpec(szPath);

	CString strDir = szPath;
	for (int i = 0; i < 5; i++)
	{
		CString strCandidate = strDir + _T("\\") + strName;
		if (::PathIsDirectory(strCandidate))
			return strCandidate;

		int nPos = strDir.ReverseFind(_T('\\'));
		if (nPos <= 2)
			break;
		strDir = strDir.Left(nPos);
	}
	return _T("");
}

CString GetGreenGpuRoot()
{
	return FindEnvRoot(_T("YOLO_Green_GPU"));
}

CString GetEnvPython(const CString& strRoot)
{
	if (strRoot.IsEmpty())
		return _T("");

	CString strPy = strRoot + _T("\\python\\python.exe");
	if (::PathFileExists(strPy))
		return strPy;

	return _T("");
}

CString GetGreenGpuPython()
{
	return GetEnvPython(GetGreenGpuRoot());
}

// 可用的 GPU 环境包，按优先级排列：主包（cu126）→ cu128（RTX 50 系）→ cu118（老驱动）
static void GetGpuEnvCandidates(CStringArray& arr)
{
	arr.RemoveAll();
	const TCHAR* szNames[] = {
		_T("YOLO_Green_GPU"),
		_T("YOLO_Green_GPU_CU128"),
		_T("YOLO_Green_GPU_CU118"),
	};
	for (int i = 0; i < _countof(szNames); i++)
	{
		CString strRoot = FindEnvRoot(szNames[i]);
		if (!strRoot.IsEmpty() && !GetEnvPython(strRoot).IsEmpty())
			arr.Add(strRoot);
	}
}

bool HasAnyGpuEnv()
{
	CStringArray arr;
	GetGpuEnvCandidates(arr);
	return arr.GetSize() > 0;
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


static int ParseTrainRunNumber(const CString& strName)
{
	if (strName == _T("train"))
		return 1;
	if (strName.GetLength() <= 5 || strName.Left(5) != _T("train"))
		return 0;

	CString strNum = strName.Mid(5);
	if (strNum.IsEmpty())
		return 0;

	strNum.Replace(_T("-"), _T(""));
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

namespace
{
	CStringArray g_arrProjectImages;   // 项目图片路径，最新的在前
	CString      g_strProjectImagesDir;
	bool         g_bProjectImagesLoaded = false;
	std::vector<YOLOPT> g_arrProjectWeights;
}

CStringArray& ProjectImages()
{
	return g_arrProjectImages;
}

std::vector<YOLOPT>& ProjectWeights()
{
	return g_arrProjectWeights;
}

CString GetRunWeightDisplayName(LPCTSTR lpszRunName)
{
	CString strRun = lpszRunName;
	strRun = strRun.Mid(strRun.GetLength() - 1);
	long	nRun = _ttol(strRun);
	if (nRun>1)
	{
		nRun--;
	}
	CString strDisplay;
	strDisplay.Format(_T("%ld--best.pt"), nRun);
	return strDisplay;
}


void ReloadProjectImages(LPCTSTR lpszDir)
{
	CString strDir = lpszDir ? lpszDir : _T("");
	strDir.Trim();

	g_arrProjectImages.RemoveAll();
	g_strProjectImagesDir = strDir;
	g_bProjectImagesLoaded = true;

	if (strDir.IsEmpty() || !::PathIsDirectory(strDir))
		return;

	CStringArray arrFound;
	const TCHAR* szExts[] = { _T("*.jpg"), _T("*.jpeg"), _T("*.png"), _T("*.bmp") };
	for (int k = 0; k < 4; k++)
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(strDir + _T("\\") + szExts[k]);
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory())
				arrFound.Add(finder.GetFilePath());
		}
		finder.Close();
	}

	// 倒序存放，最新的排在最前面
	for (INT_PTR i = arrFound.GetSize() - 1; i >= 0; i--)
		g_arrProjectImages.Add(arrFound[i]);
}

// 目录没变就直接用内存里的结果，不扫盘
void EnsureProjectImages(LPCTSTR lpszDir)
{
	CString strDir = lpszDir ? lpszDir : _T("");
	strDir.Trim();

	if (!g_bProjectImagesLoaded || strDir != g_strProjectImagesDir)
		ReloadProjectImages(strDir);
}

int GetProjectImageCount()
{
	return (int)g_arrProjectImages.GetSize();
}

void InsertProjectImageFront(LPCTSTR lpszPath)
{
	if (lpszPath == NULL || lpszPath[0] == 0)
		return;

	// 已存在则不重复插入
	for (INT_PTR i = 0; i < g_arrProjectImages.GetSize(); i++)
	{
		if (g_arrProjectImages[i].CompareNoCase(lpszPath) == 0)
		{
			g_arrProjectImages.RemoveAt(i);
			break;
		}
	}
	g_arrProjectImages.InsertAt(0, lpszPath);
}

void RemoveProjectImageAt(int nIndex)
{
	if (nIndex < 0 || nIndex >= g_arrProjectImages.GetSize())
		return;
	g_arrProjectImages.RemoveAt(nIndex);
}


// ===== GPU 可用性探测 =====
// 目的是让使用者完全不用关心 CUDA 版本：真实跑一次显卡计算，
// 能跑就用 GPU，跑不了就自动退回 CPU。

static std::mutex g_mtxGpuProbe;
static int      g_nGpuProbeState = 0;   // 0 未探测, 1 可用, 2 不可用
static int      g_nGpuProbeResult = GPU_PROBE_UNKNOWN;
static CString  g_strGpuProbeReason;
static CString  g_strGpuProbeName;
static CString  g_strGpuActiveRoot;      // 探测通过的 GPU 环境根目录

static const TCHAR* kGpuProbeScript =
	_T("# -*- coding: utf-8 -*-\n")
	_T("import sys\n")
	_T("\n")
	_T("def report(msg):\n")
	_T("    sys.stdout.write('GPU_PROBE|' + msg + '\\n')\n")
	_T("    sys.stdout.flush()\n")
	_T("\n")
	_T("try:\n")
	_T("    import torch\n")
	_T("except Exception as e:\n")
	_T("    report('FAIL|PyTorch 无法加载: %s' % e)\n")
	_T("    sys.exit(0)\n")
	_T("\n")
	_T("try:\n")
	_T("    if not torch.cuda.is_available():\n")
	_T("        report('FAIL|未检测到可用的 NVIDIA 显卡或驱动')\n")
	_T("        sys.exit(0)\n")
	_T("    name = torch.cuda.get_device_name(0)\n")
	_T("    major, minor = torch.cuda.get_device_capability(0)\n")
	_T("    cap = 'sm_%d%d' % (major, minor)\n")
	_T("    archs = [a for a in torch.cuda.get_arch_list() if a.startswith('sm_')]\n")
	_T("    supported = False\n")
	_T("    for a in archs:\n")
	_T("        try:\n")
	_T("            v = int(a[3:])\n")
	_T("        except Exception:\n")
	_T("            continue\n")
	_T("        if v // 10 == major and v % 10 <= minor:\n")
	_T("            supported = True\n")
	_T("    if not supported:\n")
	_T("        report('ARCH|%s|%s' % (cap, ','.join(archs)))\n")
	_T("        sys.exit(0)\n")
	_T("    x = torch.zeros((8, 8), device='cuda') + 1\n")
	_T("    torch.cuda.synchronize()\n")
	_T("    report('OK|%s|%s' % (name, cap))\n")
	_T("except Exception as e:\n")
	_T("    report('FAIL|GPU 计算测试失败: %s' % e)\n");

static CString Utf8ToWideText(const std::string& str)
{
	if (str.empty())
		return CString();
	int nWide = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
	if (nWide <= 0)
		return CString();
	CString strOut;
	::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
		strOut.GetBuffer(nWide), nWide);
	strOut.ReleaseBuffer(nWide);
	return strOut;
}

// 静默执行一个进程并收集其输出（stdout/stderr 合并），带超时
static bool RunProcessCapture(const CString& strCmd, CString& strOut, DWORD dwTimeoutMs)
{
	strOut.Empty();

	SECURITY_ATTRIBUTES sa = { 0 };
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE hRead = NULL;
	HANDLE hWrite = NULL;
	if (!::CreatePipe(&hRead, &hWrite, &sa, 64 * 1024))
		return false;
	::SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFO si = { 0 };
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;

	PROCESS_INFORMATION pi = { 0 };
	std::vector<TCHAR> cmd(strCmd.GetLength() + 1);
	_tcscpy_s(cmd.data(), cmd.size(), strCmd);

	BOOL bOk = ::CreateProcess(NULL, cmd.data(), NULL, NULL, TRUE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	::CloseHandle(hWrite);
	if (!bOk)
	{
		::CloseHandle(hRead);
		return false;
	}

	std::string raw;
	char buf[4096];
	DWORD nRead = 0;

	const DWORD dwStart = ::GetTickCount();
	bool bTimeout = false;

	for (;;)
	{
		DWORD nAvail = 0;
		if (::PeekNamedPipe(hRead, NULL, 0, NULL, &nAvail, NULL) && nAvail > 0)
		{
			DWORD nWant = (nAvail < (DWORD)sizeof(buf)) ? nAvail : (DWORD)sizeof(buf);
			if (::ReadFile(hRead, buf, nWant, &nRead, NULL) && nRead > 0)
				raw.append(buf, nRead);
			continue;
		}

		if (::WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0)
		{
			while (::PeekNamedPipe(hRead, NULL, 0, NULL, &nAvail, NULL) && nAvail > 0)
			{
				DWORD nWant = (nAvail < (DWORD)sizeof(buf)) ? nAvail : (DWORD)sizeof(buf);
				if (!::ReadFile(hRead, buf, nWant, &nRead, NULL) || nRead == 0)
					break;
				raw.append(buf, nRead);
			}
			break;
		}

		if (::GetTickCount() - dwStart > dwTimeoutMs)
		{
			bTimeout = true;
			::TerminateProcess(pi.hProcess, 1);
			break;
		}
	}

	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);
	::CloseHandle(hRead);

	strOut = Utf8ToWideText(raw);
	return !bTimeout;
}

static CString WriteGpuProbeScript()
{
	TCHAR szTemp[MAX_PATH] = { 0 };
	if (::GetTempPath(MAX_PATH, szTemp) == 0)
		return _T("");

	CString strPath = CString(szTemp) + _T("yolo_gpu_probe.py");
	if (!WriteTextFileUtf8(strPath, kGpuProbeScript))
		return _T("");
	return strPath;
}

// 用指定环境探测一次；detail 在 OK 时为算力，在 ARCH 时为「算力|支持列表」
static int RunGpuProbe(const CString& strPython, const CString& strScript,
	CString& strName, CString& strDetail)
{
	CString strCmd;
	strCmd.Format(_T("\"%s\" -X utf8 -u \"%s\""),
		(LPCTSTR)strPython, (LPCTSTR)strScript);

	CString strOut;
	if (!RunProcessCapture(strCmd, strOut, 120000))
	{
		strDetail = _T("GPU 检测超时");
		return 0;
	}

	const CString strMark = _T("GPU_PROBE|");
	int nPos = strOut.Find(strMark);
	if (nPos < 0)
	{
		strDetail = _T("GPU 检测无返回结果");
		return 0;
	}

	CString strLine = strOut.Mid(nPos + strMark.GetLength());
	int nEnd = strLine.Find(_T('\n'));
	if (nEnd >= 0)
		strLine = strLine.Left(nEnd);
	strLine.Trim();

	if (strLine.Left(3) == _T("OK|"))
	{
		CString strRest = strLine.Mid(3);
		int nSep = strRest.Find(_T('|'));
		if (nSep >= 0)
		{
			strName = strRest.Left(nSep);
			strDetail = strRest.Mid(nSep + 1);
		}
		else
		{
			strName = strRest;
		}
		return 1;
	}

	if (strLine.Left(5) == _T("ARCH|"))
	{
		strDetail = strLine.Mid(5);
		return 2;
	}

	strDetail = (strLine.Left(5) == _T("FAIL|")) ? strLine.Mid(5) : strLine;
	return 0;
}

bool IsGpuUsable(CString* pReason, CString* pDeviceName)
{
	std::lock_guard<std::mutex> lock(g_mtxGpuProbe);

	if (g_nGpuProbeState == 0)
	{
		int nState = 2;
		int nResult = GPU_PROBE_FAIL;
		CString strReason;
		CString strName;
		CString strArchFail;
		g_strGpuActiveRoot.Empty();

		CStringArray arrRoots;
		GetGpuEnvCandidates(arrRoots);
		CString strScript = WriteGpuProbeScript();

		if (arrRoots.GetSize() == 0)
		{
			nResult = GPU_PROBE_NO_ENV;
			strReason = _T("未找到可用的 GPU 绿色包");
		}
		else if (strScript.IsEmpty())
		{
			strReason = _T("无法生成 GPU 探测脚本");
		}
		else
		{
			// 依次尝试各 GPU 环境：主包(cu126) → cu128(RTX 50) → cu118(老驱动)
			for (int i = 0; i < arrRoots.GetSize(); i++)
			{
				CString strRoot = arrRoots[i];
				CString strProbeName, strDetail;
				int nProbe = RunGpuProbe(GetEnvPython(strRoot), strScript,
					strProbeName, strDetail);

				if (nProbe == 1)
				{
					nState = 1;
					nResult = GPU_PROBE_OK;
					strName = strProbeName;
					strReason = strDetail;
					g_strGpuActiveRoot = strRoot;
					break;
				}
				if (nProbe == 2)
				{
					CString strCap = strDetail;
					int nSep = strDetail.Find(_T('|'));
					if (nSep >= 0)
						strCap = strDetail.Left(nSep);
					strArchFail.Format(
						_T("显卡算力 %s 不被现有 GPU 环境支持（缺少 YOLO_Green_GPU_CU128 包）"),
						(LPCTSTR)strCap);
				}
				else
				{
					strReason = strDetail;
				}
			}

			if (nState != 1 && !strArchFail.IsEmpty())
			{
				strReason = strArchFail;
				nResult = GPU_PROBE_ARCH;
			}
		}

		g_nGpuProbeState = nState;
		g_nGpuProbeResult = nResult;
		g_strGpuProbeReason = strReason;
		g_strGpuProbeName = strName;
	}

	if (pReason)
		*pReason = g_strGpuProbeReason;
	if (pDeviceName)
		*pDeviceName = g_strGpuProbeName;
	return g_nGpuProbeState == 1;
}

CString GetGpuEnvRoot()
{
	if (!IsGpuUsable())
		return _T("");

	std::lock_guard<std::mutex> lock(g_mtxGpuProbe);
	return g_strGpuActiveRoot;
}

GpuProbeResult GetGpuProbeResult()
{
	std::lock_guard<std::mutex> lock(g_mtxGpuProbe);
	return (GpuProbeResult)g_nGpuProbeResult;
}

void InvalidateGpuProbe()
{
	std::lock_guard<std::mutex> lock(g_mtxGpuProbe);
	g_nGpuProbeState = 0;
	g_nGpuProbeResult = GPU_PROBE_UNKNOWN;
	g_strGpuProbeReason.Empty();
	g_strGpuProbeName.Empty();
	g_strGpuActiveRoot.Empty();
}
