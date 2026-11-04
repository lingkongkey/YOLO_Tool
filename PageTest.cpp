// PageTest.cpp:
//

#include "pch.h"
#include "framework.h"
#include "YOLO_Tool.h"
#include "PageTest.h"
#include "GreenEnv.h"
#include "UiUtil.h"
#include "afxdialogex.h"
#include <afxdlgs.h>
#include <shlwapi.h>
#include <shlobj.h>
#pragma comment(lib, "shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CPageTest::CPageTest(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_PAGE_TEST, pParent)
	, m_ullResultTime(0)
	, m_bRunning(false)
	, m_bInitializing(false)
	, m_bClientOnly(false)
	, m_hwndTarget(NULL)
{
}

void CPageTest::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PREVIEW_TEST, m_wndPreview);
	DDX_Control(pDX, IDC_FINDER_TEST, m_wndFinder);
}

BEGIN_MESSAGE_MAP(CPageTest, CDialogEx)
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_BTN_TEST_OPEN, &CPageTest::OnBnClickedBtnTestOpen)
	ON_BN_CLICKED(IDC_BTN_TEST_REFRESH, &CPageTest::OnBnClickedBtnTestRefresh)
	ON_BN_CLICKED(IDC_BTN_TEST_DETECT, &CPageTest::OnBnClickedBtnTestDetect)
	ON_CBN_SELCHANGE(IDC_COMBO_TEST_MODEL, &CPageTest::OnCbnSelchangeComboTestModel)
	ON_MESSAGE(WM_FINDER_PICKED, &CPageTest::OnFinderPicked)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

BOOL CPageTest::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	SetDlgItemText(IDC_EDIT_TEST_IMGSZ, _T("640"));
	SetDlgItemText(IDC_EDIT_TEST_CONF, _T("0.25"));
	SetDlgItemText(IDC_EDIT_TEST_IOU, _T("0.45"));
	m_bInitializing = true;
	FillModelCombo();
	RestoreModelSelection();
	m_bInitializing = false;

	
	CString strOutDir = GetProjectOutputDir();
	if (!strOutDir.IsEmpty())
	{
		::SHCreateDirectoryEx(NULL, strOutDir, NULL);
		m_strResultPath = strOutDir + _T("\\result.jpg");
	}
	else
	{
		m_strResultPath.Empty();
	}

	SetStatus(_T("就绪"));
	Layout();
	return TRUE;
}


BOOL CPageTest::PreTranslateMessage(MSG* pMsg)
{

	if (UiForwardEditPaste(pMsg))
		return TRUE;

	if (UiSwallowDialogKey(pMsg))
		return TRUE;
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CPageTest::OnOK()
{
}

void CPageTest::OnCancel()
{
}

void CPageTest::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	Layout();
}


