// EnvDownloader.cpp: 运行环境包按需下载与安装
//
#include "pch.h"
#include "EnvDownloader.h"
#include <shlwapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <vector>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

// ===== 路径相关 =====

static CString GetExeDir()
{
	TCHAR szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	::PathRemoveFileSpec(szPath);
	return CString(szPath);
}

static CString GetParentDir(const CString& strDir)
{
	int nPos = strDir.ReverseFind(_T('\\'));
	if (nPos <= 0)
		return strDir;
	return strDir.Left(nPos);
}

// 从 exe 所在目录向上最多 5 级，返回第一个含环境包目录的位置
static CString FindEnvBaseDir()
{
	const TCHAR* szNames[] = {
		_T("YOLO_Green"),
		_T("YOLO_Green_GPU"),
		_T("YOLO_Green_GPU_CU128"),
		_T("YOLO_Green_GPU_CU118"),
	};

	CString strDir = GetExeDir();
	for (int i = 0; i < 5; i++)
	{
		for (int k = 0; k < _countof(szNames); k++)
		{
			if (::PathIsDirectory(strDir + _T("\\") + szNames[k]))
				return strDir;
		}

		int nPos = strDir.ReverseFind(_T('\\'));
		if (nPos <= 2)
			break;
		strDir = strDir.Left(nPos);
	}
	return GetExeDir();
}

CString GetEnvInstallBaseDir()
{
	return FindEnvBaseDir();
}

bool IsEnvInstalled(LPCTSTR lpszName)
{
	if (lpszName == NULL || *lpszName == 0)
		return false;

	CString strDir = GetExeDir();
	for (int i = 0; i < 5; i++)
	{
		CString strRoot = strDir + _T("\\") + lpszName;
		if (::PathIsDirectory(strRoot) &&
			::PathFileExists(strRoot + _T("\\python\\python.exe")))
			return true;

		int nPos = strDir.ReverseFind(_T('\\'));
		if (nPos <= 2)
			break;
		strDir = strDir.Left(nPos);
	}
	return false;
}

bool HasNvidiaDriver()
{
	HMODULE hMod = ::LoadLibraryW(L"nvcuda.dll");
	if (hMod != NULL)
	{
		::FreeLibrary(hMod);
		return true;
	}

	DISPLAY_DEVICEW dd = { 0 };
	dd.cb = sizeof(dd);
	for (DWORD i = 0; ::EnumDisplayDevicesW(NULL, i, &dd, 0); i++)
	{
		if (wcsstr(dd.DeviceString, L"NVIDIA") != NULL)
			return true;
	}
	return false;
}

// ===== 文本文件读写（清单可能是 UTF-8 或 ANSI）=====

static bool ReadTextFile(const CString& strPath, CString& strText)
{
	strText.Empty();

	CFile file;
	if (!file.Open(strPath, CFile::modeRead | CFile::typeBinary))
		return false;

	ULONGLONG nLen = file.GetLength();
	if (nLen == 0 || nLen > 256 * 1024)
	{
		file.Close();
		return false;
	}

	std::vector<BYTE> buf((size_t)nLen);
	file.Read(buf.data(), (UINT)nLen);
	file.Close();

	size_t nBegin = 0;
	UINT nCodePage = CP_UTF8;
	DWORD dwFlags = 0;
	if (nLen >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
		nBegin = 3;

	LPCCH psz = (LPCCH)buf.data() + nBegin;
	int nBytes = (int)(nLen - nBegin);
	int nWide = ::MultiByteToWideChar(nCodePage, dwFlags, psz, nBytes, NULL, 0);
	if (nWide <= 0)
	{
		nWide = ::MultiByteToWideChar(CP_ACP, 0, psz, nBytes, NULL, 0);
		if (nWide <= 0)
			return false;
		::MultiByteToWideChar(CP_ACP, 0, psz, nBytes, strText.GetBuffer(nWide), nWide);
	}
	else
	{
		::MultiByteToWideChar(nCodePage, dwFlags, psz, nBytes, strText.GetBuffer(nWide), nWide);
	}
	strText.ReleaseBuffer(nWide);
	return true;
}

static bool WriteTextFile(const CString& strPath, const CString& strText)
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
	const BYTE bom[3] = { 0xEF, 0xBB, 0xBF };
	file.Write(bom, 3);
	file.Write(strA, (UINT)strA.GetLength());
	file.Close();
	return true;
}

