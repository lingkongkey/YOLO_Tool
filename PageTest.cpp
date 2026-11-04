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

	mv(IDC_STATIC_TEST_ZOOM, W - M - 80, ROW2 + 6, 56, 14);

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

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	CString strScript = strGreen + _T("\\scripts\\infer_ui.py");
	if (strGreen.IsEmpty() || !::PathFileExists(strPython) || !::PathFileExists(strScript))
	{
		AfxMessageBox(_T("未找到 scripts\\infer_ui.py，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	if (m_strResultPath.IsEmpty())
	{
		AfxMessageBox(_T("未找到输出目录，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

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

	m_ullResultTime = 0;
	::DeleteFile(m_strResultPath);
	m_wndPreview.SetImage(NULL);

	CString strCmd;
	strCmd.Format(_T("\"%s\" \"%s\" --weights \"%s\" --hwnd 0x%I64X --imgsz %d --conf %.2f --iou %.2f --mode single --out \"%s\""),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strModel,
		(unsigned __int64)(UINT_PTR)hwnd, nImgsz, dConf, dIou, (LPCTSTR)m_strResultPath);

	SetRunning(true);
	SetStatus(_T("检测中..."));
	AfxGetApp()->DoWaitCursor(1);

	STARTUPINFO si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = { 0 };
	DWORD dwExit = (DWORD)-1;

	if (::CreateProcess(NULL, (LPTSTR)(LPCTSTR)strCmd, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, strGreen, &si, &pi))
	{
		::WaitForSingleObject(pi.hProcess, INFINITE);
		::GetExitCodeProcess(pi.hProcess, &dwExit);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
	}

	AfxGetApp()->DoWaitCursor(0);
	SetRunning(false);

	if (dwExit == 0 && ::PathFileExists(m_strResultPath))
	{
		SetStatus(_T("检测完成"));
		LoadResultImage();
	}
	else
	{
		AfxMessageBox(_T("检测失败，请确认模型与绿色包环境完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
	}
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