void CPageTest::Layout()
{
	if (!::IsWindow(GetSafeHwnd()))
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	int W = rcClient.Width();
	int H = rcClient.Height();
	if (W < 200 || H < 150)
		return;

	int dpi = UiDpi(m_hWnd);
	auto mv = [&](UINT nID, int x, int y, int w, int h)
	{
		CWnd* pWnd = GetDlgItem(nID);
		if (pWnd != NULL)
		{
			int rw = ::MulDiv(w, dpi, 96); if (rw < 1) rw = 1;
			int rh = ::MulDiv(h, dpi, 96); if (rh < 1) rh = 1;
			pWnd->MoveWindow(::MulDiv(x, dpi, 96), ::MulDiv(y, dpi, 96), rw, rh);
		}
	};

	const int M = 8, ROW1 = 8, ROW2 = 34, CH = 22, BARH = 16;

	int x = M;
	mv(IDC_LBL_TEST_MODEL, x, ROW1 + 6, 36, 14);
	x += 36 + 4;

	int nTailW = 60 + 4 + 56 + 6 + 48 + 6 + 48;
	int nComboW = W - M - x - nTailW;
	if (nComboW > 420) nComboW = 420;
	if (nComboW < 120) nComboW = 120;
	mv(IDC_COMBO_TEST_MODEL, x, ROW1, nComboW, 220);
	x += nComboW + 8;
	mv(IDC_LBL_TEST_IMGSZ, x, ROW1 + 6, 60, 14);
	x += 60 + 4;
	mv(IDC_EDIT_TEST_IMGSZ, x, ROW1, 56, CH);
	x += 56 + 6;
	mv(IDC_BTN_TEST_OPEN, x, ROW1, 48, CH);
	x += 48 + 6;
	mv(IDC_BTN_TEST_REFRESH, x, ROW1, 48, CH);

	x = M;
	mv(IDC_FINDER_TEST, x, ROW2 + 1, 20, 20);
	x += 20 + 8;
	mv(IDC_LBL_TEST_HANDLE, x, ROW2 + 6, 62, 14);
	x += 62 + 4;
	mv(IDC_EDIT_TEST_HANDLE, x, ROW2, 76, CH);
	x += 76 + 10;
	mv(IDC_BTN_TEST_DETECT, x, ROW2, 56, CH);
	x += 56 + 10;
	mv(IDC_LBL_TEST_CONF, x, ROW2 + 6, 48, 14);
	x += 48 + 4;
	mv(IDC_EDIT_TEST_CONF, x, ROW2, 42, CH);
	x += 42 + 6;
	mv(IDC_LBL_TEST_IOU, x, ROW2 + 6, 28, 14);
	x += 28 + 4;
	mv(IDC_EDIT_TEST_IOU, x, ROW2, 42, CH);

	mv(IDC_STATIC_TEST_ZOOM, W - M - 56, ROW2 + 6, 56, 14);

	int nPreviewY = ROW2 + CH + 8;
	int nBarY = H - M - BARH;
	int nPreviewH = nBarY - 6 - nPreviewY;
	if (nPreviewH < 60) nPreviewH = 60;
	mv(IDC_PREVIEW_TEST, M, nPreviewY, W - M * 2, nPreviewH);
	mv(IDC_STATIC_TEST_STATUS, M, nBarY, W - M * 2, BARH);
}


static CString MakeModelDisplayName(const CString& strFull)
{
	CStringArray arrPrefixes;
	CString strProj = GetProjectRoot();
	if (!strProj.IsEmpty())
		arrPrefixes.Add(strProj + _T("\\"));
	CString strGreen = GetGreenRoot();
	if (!strGreen.IsEmpty())
		arrPrefixes.Add(strGreen + _T("\\"));

	for (INT_PTR i = 0; i < arrPrefixes.GetSize(); i++)
	{
		CString strPrefix = arrPrefixes[i];
		if (strFull.GetLength() > strPrefix.GetLength() &&
			strFull.Left(strPrefix.GetLength()).CompareNoCase(strPrefix) == 0)
			return strFull.Mid(strPrefix.GetLength());
	}
	return strFull;
}

void CPageTest::FillModelCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	CString strKeep = GetSelectedModel();

	pCombo->ResetContent();
	m_arrModels.RemoveAll();

	CStringArray arrFiles;
	CString strBest = ::GetBestWeightPath();
	if (!strBest.IsEmpty() && ::PathFileExists(strBest))
		arrFiles.Add(strBest);

	CStringArray arrDirs;
	{
		CString strProj = GetProjectModelsDir();
		if (!strProj.IsEmpty())
			arrDirs.Add(strProj);
		CString strGreen = GetGreenRoot();
		if (!strGreen.IsEmpty())
			arrDirs.Add(strGreen + _T("\\models"));
	}

	for (INT_PTR d = 0; d < arrDirs.GetSize(); d++)
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(arrDirs[d] + _T("\\*.pt"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory())
				arrFiles.Add(finder.GetFileURL());
		}
		finder.Close();
	}

	for (INT_PTR i = 0; i < arrFiles.GetSize(); i++)
	{
		pCombo->AddString(MakeModelDisplayName(arrFiles[i]));
		m_arrModels.Add(arrFiles[i]);
	}

	int nSel = -1;
	if (!strKeep.IsEmpty())
	{
		for (INT_PTR i = 0; i < m_arrModels.GetSize(); i++)
		{
			if (m_arrModels[i].CompareNoCase(strKeep) == 0)
			{
				nSel = (int)i;
				break;
			}
		}
	}
	if (nSel < 0 && pCombo->GetCount() > 0)
		nSel = 0;
	if (nSel >= 0)
		pCombo->SetCurSel(nSel);
}