// ===== 清单解析 =====

static const TCHAR* kDefaultManifest =
	_T("; YOLO 运行环境包清单\n")
	_T("; 把下面各包的 url 改成你的下载地址（例如 GitHub Releases 的直链）即可\n")
	_T("; sha256 可留空（留空则不校验）；size 单位为字节，可留空\n")
	_T("\n")
	_T("[YOLO_Green]\n")
	_T("url=https://github.com/lingkongkey/YOLO_Tool/releases/download/env-v1/YOLO_Green.zip\n")
	_T("sha256=\n")
	_T("size=0\n")
	_T("\n")
	_T("[YOLO_Green_GPU]\n")
	_T("url=https://github.com/lingkongkey/YOLO_Tool/releases/download/env-v1/YOLO_Green_GPU.zip\n")
	_T("sha256=\n")
	_T("size=0\n")
	_T("\n")
	_T("[YOLO_Green_GPU_CU128]\n")
	_T("url=https://github.com/lingkongkey/YOLO_Tool/releases/download/env-v1/YOLO_Green_GPU_CU128.zip\n")
	_T("sha256=\n")
	_T("size=0\n")
	_T("\n")
	_T("[YOLO_Green_GPU_CU118]\n")
	_T("url=https://github.com/lingkongkey/YOLO_Tool/releases/download/env-v1/YOLO_Green_GPU_CU118.zip\n")
	_T("sha256=\n")
	_T("size=0\n");

static CString GetManifestPath()
{
	CString strDir = GetExeDir();
	for (int i = 0; i < 5; i++)
	{
		CString strPath = strDir + _T("\\env_manifest.ini");
		if (::PathFileExists(strPath))
			return strPath;

		int nPos = strDir.ReverseFind(_T('\\'));
		if (nPos <= 2)
			break;
		strDir = strDir.Left(nPos);
	}
	return GetExeDir() + _T("\\env_manifest.ini");
}

// 解析清单文本，格式与 ini 相同
static void ParseManifest(const CString& strText, std::vector<EnvPackageInfo>& arrPkgs)
{
	arrPkgs.clear();

	EnvPackageInfo* pCur = NULL;
	INT_PTR nPos = 0;
	while (nPos <= strText.GetLength())
	{
		INT_PTR nEnd = strText.Find(_T('\n'), (int)nPos);
		CString strLine = (nEnd < 0) ? strText.Mid((int)nPos)
			: strText.Mid((int)nPos, (int)(nEnd - nPos));
		nPos = (nEnd < 0) ? strText.GetLength() + 1 : nEnd + 1;

		strLine.Trim();
		if (strLine.IsEmpty() || strLine[0] == _T(';') || strLine[0] == _T('#'))
			continue;

		if (strLine[0] == _T('[') && strLine[strLine.GetLength() - 1] == _T(']'))
		{
			EnvPackageInfo info;
			info.strName = strLine.Mid(1, strLine.GetLength() - 2);
			info.strName.Trim();
			if (!info.strName.IsEmpty())
			{
				arrPkgs.push_back(info);
				pCur = &arrPkgs.back();
			}
			else
			{
				pCur = NULL;
			}
			continue;
		}

		if (pCur == NULL)
			continue;

		int nEq = strLine.Find(_T('='));
		if (nEq <= 0)
			continue;

		CString strKey = strLine.Left(nEq);
		CString strValue = strLine.Mid(nEq + 1);
		strKey.Trim();
		strValue.Trim();

		if (strKey.CompareNoCase(_T("url")) == 0)
			pCur->strUrl = strValue;
		else if (strKey.CompareNoCase(_T("sha256")) == 0)
			pCur->strSha256 = strValue;
		else if (strKey.CompareNoCase(_T("size")) == 0)
			pCur->nSize = _wcstoui64(strValue, NULL, 10);
	}
}