CString CPageTest::GetSelectedModel()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return _T("");

	int nSel = pCombo->GetCurSel();
	if (nSel < 0 || nSel >= m_arrModels.GetSize())
		return _T("");
	return m_arrModels[nSel];
}

void CPageTest::SaveLastModel()
{
	if (m_bInitializing)
		return;
	CString strModel = GetSelectedModel();
	if (strModel.IsEmpty())
		return;
	::SetLastTestModel(strModel);
}

void CPageTest::RestoreModelSelection()
{
	CString strSaved = ::GetLastTestModel();
	if (strSaved.IsEmpty())
		return;

	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	int nSel = -1;
	for (INT_PTR i = 0; i < m_arrModels.GetSize(); i++)
	{
		if (m_arrModels[i].CompareNoCase(strSaved) == 0)
		{
			nSel = (int)i;
			break;
		}
	}

	if (nSel < 0 && ::PathFileExists(strSaved))
	{
		pCombo->AddString(MakeModelDisplayName(strSaved));
		m_arrModels.Add(strSaved);
		nSel = pCombo->GetCount() - 1;
	}

	if (nSel >= 0)
		pCombo->SetCurSel(nSel);
}

void CPageTest::OnCbnSelchangeComboTestModel()
{
	SaveLastModel();
}


static bool ParseHandleText(const CString& strText, DWORD& dwValue)
{
	CString s = strText;
	s.Trim();
	if (s.IsEmpty())
		return false;

	int nBase = 10;
	CString upper = s;
	upper.MakeUpper();
	if (upper.Left(2) == _T("0X"))
	{
		nBase = 16;
		s = s.Mid(2);
	}
	else if (upper.FindOneOf(_T("ABCDEF")) >= 0)
	{
		nBase = 16;
	}

	if (s.IsEmpty())
		return false;

	TCHAR* pEnd = NULL;
	unsigned long v = _tcstoul(s, &pEnd, nBase);
	if (pEnd == NULL || *pEnd != _T('\0'))
		return false;

	dwValue = (DWORD)v;
	return dwValue != 0;
}

HWND CPageTest::GetTargetHandle(bool bShowError)
{
	CString strText;
	GetDlgItemText(IDC_EDIT_TEST_HANDLE, strText);

	DWORD dwValue = 0;
	HWND hwnd = NULL;
	if (ParseHandleText(strText, dwValue))
		hwnd = (HWND)(UINT_PTR)dwValue;

	if (hwnd == NULL || !::IsWindow(hwnd))
	{
		if (bShowError)
		{
			AfxMessageBox(_T("窗口句柄无效，请重新填写，或按住准星拖到目标窗口上自动获取。"), MB_ICONWARNING);
		}
		return NULL;
	}
	return hwnd;
}


bool CPageTest::EnsureNcnnModel(const CString& strPt, CString& strParam, CString& strBin)
{
	CString strDir;
	{
		CString strTmp = strPt;
		LPTSTR p = strTmp.GetBuffer(_MAX_PATH);
		::PathRemoveFileSpec(p);
		strTmp.ReleaseBuffer();
		strDir = strTmp;
	}

	CString strBase = ::PathFindFileName(strPt);
	int nDot = strBase.ReverseFind(_T('.'));
	if (nDot > 0)
		strBase = strBase.Left(nDot);

	CString strNcnnDir = strDir + _T("\\") + strBase + _T("_ncnn_model");
	strParam = strNcnnDir + _T("\\model.ncnn.param");
	strBin   = strNcnnDir + _T("\\model.ncnn.bin");

	if (::PathFileExists(strParam) && ::PathFileExists(strBin))
		return true;


	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	CString strScript = strGreen + _T("\\scripts\\export_ncnn.py");
	if (strGreen.IsEmpty() || !::PathFileExists(strPython) || !::PathFileExists(strScript))
	{
		AfxMessageBox(_T("未找到 ncnn 导出脚本（scripts\\export_ncnn.py），请先训练或手动导出。"), MB_ICONWARNING);
		return false;
	}

	SetStatus(_T("正在导出 NCNN 模型（首次较慢，请稍候）..."));
	AfxGetApp()->DoWaitCursor(1);

	CString strCmd;
	strCmd.Format(_T("\"%s\" \"%s\" --weights \"%s\""),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strPt);

	STARTUPINFO si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = { 0 };

	
	if (::CreateProcess(NULL, strCmd.GetBuffer(), NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, strGreen, &si, &pi))
	{
		::WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD dwExit = 0;
		::GetExitCodeProcess(pi.hProcess, &dwExit);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		if (dwExit != 0)
		{
			AfxGetApp()->DoWaitCursor(0);
			AfxMessageBox(_T("NCNN 模型导出失败，请在控制台手动执行 export_ncnn.py 查看错误。"), MB_ICONWARNING);
			return false;
		}
	}
	else
	{
		AfxGetApp()->DoWaitCursor(0);
		AfxMessageBox(_T("无法启动导出脚本。"), MB_ICONWARNING);
		return false;
	}

	AfxGetApp()->DoWaitCursor(0);

	return ::PathFileExists(strParam) && ::PathFileExists(strBin);
}


bool CPageTest::LoadClassNames(const CString& strDataYaml, std::vector<std::string>& names)
{
	names.clear();
	if (strDataYaml.IsEmpty() || !::PathFileExists(strDataYaml))
		return false;

	CStringA strAnsi;
	{
		CFile f;
		if (!f.Open(strDataYaml, CFile::modeRead | CFile::shareDenyNone))
			return false;
		ULONGLONG nLen = f.GetLength();
		if (nLen == 0)
		{
			f.Close();
			return false;
		}
		char* pBuf = new char[(UINT)nLen + 1];
		f.Read(pBuf, (UINT)nLen);
		pBuf[nLen] = 0;
		strAnsi = pBuf;
		delete[] pBuf;
		f.Close();
	}


	int nPos = 0;
	while (nPos < strAnsi.GetLength())
	{
		int nEnd = strAnsi.Find('\n', nPos);
		CStringA strLine = (nEnd < 0) ? strAnsi.Mid(nPos) : strAnsi.Mid(nPos, nEnd - nPos);
		strLine.Trim();
		nPos = (nEnd < 0) ? strAnsi.GetLength() : nEnd + 1;

		
		int nColon = strLine.Find(':');
		if (nColon <= 0)
			continue;
		CStringA strKey = strLine.Left(nColon);
		strKey.Trim();
		bool bDigit = !strKey.IsEmpty();
		for (int i = 0; i < strKey.GetLength() && bDigit; i++)
			if (strKey[i] < '0' || strKey[i] > '9') bDigit = false;
		if (!bDigit)
			continue;

		CStringA strVal = strLine.Mid(nColon + 1);
		strVal.Trim();
		
		if (strVal.GetLength() >= 2 && (strVal[0] == '"' || strVal[0] == '\''))
			strVal = strVal.Mid(1, strVal.GetLength() - 2);
		if (!strVal.IsEmpty())
			names.push_back(LPCSTR(strVal));
	}
	return !names.empty();
}



bool CPageTest::CaptureWindowMat(HWND hwnd, bool bClientOnly, cv::Mat& outMat)
{
	if (hwnd == NULL || !::IsWindow(hwnd))
		return false;

	CRect rc;
	if (bClientOnly)
		::GetClientRect(hwnd, &rc);
	else
		::GetWindowRect(hwnd, &rc);

	int w = rc.Width();
	int h = rc.Height();
	if (w < 4 || h < 4)
		return false;

	HDC hdcWin = bClientOnly ? ::GetDC(hwnd) : ::GetWindowDC(hwnd);
	if (hdcWin == NULL)
		return false;

	HDC hdcMem = ::CreateCompatibleDC(hdcWin);
	HBITMAP hBmp = ::CreateCompatibleBitmap(hdcWin, w, h);
	HGDIOBJ hOld = ::SelectObject(hdcMem, hBmp);

#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x00000001
#endif
	UINT nFlags = bClientOnly ? PW_CLIENTONLY : 0;
	if (!::PrintWindow(hwnd, hdcMem, nFlags))
		::BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);

	::SelectObject(hdcMem, hOld);

	BITMAPINFO bi = { 0 };
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	outMat.create(h, w, CV_8UC4);
	::GetDIBits(hdcMem, hBmp, 0, h, outMat.data, &bi, DIB_RGB_COLORS);

	::DeleteObject(hBmp);
	::DeleteDC(hdcMem);
	::ReleaseDC(hwnd, hdcWin);

	// BGRA → BGR
	cv::cvtColor(outMat, outMat, cv::COLOR_BGRA2BGR);
	return true;
}