bool LoadEnvManifest(std::vector<EnvPackageInfo>& arrPkgs)
{
	arrPkgs.clear();

	CString strPath = GetManifestPath();
	CString strText;
	if (!::PathFileExists(strPath) || !ReadTextFile(strPath, strText))
	{
		// 没有清单时使用内置默认值，并尽力生成一份可编辑的模板
		strText = kDefaultManifest;
		WriteTextFile(strPath, strText);
	}

	ParseManifest(strText, arrPkgs);
	if (arrPkgs.empty())
	{
		// 用户清单为空或损坏：用默认值兜底，并把模板写回磁盘供其编辑
		strText = kDefaultManifest;
		WriteTextFile(strPath, strText);
		ParseManifest(strText, arrPkgs);
	}
	return !arrPkgs.empty();
}

CString EnsureEnvManifest()
{
	std::vector<EnvPackageInfo> arrPkgs;
	LoadEnvManifest(arrPkgs);
	return GetManifestPath();
}

bool FindEnvPackage(LPCTSTR lpszName, EnvPackageInfo& info)
{
	std::vector<EnvPackageInfo> arrPkgs;
	LoadEnvManifest(arrPkgs);
	for (size_t i = 0; i < arrPkgs.size(); i++)
	{
		if (arrPkgs[i].strName.CompareNoCase(lpszName) == 0)
		{
			info = arrPkgs[i];
			return true;
		}
	}
	return false;
}

// ===== SHA256 校验 =====

static bool ComputeFileSha256(const CString& strPath, CString& strHex)
{
	strHex.Empty();

	HANDLE hFile = ::CreateFileW(strPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	BCRYPT_ALG_HANDLE hAlg = NULL;
	BCRYPT_HASH_HANDLE hHash = NULL;
	DWORD cbHash = 0, cbTmp = 0;
	bool bOk = false;
	std::vector<BYTE> hashBuf;

	do
	{
		if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
			break;
		if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
			(PUCHAR)&cbHash, sizeof(cbHash), &cbTmp, 0) < 0)
			break;
		if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) < 0)
			break;

		std::vector<BYTE> buf(256 * 1024);
		DWORD nRead = 0;
		bool bReadOk = true;
		while (::ReadFile(hFile, buf.data(), (DWORD)buf.size(), &nRead, NULL) && nRead > 0)
		{
			if (BCryptHashData(hHash, buf.data(), nRead, 0) < 0)
			{
				bReadOk = false;
				break;
			}
		}

		hashBuf.resize(cbHash);
		if (!bReadOk || BCryptFinishHash(hHash, hashBuf.data(), cbHash, 0) < 0)
			break;
		bOk = true;
	} while (false);

	if (hHash)
		BCryptDestroyHash(hHash);
	if (hAlg)
		BCryptCloseAlgorithmProvider(hAlg, 0);
	::CloseHandle(hFile);

	if (!bOk)
		return false;

	for (DWORD i = 0; i < cbHash; i++)
	{
		CString strByte;
		strByte.Format(_T("%02x"), hashBuf[i]);
		strHex += strByte;
	}
	return true;
}

// ===== HTTP 下载（支持断点续传）=====

static bool DownloadFile(const CString& strUrl, const CString& strDest,
	EnvProgressFn pfn, LPVOID pUser, ULONGLONG nExpectSize, CString& strErr)
{
	URL_COMPONENTS uc = { 0 };
	TCHAR szHost[2048] = { 0 };
	TCHAR szPath[8192] = { 0 };
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = szHost;
	uc.dwHostNameLength = _countof(szHost);
	uc.lpszUrlPath = szPath;
	uc.dwUrlPathLength = _countof(szPath);

	if (!::WinHttpCrackUrl(strUrl, (DWORD)strUrl.GetLength(), 0, &uc))
	{
		strErr = _T("下载地址无效，请检查 env_manifest.ini");
		return false;
	}

	// 已有文件大小，用于续传
	ULONGLONG nHave = 0;
	HANDLE hProbe = ::CreateFileW(strDest, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hProbe != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER li = { 0 };
		if (::GetFileSizeEx(hProbe, &li))
			nHave = (ULONGLONG)li.QuadPart;
		::CloseHandle(hProbe);
	}

	if (nExpectSize > 0 && nHave == nExpectSize)
		return true;
	if (nExpectSize > 0 && nHave > nExpectSize)
	{
		::DeleteFileW(strDest);
		nHave = 0;
	}

	HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
	bool bOk = false;
	HANDLE hFile = INVALID_HANDLE_VALUE;

	do
	{
		hSession = ::WinHttpOpen(L"YOLO_Tool/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (hSession == NULL)
		{
			strErr = _T("初始化网络失败");
			break;
		}

		DWORD dwTimeout = 30000;
		::WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
		::WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));

		hConnect = ::WinHttpConnect(hSession, szHost, uc.nPort, 0);
		if (hConnect == NULL)
		{
			strErr = _T("无法连接下载服务器，请检查网络");
			break;
		}

		DWORD dwFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
		hRequest = ::WinHttpOpenRequest(hConnect, L"GET", szPath, NULL,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
		if (hRequest == NULL)
		{
			strErr = _T("创建下载请求失败");
			break;
		}

		// GitHub Releases 会跳转到 CDN，必须允许自动跟随
		DWORD dwPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
		::WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &dwPolicy, sizeof(dwPolicy));

		if (nHave > 0)
		{
			CString strRange;
			strRange.Format(_T("Range: bytes=%I64u-\r\n"), nHave);
			::WinHttpAddRequestHeaders(hRequest, strRange, (DWORD)-1L,
				WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
		}

		if (!::WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			!::WinHttpReceiveResponse(hRequest, NULL))
		{
			strErr = _T("下载请求失败，请检查网络");
			break;
		}

		DWORD dwCode = 0;
		DWORD cbCode = sizeof(dwCode);
		::WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &dwCode, &cbCode, WINHTTP_NO_HEADER_INDEX);

		if (dwCode == 416)
		{
			// 服务器认为已有部分已经到文件尾：当作已下载完，交给校验环节判断
			bOk = true;
			break;
		}
		if (dwCode != 200 && dwCode != 206)
		{
			strErr.Format(_T("下载失败（HTTP %lu），请确认下载地址有效"), dwCode);
			break;
		}

		if (dwCode == 200 && nHave > 0)
		{
			// 服务器不支持续传，只能从头写
			nHave = 0;
		}

		// 总大小：优先用清单里的值
		ULONGLONG nTotal = nExpectSize;
		if (nTotal == 0)
		{
			TCHAR szLen[64] = { 0 };
			DWORD cbLen = sizeof(szLen) - sizeof(TCHAR);
			if (::WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
				WINHTTP_HEADER_NAME_BY_INDEX, szLen, &cbLen, WINHTTP_NO_HEADER_INDEX))
			{
				nTotal = _wcstoui64(szLen, NULL, 10) + nHave;
			}
		}

		hFile = ::CreateFileW(strDest, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			strErr = _T("无法写入下载文件，请检查磁盘空间或目录权限");
			break;
		}

		LARGE_INTEGER liSeek = { 0 };
		liSeek.QuadPart = (LONGLONG)nHave;
		::SetFilePointerEx(hFile, liSeek, NULL, FILE_BEGIN);
		if (nHave == 0)
			::SetEndOfFile(hFile);

		ULONGLONG nDone = nHave;
		DWORD dwLastTick = ::GetTickCount();
		int nLastPct = -100;
		std::vector<BYTE> buf(64 * 1024);
		bool bWriteOk = true;

		for (;;)
		{
			DWORD nRead = 0;
			if (!::WinHttpReadData(hRequest, buf.data(), (DWORD)buf.size(), &nRead))
			{
				bWriteOk = false;
				break;
			}
			if (nRead == 0)
				break;

			DWORD nWritten = 0;
			if (!::WriteFile(hFile, buf.data(), nRead, &nWritten, NULL) || nWritten != nRead)
			{
				bWriteOk = false;
				break;
			}
			nDone += nRead;

			if (pfn)
			{
				DWORD dwNow = ::GetTickCount();
				int nPct = (nTotal > 0) ? (int)(nDone * 100 / nTotal) : -1;
				if (nPct < 0)
				{
					if (dwNow - dwLastTick >= 300)
					{
						dwLastTick = dwNow;
						pfn(-1, 0, pUser);
					}
				}
				else if (nPct != nLastPct && (dwNow - dwLastTick >= 200 || nPct >= 100))
				{
					dwLastTick = dwNow;
					nLastPct = nPct;
					pfn(nPct, 0, pUser);
				}
			}
		}

		if (!bWriteOk)
		{
			strErr = _T("下载中断，请重试（支持断点续传）");
			break;
		}

		if (nTotal > 0 && nDone < nTotal)
		{
			strErr = _T("下载未完成，请重试（支持断点续传）");
			break;
		}

		bOk = true;
	} while (false);

	if (hFile != INVALID_HANDLE_VALUE)
		::CloseHandle(hFile);
	if (hRequest)
		::WinHttpCloseHandle(hRequest);
	if (hConnect)
		::WinHttpCloseHandle(hConnect);
	if (hSession)
		::WinHttpCloseHandle(hSession);

	return bOk;
}