void CPageTest::OnBnClickedBtnTestDetect()
{
	StartDetect();
}

void CPageTest::StartDetect()
{
	CString strModel = GetSelectedModel();
	if (strModel.IsEmpty())
	{
		AfxMessageBox(_T("请先选择模型（可点「刷新」重新扫描 models 目录）。"), MB_ICONWARNING);
		return;
	}
	SaveLastModel();

	HWND hwnd = GetTargetHandle(true);
	if (hwnd == NULL)
		return;

	CString strParam, strBin;
	if (!EnsureNcnnModel(strModel, strParam, strBin))
	{
		SetStatus(_T("就绪"));
		return;
	}

	SetStatus(_T("加载 NCNN 模型..."));
	AfxGetApp()->DoWaitCursor(1);

	CString strTmp;
	int nImgsz = 0;
	GetDlgItemText(IDC_EDIT_TEST_IMGSZ, strTmp);
	nImgsz = _ttoi(strTmp);
	if (nImgsz < 64 || nImgsz > 4096) nImgsz = 640;

	double dConf = 0.25;
	GetDlgItemText(IDC_EDIT_TEST_CONF, strTmp);
	dConf = _tcstod(strTmp, NULL);
	if (dConf <= 0.0 || dConf > 1.0) dConf = 0.25;

	double dIou = 0.45;
	GetDlgItemText(IDC_EDIT_TEST_IOU, strTmp);
	dIou = _tcstod(strTmp, NULL);
	if (dIou <= 0.0 || dIou > 1.0) dIou = 0.45;

	m_detector.SetInputSize(nImgsz);
	m_detector.SetConfThreshold((float)dConf);
	m_detector.SetNmsThreshold((float)dIou);

	CString strDataYaml = GetProjectDataYaml();
	std::vector<std::string> names;
	if (LoadClassNames(strDataYaml, names))
		m_detector.SetClassNames(names);

	CT2A caParam(strParam, CP_UTF8);
	CT2A caBin(strBin, CP_UTF8);
	if (!m_detector.Load(std::string((LPCSTR)caParam), std::string((LPCSTR)caBin)))
	{
		AfxGetApp()->DoWaitCursor(0);
		AfxMessageBox(_T("NCNN 模型加载失败，请确认 .param/.bin 文件完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}
	AfxGetApp()->DoWaitCursor(0);

	m_ullResultTime = 0;
	::DeleteFile(m_strResultPath);
	m_wndPreview.SetImage(NULL);

	SetRunning(true);
	SetStatus(_T("检测中..."));
	AfxGetApp()->DoWaitCursor(1);

	cv::Mat frame;
	if (CaptureWindowMat(hwnd, m_bClientOnly, frame) && !frame.empty())
	{
		cv::Mat result = m_detector.DetectAndDraw(frame);
		if (!result.empty() && !m_strResultPath.IsEmpty())
		{
			CT2A caOut(m_strResultPath, CP_UTF8);
			std::vector<int> params;
			params.push_back(cv::IMWRITE_JPEG_QUALITY);
			params.push_back(90);
			cv::imwrite(std::string((LPCSTR)caOut), result, params);
		}
		SetStatus(_T("检测完成"));
	}
	else
	{
		SetStatus(_T("截图失败，请确认目标窗口存在且未最小化。"));
	}

	AfxGetApp()->DoWaitCursor(0);
	SetRunning(false);
	LoadResultImage();
}

void CPageTest::SetRunning(bool bRunning)
{
	m_bRunning = bRunning;

	CWnd* pBtn = GetDlgItem(IDC_BTN_TEST_DETECT);
	if (pBtn != NULL)
		pBtn->EnableWindow(bRunning ? FALSE : TRUE);

	BOOL bEnable = bRunning ? FALSE : TRUE;
	UINT ids[] = {
		IDC_COMBO_TEST_MODEL, IDC_BTN_TEST_OPEN, IDC_BTN_TEST_REFRESH,
		IDC_EDIT_TEST_IMGSZ, IDC_EDIT_TEST_HANDLE, IDC_FINDER_TEST,
		IDC_EDIT_TEST_CONF, IDC_EDIT_TEST_IOU
	};
	for (int i = 0; i < _countof(ids); i++)
	{
		CWnd* pWnd = GetDlgItem(ids[i]);
		if (pWnd != NULL)
			pWnd->EnableWindow(bEnable);
	}
}

void CPageTest::SetStatus(LPCTSTR lpszText)
{
	SetDlgItemText(IDC_STATIC_TEST_STATUS, lpszText);
}

void CPageTest::LoadResultImage()
{
	if (m_strResultPath.IsEmpty())
		return;

	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!::GetFileAttributesEx(m_strResultPath, GetFileExInfoStandard, &fad))
		return;
	if (fad.nFileSizeLow == 0 && fad.nFileSizeHigh == 0)
		return;

	ULONGLONG t = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
		fad.ftLastWriteTime.dwLowDateTime;
	if (t == m_ullResultTime)
		return;

	CImage image;
	if (FAILED(image.Load(m_strResultPath)))
		return;

	m_ullResultTime = t;
	m_wndPreview.SetImage(&image);
}



void CPageTest::OnBnClickedBtnTestOpen()
{
	CFileDialog dlg(TRUE, _T("pt"), NULL,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("模型文件 (*.pt)|*.pt|所有文件 (*.*)|*.*||"), this);

	CString strInitDir = GetProjectModelsDir();
	if (!::PathIsDirectory(strInitDir))
	{
		CString strGreen = GetGreenRoot();
		if (!strGreen.IsEmpty())
			strInitDir = strGreen + _T("\\models");
	}
	if (::PathIsDirectory(strInitDir))
		dlg.m_ofn.lpstrInitialDir = (LPCTSTR)strInitDir;

	if (dlg.DoModal() != IDOK)
		return;

	CString strPath = dlg.GetPathName();
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	int nSel = -1;
	for (INT_PTR i = 0; i < m_arrModels.GetSize(); i++)
	{
		if (m_arrModels[i].CompareNoCase(strPath) == 0)
		{
			nSel = (int)i;
			break;
		}
	}
	if (nSel < 0)
	{
		pCombo->AddString(MakeModelDisplayName(strPath));
		m_arrModels.Add(strPath);
		nSel = pCombo->GetCount() - 1;
	}
	pCombo->SetCurSel(nSel);
	SetStatus(_T("已加载模型: ") + MakeModelDisplayName(strPath));
}

void CPageTest::OnBnClickedBtnTestRefresh()
{
	FillModelCombo();
	SetStatus(_T("模型列表已刷新"));
}

LRESULT CPageTest::OnFinderPicked(WPARAM wParam, LPARAM /*lParam*/)
{
	HWND hwnd = (HWND)(UINT_PTR)wParam;
	if (hwnd != NULL && ::IsWindow(hwnd))
	{
		CString strHwnd;
		strHwnd.Format(_T("0x%08X"), (DWORD)(DWORD_PTR)hwnd);
		SetDlgItemText(IDC_EDIT_TEST_HANDLE, strHwnd);
		m_hwndTarget = hwnd;

		TCHAR szTitle[256] = { 0 };
		::GetWindowText(hwnd, szTitle, _countof(szTitle));
		CString strMsg;
		strMsg.Format(_T("已选中窗口: %s"), szTitle);
		SetStatus(strMsg);
	}
	return 0;
}

BOOL CPageTest::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	return TRUE;  
}