// ===== 解压与安装 =====

static bool RunAndWait(const CString& strCmdLine, DWORD dwTimeoutMs)
{
	STARTUPINFO si = { 0 };
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi = { 0 };
	std::vector<TCHAR> cmd(strCmdLine.GetLength() + 1);
	_tcscpy_s(cmd.data(), cmd.size(), strCmdLine);

	if (!::CreateProcess(NULL, cmd.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW,
		NULL, NULL, &si, &pi))
		return false;

	DWORD dwWait = ::WaitForSingleObject(pi.hProcess, dwTimeoutMs);
	DWORD dwExit = 1;
	::GetExitCodeProcess(pi.hProcess, &dwExit);
	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);

	return dwWait == WAIT_OBJECT_0 && dwExit == 0;
}

static void RemoveDirRecursive(const CString& strDir)
{
	CString strPattern = strDir + _T("\\*");
	WIN32_FIND_DATA fd;
	HANDLE hFind = ::FindFirstFile(strPattern, &fd);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (_tcscmp(fd.cFileName, _T(".")) == 0 || _tcscmp(fd.cFileName, _T("..")) == 0)
				continue;

			CString strChild = strDir + _T("\\") + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				RemoveDirRecursive(strChild);
			}
			else
			{
				::SetFileAttributes(strChild, FILE_ATTRIBUTE_NORMAL);
				::DeleteFile(strChild);
			}
		} while (::FindNextFile(hFind, &fd));
		::FindClose(hFind);
	}
	::RemoveDirectory(strDir);
}

// 在解压目录中找真正的环境根（兼容「压缩包内多一层同名目录」）
static CString FindExtractedEnvRoot(const CString& strDir)
{
	if (::PathFileExists(strDir + _T("\\python\\python.exe")))
		return strDir;

	WIN32_FIND_DATA fd;
	HANDLE hFind = ::FindFirstFile(strDir + _T("\\*"), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return CString();

	CString strFound;
	do
	{
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (_tcscmp(fd.cFileName, _T(".")) == 0 || _tcscmp(fd.cFileName, _T("..")) == 0)
			continue;

		CString strSub = strDir + _T("\\") + fd.cFileName;
		if (::PathFileExists(strSub + _T("\\python\\python.exe")))
		{
			strFound = strSub;
			break;
		}
	} while (::FindNextFile(hFind, &fd));
	::FindClose(hFind);

	return strFound;
}

static CString GetTarPath()
{
	TCHAR szWin[MAX_PATH] = { 0 };
	if (::GetWindowsDirectory(szWin, MAX_PATH) > 0)
	{
		CString strTar = CString(szWin) + _T("\\System32\\tar.exe");
		if (::PathFileExists(strTar))
			return strTar;
	}
	return CString();
}

bool DownloadAndInstallEnv(LPCTSTR lpszName, EnvProgressFn pfn, LPVOID pUser, CString& strErr)
{
	strErr.Empty();

	EnvPackageInfo info;
	if (!FindEnvPackage(lpszName, info))
	{
		strErr.Format(_T("环境包清单中没有 %s，请编辑 env_manifest.ini"), lpszName);
		return false;
	}

	CString strUrl = info.strUrl;
	strUrl.Trim();
	if (strUrl.IsEmpty())
	{
		strErr.Format(_T("环境包 %s 未配置下载地址，请编辑 env_manifest.ini"), lpszName);
		return false;
	}

	CString strTar = GetTarPath();
	if (strTar.IsEmpty())
	{
		strErr = _T("系统缺少 tar.exe（需要 Windows 10 1803 及以上版本）");
		return false;
	}

	CString strBase = GetEnvInstallBaseDir();
	CString strWorkDir = strBase + _T("\\.env_download");
	::SHCreateDirectoryEx(NULL, strWorkDir, NULL);
	if (!::PathIsDirectory(strWorkDir))
	{
		strErr = _T("程序目录不可写，请把工具放到有写入权限的目录（不要放在 Program Files 下）");
		return false;
	}

	CString strZip = strWorkDir + _T("\\") + lpszName + _T(".zip");
	CString strExtract = strWorkDir + _T("\\") + lpszName + _T("_tmp");
	CString strTarget = strBase + _T("\\") + lpszName;

	if (pfn)
		pfn(0, 0, pUser);

	if (!DownloadFile(strUrl, strZip, pfn, pUser, info.nSize, strErr))
		return false;

	CString strWant = info.strSha256;
	strWant.Trim();
	if (!strWant.IsEmpty())
	{
		if (pfn)
			pfn(-1, 1, pUser);

		CString strActual;
		if (!ComputeFileSha256(strZip, strActual))
		{
			strErr = _T("读取下载文件失败");
			return false;
		}
		strWant.Remove(_T(' '));
		if (strActual.CompareNoCase(strWant) != 0)
		{
			::DeleteFile(strZip);
			strErr = _T("下载文件校验失败（SHA256 不匹配），已删除，请重新下载");
			return false;
		}
	}

	if (pfn)
		pfn(-1, 2, pUser);

	RemoveDirRecursive(strExtract);
	::SHCreateDirectoryEx(NULL, strExtract, NULL);

	CString strCmd;
	strCmd.Format(_T("\"%s\" -xf \"%s\" -C \"%s\""),
		(LPCTSTR)strTar, (LPCTSTR)strZip, (LPCTSTR)strExtract);
	if (!RunAndWait(strCmd, 60 * 60 * 1000))
	{
		RemoveDirRecursive(strExtract);
		strErr = _T("解压失败，压缩包可能已损坏，请重试");
		return false;
	}

	CString strRoot = FindExtractedEnvRoot(strExtract);
	if (strRoot.IsEmpty())
	{
		RemoveDirRecursive(strExtract);
		strErr.Format(_T("压缩包内容不正确：没有找到 python\\python.exe，请确认 %s.zip 的内容"), lpszName);
		return false;
	}

	// 删除之前不完整的同名目录，再移动到目标位置（同一磁盘，移动很快）
	if (::PathIsDirectory(strTarget))
		RemoveDirRecursive(strTarget);

	if (!::MoveFileEx(strRoot, strTarget, MOVEFILE_REPLACE_EXISTING))
	{
		RemoveDirRecursive(strExtract);
		strErr = _T("安装失败，请检查磁盘空间或目录权限");
		return false;
	}

	RemoveDirRecursive(strExtract);
	::DeleteFile(strZip);
	::RemoveDirectory(strWorkDir);

	if (pfn)
		pfn(100, 2, pUser);
	return true;
}
