// PageCapture.cpp:

#include "pch.h"
#include "framework.h"
#include "YOLO_Tool.h"
#include "PageCapture.h"
#include "GreenEnv.h"
#include "UiUtil.h"
#include "AppConfig.h"
#include "afxdialogex.h"
#include <afxdlgs.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define IDT_AUTO_CAPTURE    1

static const double DIFF_RATIO_THRESHOLD = 0.005;  
static const double DIFF_MEAN_THRESHOLD  = 1.5;   

#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x00000001
#endif


const int CPageCapture::kCapPageSizes[4] = { 50, 100, 200, 500 };

CPageCapture::CPageCapture(CWnd* pParent /*=nullptr*/)
{
	CDialogEx(IDD_PAGE_CAPTURE, pParent);

	m_hwndTarget = NULL;
	m_bAuto      = false;
	m_nSaved     = 0;
	m_nDirCount  = 0;
	m_nCurCapture = -1;
	m_nPageSize   = kCapPageSizes[0];
	m_nCurPage    = 0;
	m_bLoading    = true;
}

void CPageCapture::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FINDER, m_wndFinder);
	DDX_Control(pDX, IDC_PREVIEW, m_wndPreview);
}

BEGIN_MESSAGE_MAP(CPageCapture, CDialogEx)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BTN_AUTO, &CPageCapture::OnBnClickedBtnAuto)
	ON_BN_CLICKED(IDC_BTN_MANUAL, &CPageCapture::OnBnClickedBtnManual)
	ON_BN_CLICKED(IDC_BTN_BROWSE, &CPageCapture::OnBnClickedBtnBrowse)
	ON_BN_CLICKED(IDC_BTN_CHECK, &CPageCapture::OnBnClickedBtnCheck)
	ON_LBN_SELCHANGE(IDC_LIST_CAPTURES, &CPageCapture::OnLbnSelchangeListCaptures)
	ON_EN_CHANGE(IDC_EDIT_MAXCOUNT, &CPageCapture::OnEnChangeEditMaxcount)
	ON_EN_CHANGE(IDC_EDIT_INTERVAL, &CPageCapture::OnEnChangeEditInterval)
	ON_BN_CLICKED(IDC_BTN_CAP_PREV, &CPageCapture::OnBnClickedBtnCapPrev)
	ON_BN_CLICKED(IDC_BTN_CAP_NEXT, &CPageCapture::OnBnClickedBtnCapNext)
	ON_CBN_SELCHANGE(IDC_COMBO_CAP_SIZE, &CPageCapture::OnCbnSelchangeComboCapSize)
	ON_MESSAGE(WM_FINDER_PICKED, &CPageCapture::OnFinderPicked)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()


BOOL CPageCapture::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CSpinButtonCtrl* pSpin = (CSpinButtonCtrl*)GetDlgItem(IDC_SPIN_INTERVAL);
	if (pSpin != NULL)
		pSpin->SetRange32(100, 600000);

	CSpinButtonCtrl* pSpinMax = (CSpinButtonCtrl*)GetDlgItem(IDC_SPIN_MAXCOUNT);
	if (pSpinMax != NULL)
		pSpinMax->SetRange32(0, 1000000);

	CString strInterval = CAppConfig::Instance().strCaptureInterval;
	CString strLimit    = CAppConfig::Instance().strCaptureLimit;
	if (strInterval.IsEmpty())
		strInterval = _T("1000");
	if (strLimit.IsEmpty())
		strLimit = _T("1000");

	SetDlgItemText(IDC_EDIT_INTERVAL, strInterval);
	SetDlgItemText(IDC_EDIT_MAXCOUNT, strLimit);
	CheckDlgButton(IDC_CHK_CLIENT, BST_CHECKED);

	m_strSaveDir = GetDefaultSaveDir();
	SetDlgItemText(IDC_EDIT_SAVE_DIR, m_strSaveDir);

	InitCapPageSizeCombo();
	m_bLoading = false;

	RefreshDirCount();
	UpdateStatus(_T("就绪"));
	
	FillCaptureList();

	return TRUE;
}

void CPageCapture::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	CListBox* p=(CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
	if (p)
	{
		CRect rcCtrl, rcCtrl2;
		p->GetWindowRect(&rcCtrl);       
		ScreenToClient(&rcCtrl);         
		CRect rcNew = rcCtrl;
		rcNew.bottom = cy - 58;  
		//rcNew.right = cx - 10;
		p->MoveWindow(&rcNew);

		long cyy = 50;
		CWnd* pNav = GetDlgItem(IDC_BTN_CAP_PREV);
		if (pNav != NULL)
		{
			CRect rc; pNav->GetWindowRect(&rc); ScreenToClient(&rc);
			pNav->MoveWindow(rc.left, cy - cyy, rc.Width(), rc.Height());
		}
		pNav = GetDlgItem(IDC_COMBO_CAP_SIZE);
		if (pNav != NULL)
		{
			CRect rc; pNav->GetWindowRect(&rc); ScreenToClient(&rc);
			pNav->MoveWindow(rc.left, cy - cyy, rc.Width(), 120);
		}
		pNav = GetDlgItem(IDC_BTN_CAP_NEXT);
		if (pNav != NULL)
		{
			CRect rc; pNav->GetWindowRect(&rc); ScreenToClient(&rc);
			pNav->MoveWindow(rc.left, cy - cyy, rc.Width(), rc.Height());
		}
		pNav = GetDlgItem(IDC_STATIC_CAP_PAGE);
		if (pNav != NULL)
		{
			CRect rc; pNav->GetWindowRect(&rc); ScreenToClient(&rc);
			pNav->MoveWindow(rc.left, cy - 23, rc.Width(), rc.Height());
		}


		p = (CListBox*)GetDlgItem(IDC_GRP_PREVIEW);
		if (p)
		{
			p->GetWindowRect(&rcCtrl);      
			ScreenToClient(&rcCtrl);        
			CRect rcNew = rcCtrl;
			rcNew.bottom = cy - 10;  
			rcNew.right = cx - 10;
			p->MoveWindow(&rcNew);
		}
		p = (CListBox*)GetDlgItem(IDC_PREVIEW);
		if (p)
		{
			p->GetWindowRect(&rcCtrl);      
			ScreenToClient(&rcCtrl);        
			CRect rcNew = rcCtrl;
			rcNew.bottom = cy - 10;   
			rcNew.right = cx - 10;
			p->MoveWindow(&rcNew);
		}

		
	}
	
}


void CPageCapture::OnDestroy()
{
	if (m_bAuto)
	{
		KillTimer(IDT_AUTO_CAPTURE);
		m_bAuto = false;
	}

	CDialogEx::OnDestroy();
}

void CPageCapture::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == IDT_AUTO_CAPTURE)
		CaptureOnce();
	CDialogEx::OnTimer(nIDEvent);
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

bool CPageCapture::ValidateTarget(bool bShowError)
{
	CString strText;
	GetDlgItemText(IDC_EDIT_HANDLE, strText);

	DWORD dwValue = 0;
	HWND hwnd = NULL;
	if (ParseHandleText(strText, dwValue))
		hwnd = (HWND)(UINT_PTR)dwValue;

	if (hwnd == NULL || !::IsWindow(hwnd))
	{
		if (bShowError)
			AfxMessageBox(_T("窗口句柄无效，请重新填写，或按住准星拖到目标窗口上自动获取。"), MB_ICONWARNING);
		return false;
	}

	if (hwnd != m_hwndTarget)
		m_imgPrevThumb.Destroy();
	m_hwndTarget = hwnd;
	UpdateTargetTitle();
	return true;
}

void CPageCapture::UpdateTargetTitle()
{
	TCHAR szTitle[256] = { 0 };
	if (m_hwndTarget != NULL && ::IsWindow(m_hwndTarget))
		::GetWindowText(m_hwndTarget, szTitle, _countof(szTitle));
	SetDlgItemText(IDC_EDIT_TITLE, szTitle);
}

static	void FillSoRect(HDC m_hDC, LPCRECT lpRect, COLORREF clr)
{
	if (m_hDC == NULL || IsRectEmpty(lpRect))
	{
		return;
	}

	::SetBkColor(m_hDC, clr);
	::ExtTextOut(m_hDC, 0, 0, ETO_OPAQUE, lpRect, NULL, 0, NULL);
}

bool CPageCapture::CaptureWindowToImage(HWND hwnd, bool bClientOnly, CImage& image)
{
	if (!::IsWindow(hwnd))
		return false;

	CRect rc;
	if (bClientOnly)
		::GetClientRect(hwnd, &rc);
	else
		::GetWindowRect(hwnd, &rc);

	int w = rc.Width();
	int h = rc.Height();
	if (w < 8 || h < 8)
		return false;

	HDC hWndDC = ::GetDC(hwnd);
	if (!hWndDC) return false;


	HDC hMemDC = CreateCompatibleDC(hWndDC);
	HBITMAP hBmp = CreateCompatibleBitmap(hWndDC, w, h);
	if (!hMemDC || !hBmp)
	{
		::ReleaseDC(hwnd, hWndDC);
		if (hMemDC) DeleteDC(hMemDC);
		if (hBmp) DeleteObject(hBmp);
		return false;
	}

	HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);
	RECT rct = { 0,0,w ,h };
	FillSoRect(hMemDC, &rct, RGB(255, 0, 0));
	if (!BitBlt(hMemDC, 0, 0, w, h, hWndDC, 0,0, SRCCOPY))
	{
		SelectObject(hMemDC, hOldBmp);
		DeleteObject(hBmp);
		DeleteDC(hMemDC);
		::ReleaseDC(hwnd, hWndDC);
		return false;
	}
	
	
	SelectObject(hMemDC, hOldBmp);
	::ReleaseDC(hwnd, hWndDC);
	DeleteDC(hMemDC);
	image.Destroy();
	image.Attach(hBmp);
	return true;
}

void CPageCapture::MakeThumb(CImage& src, CImage& dst)
{
	const int W = 64, H = 64;
	dst.Destroy();
	dst.Create(W, H, 24);
	HDC hdc = dst.GetDC();
	src.Draw(hdc, 0, 0, W, H);
	dst.ReleaseDC();
}

double CPageCapture::ThumbDiff(CImage& a, CImage& b, double& dChangedRatio)
{
	const int W = 64, H = 64;
	long long total = 0;
	int nChanged = 0;
	for (int y = 0; y < H; y++)
	{
		const BYTE* pa = (const BYTE*)a.GetBits() + y * a.GetPitch();
		const BYTE* pb = (const BYTE*)b.GetBits() + y * b.GetPitch();
		for (int x = 0; x < W; x++)
		{
			int db = abs((int)pa[x * 3]     - (int)pb[x * 3]);
			int dg = abs((int)pa[x * 3 + 1] - (int)pb[x * 3 + 1]);
			int dr = abs((int)pa[x * 3 + 2] - (int)pb[x * 3 + 2]);
			total += db + dg + dr;
			if (db > 32 || dg > 32 || dr > 32)
				nChanged++;
		}
	}
	dChangedRatio = (double)nChanged / (W * H);
	return (double)total / (W * H * 3);
}

bool CPageCapture::CaptureOnce()
{
	if (m_hwndTarget == NULL || !::IsWindow(m_hwndTarget))
	{
		if (m_bAuto)
		{
			StopAuto();
			AfxMessageBox(_T("目标窗口已关闭，自动截图已停止"), MB_ICONWARNING);
		}
		else
		{
			AfxMessageBox(_T("目标窗口句柄已失效，请重新获取。"), MB_ICONWARNING);
		}
		return false;
	}

	GetDlgItemText(IDC_EDIT_SAVE_DIR, m_strSaveDir);
	m_strSaveDir.Trim();
	if (m_strSaveDir.IsEmpty())
	{
		AfxMessageBox(_T("Please enter a capture save directory."), MB_ICONWARNING);
		return false;
	}

	if (!::PathIsDirectory(m_strSaveDir))
	{
		::SHCreateDirectoryEx(NULL, m_strSaveDir, NULL);
		if (!::PathIsDirectory(m_strSaveDir))
		{
			AfxMessageBox(_T("保存目录创建失败：") + m_strSaveDir, MB_ICONWARNING);
			return false;
		}
	}

	bool bClientOnly = (IsDlgButtonChecked(IDC_CHK_CLIENT) == BST_CHECKED);

	// 张数限制：按内存中的项目照片总数计算，不扫盘
	if (m_bAuto)
	{
		int nLimit = GetMaxCount();
		if (nLimit > 0 && m_nDirCount >= nLimit)
		{
			StopAuto();
			CString strMsg;
			strMsg.Format(_T("项目照片已达上限 %d 张（当前 %d 张），自动截图已停止。"),
				nLimit, m_nDirCount);
			AfxMessageBox(strMsg, MB_ICONINFORMATION);
			return false;
		}
	}

	CImage image;
	if (!CaptureWindowToImage(m_hwndTarget, bClientOnly, image))
	{
		AfxMessageBox(_T("截图失败，请确认目标窗口未最小化且句柄有效。"), MB_ICONWARNING);
		return false;
	}

	CImage thumb;
	MakeThumb(image, thumb);
	if (!m_imgPrevThumb.IsNull())
	{
		double dRatio = 0.0;
		double dMean = ThumbDiff(thumb, m_imgPrevThumb, dRatio);
		if (dRatio < DIFF_RATIO_THRESHOLD && dMean < DIFF_MEAN_THRESHOLD)
		{
			m_wndPreview.SetImage(&image);
			UpdateStatus(m_bAuto ? _T("自动截图中（变化不大已跳过）") : _T("变化不大，未保存"));
			return true;
		}
	}


	SYSTEMTIME st;
	::GetLocalTime(&st);
	CString strFile;
	strFile.Format(_T("%s\\%04d%02d%02d_%02d%02d%02d_%03d.jpg"),
		(LPCTSTR)m_strSaveDir,
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	if (FAILED(image.Save(strFile, Gdiplus::ImageFormatJPEG)))
	{
		AfxMessageBox(_T("保存图片失败: ") + strFile, MB_ICONWARNING);
		return false;
	}

	m_nSaved++;
	
	m_imgPrevThumb.Destroy();
	m_imgPrevThumb.Attach(thumb.Detach());
	m_wndPreview.SetImage(&image);
	m_nDirCount++;          // 新增一张，内存计数 +1，不再扫盘
	AddCaptureToFront(strFile);
	UpdateStatus(m_bAuto ? _T("自动截图中") : _T("就绪"));
	return true;
}

void CPageCapture::StartAuto()
{
	if (!ValidateTarget(true))
		return;

	int nLimit = GetMaxCount();
	if (nLimit > 0 && m_nDirCount >= nLimit)
	{
		CString strMsg;
		strMsg.Format(_T("项目照片已达上限 %d 张（当前 %d 张），无法开始自动截图。\r\n")
			_T("如需继续，请调大上限或清理已有照片。"), nLimit, m_nDirCount);
		AfxMessageBox(strMsg, MB_ICONINFORMATION);
		return;
	}

		CString strInterval;
	GetDlgItemText(IDC_EDIT_INTERVAL, strInterval);
	int nInterval = _ttoi(strInterval);
	if (nInterval < 100)
		nInterval = 100;
	if (nInterval > 600000)
		nInterval = 600000;

	m_bAuto = true;
	SetTimer(IDT_AUTO_CAPTURE, (UINT)nInterval, NULL);
	SetUiIdle(false);
	SetDlgItemText(IDC_BTN_AUTO, _T("停止自动截图"));
	UpdateStatus(_T("自动截图中"));
}

void CPageCapture::StopAuto()
{
	if (m_bAuto)
	{
		KillTimer(IDT_AUTO_CAPTURE);
		m_bAuto = false;
	}
	SetUiIdle(true);
	SetDlgItemText(IDC_BTN_AUTO, _T("开始自动截图"));
	UpdateStatus(_T("就绪"));
}

void CPageCapture::SetUiIdle(bool bIdle)
{
	BOOL bEnable = bIdle ? TRUE : FALSE;
	GetDlgItem(IDC_BTN_MANUAL)->EnableWindow(bEnable);
	GetDlgItem(IDC_EDIT_HANDLE)->EnableWindow(bEnable);
	GetDlgItem(IDC_BTN_CHECK)->EnableWindow(bEnable);
	GetDlgItem(IDC_FINDER)->EnableWindow(bEnable);
	GetDlgItem(IDC_EDIT_INTERVAL)->EnableWindow(bEnable);
	GetDlgItem(IDC_SPIN_INTERVAL)->EnableWindow(bEnable);
	GetDlgItem(IDC_EDIT_MAXCOUNT)->EnableWindow(bEnable);
	GetDlgItem(IDC_SPIN_MAXCOUNT)->EnableWindow(bEnable);
	GetDlgItem(IDC_EDIT_SAVE_DIR)->EnableWindow(bEnable);
	GetDlgItem(IDC_BTN_BROWSE)->EnableWindow(bEnable);
	GetDlgItem(IDC_CHK_CLIENT)->EnableWindow(bEnable);
}

void CPageCapture::UpdateStatus(LPCTSTR lpszState)
{
	m_strState = lpszState;

	CString strImages;
	int nLimit = GetMaxCount();
	if (nLimit > 0)
		strImages.Format(_T("%d/%d"), m_nDirCount, nLimit);
	else
		strImages.Format(_T("%d/不限"), m_nDirCount);

	CString str;
	str.Format(_T("Status: %s     Saved: %d     Images: %s"),
		lpszState, m_nSaved, (LPCTSTR)strImages);
	SetDlgItemText(IDC_STATIC_STATUS, str);
}

int CPageCapture::GetMaxCount()
{
	CString strText;
	GetDlgItemText(IDC_EDIT_MAXCOUNT, strText);
	strText.Trim();

	int nLimit = _ttoi(strText);
	return nLimit < 0 ? 0 : nLimit;
}


void CPageCapture::RefreshDirCount()
{
	CString strDir;
	GetDlgItemText(IDC_EDIT_SAVE_DIR, strDir);
	strDir.Trim();

	int nCount = 0;
	if (!strDir.IsEmpty() && ::PathIsDirectory(strDir))
	{
		const TCHAR* szExts[] = { _T("*.jpg"), _T("*.jpeg"), _T("*.png"), _T("*.bmp") };
		for (int k = 0; k < 4; k++)
		{
			CFileFind finder;
			BOOL bMore = finder.FindFile(strDir + _T("\\") + szExts[k]);
			while (bMore)
			{
				bMore = finder.FindNextFile();
				if (!finder.IsDirectory())
					nCount++;
			}
			finder.Close();
		}
	}

	if (nCount != m_nDirCount)
	{
		m_nDirCount = nCount;
		UpdateStatus(m_strState);
	}
}

CString CPageCapture::GetDefaultSaveDir()
{
	CString strDir = GetProjectImagesDir();
	if (!strDir.IsEmpty() && ::PathIsDirectory(strDir))
		return strDir;

	TCHAR szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	::PathRemoveFileSpec(szPath);
	return szPath;
}


void CPageCapture::OnEnChangeEditMaxcount()
{
	if (m_bLoading)
		return;

	CString strText;
	GetDlgItemText(IDC_EDIT_MAXCOUNT, strText);
	strText.Trim();
	CAppConfig::Instance().strCaptureLimit = strText;

	// 上限改动后立即刷新状态栏的 已用/上限
	UpdateStatus(m_strState);
}

void CPageCapture::OnEnChangeEditInterval()
{
	if (m_bLoading)
		return;

	CString strText;
	GetDlgItemText(IDC_EDIT_INTERVAL, strText);
	strText.Trim();
	CAppConfig::Instance().strCaptureInterval = strText;
}

void CPageCapture::OnBnClickedBtnAuto()
{
	if (m_bAuto)
		StopAuto();
	else
		StartAuto();
}

void CPageCapture::OnBnClickedBtnManual()
{
	if (ValidateTarget(true))
		CaptureOnce();
}

void CPageCapture::OnBnClickedBtnBrowse()
{
	CFolderPickerDialog dlgFolder;
	dlgFolder.m_ofn.lpstrTitle = _T("选择项目根目录（截图/标注/训练成果都保存在这里）");

	CString strInit = GetProjectRoot();
	if (!strInit.IsEmpty() && ::PathIsDirectory(strInit))
		dlgFolder.m_ofn.lpstrInitialDir = (LPCTSTR)strInit;

	if (dlgFolder.DoModal() != IDOK)
		return;

	CString strRoot = dlgFolder.GetPathName();
	if (!SetProjectRoot(strRoot))
	{
		AfxMessageBox(_T("项目目录创建失败：") + strRoot, MB_ICONWARNING);
		return;
	}

	if (m_bAuto)
		StopAuto();

	m_strSaveDir = GetProjectImagesDir();
	SetDlgItemText(IDC_EDIT_SAVE_DIR, m_strSaveDir);
	RefreshDirCount();      // 更换目录后重新统计一次
	FillCaptureList();
	UpdateStatus(_T("就绪"));

	CString strMsg;
	strMsg.Format(
		_T("项目根目录已设置：%s\r\n\r\n已创建子目录：\r\n")
		_T("  dataset\\images\\train、val\r\n")
		_T("  dataset\\labels\\train、val\r\n")
		_T("  dataset\\data.yaml\r\n")
		_T("  runs\\detect\\train\r\n")
		_T("  models\r\n")
		_T("  output\\ui\r\n\r\n截图保存到：%s"),
		(LPCTSTR)strRoot, (LPCTSTR)m_strSaveDir);
	AfxMessageBox(strMsg, MB_ICONINFORMATION);
}

void CPageCapture::OnOK()
{
}

void CPageCapture::OnCancel()
{
}

void CPageCapture::OnBnClickedBtnCheck()
{
	if (ValidateTarget(false))
		UpdateTargetTitle();
	else
		SetDlgItemText(IDC_EDIT_TITLE, _T("(无效句柄)"));
}

LRESULT CPageCapture::OnFinderPicked(WPARAM wParam, LPARAM /*lParam*/)
{
	m_hwndTarget = (HWND)wParam;

	CString strHandle;
	strHandle.Format(_T("%08X"), (DWORD)(DWORD_PTR)m_hwndTarget);
	SetDlgItemText(IDC_EDIT_HANDLE, strHandle);
	UpdateTargetTitle();
	return 0;
}


BOOL CPageCapture::PreTranslateMessage(MSG* pMsg)
{
	if (UiForwardEditPaste(pMsg))
		return TRUE;

	if (UiSwallowDialogKey(pMsg))
		return TRUE;

	if (pMsg->message == WM_RBUTTONUP)
	{
		CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
		if (pList != NULL && pMsg->hwnd == pList->GetSafeHwnd())
		{
			CPoint pt((short)LOWORD(pMsg->lParam), (short)HIWORD(pMsg->lParam));
			BOOL bOutside = FALSE;
			int nIndex = pList->ItemFromPoint(pt, bOutside);
			if (!bOutside && nIndex >= 0 && nIndex < pList->GetCount())
			{
				pList->SetCurSel(nIndex);
				LoadPreviewAt(nIndex);
			}

			CPoint ptScreen = pt;
			pList->ClientToScreen(&ptScreen);
			ShowCaptureMenu(ptScreen.x, ptScreen.y);
			return TRUE;
		}
	}

	if (pMsg->message == WM_CONTEXTMENU)
	{
		CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
		if (pList != NULL && pMsg->hwnd == pList->GetSafeHwnd())
		{
			int x = 0, y = 0;
			if (pMsg->lParam == -1)   
			{
				CRect rc;
				pList->GetWindowRect(&rc);
				x = rc.right;
				y = rc.top;
			}
			else
			{
				x = (short)LOWORD(pMsg->lParam);
				y = (short)HIWORD(pMsg->lParam);
			}

			POINT pt = { x, y };
			pList->ScreenToClient(&pt);
			BOOL bOutside = FALSE;
			int nIndex = pList->ItemFromPoint(CPoint(pt), bOutside);
			if (nIndex >= 0 && nIndex < pList->GetCount())
			{
				pList->SetCurSel(nIndex);
				LoadPreviewAt(nIndex);
			}
			ShowCaptureMenu(x, y);
			return TRUE;
		}
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CPageCapture::FillCaptureList()
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
	if (pList == NULL)
		return;


	GetDlgItemText(IDC_EDIT_SAVE_DIR, m_strSaveDir);
	m_strSaveDir.Trim();

	CString strKeep;
	if (m_nCurCapture >= 0 && m_nCurCapture < m_arrCaptures.GetSize())
		strKeep = m_arrCaptures[m_nCurCapture];

	CStringArray arrFiles;
	if (!m_strSaveDir.IsEmpty() && ::PathIsDirectory(m_strSaveDir))
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(m_strSaveDir + _T("\\*.jpg"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory())
				arrFiles.Add(finder.GetFileName());
		}
		finder.Close();
	}

	m_arrCaptures.RemoveAll();
	for (INT_PTR i = arrFiles.GetSize() - 1; i >= 0; i--)
		m_arrCaptures.Add(m_strSaveDir + _T("\\") + arrFiles[i]);

	int nSel = -1;
	if (!strKeep.IsEmpty())
	{
		for (INT_PTR i = 0; i < m_arrCaptures.GetSize(); i++)
		{
			if (m_arrCaptures[i] == strKeep)
			{
				nSel = (int)i;
				break;
			}
		}
	}
	if (nSel < 0 && m_arrCaptures.GetSize() > 0)
		nSel = 0;

	m_nCurCapture = nSel;
	int nSize = m_nPageSize > 0 ? m_nPageSize : kCapPageSizes[0];
	m_nCurPage = (nSel >= 0) ? nSel / nSize : 0;

	RefreshCapPage();
	UpdateCapPageInfo();
}

// 新截图直接插到列表头部（最新在前），不重新扫描目录
void CPageCapture::AddCaptureToFront(const CString& strFile)
{
	m_arrCaptures.InsertAt(0, strFile);
	m_nCurCapture = 0;
	m_nCurPage = 0;

	RefreshCapPage();
	UpdateCapPageInfo();
}

void CPageCapture::InitCapPageSizeCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_CAP_SIZE);
	if (pCombo == NULL)
		return;

	pCombo->ResetContent();
	int iSel = 0;
	for (int i = 0; i < 4; i++)
	{
		CString str;
		str.Format(_T("%d"), kCapPageSizes[i]);
		int nIdx = pCombo->AddString(str);
		if (kCapPageSizes[i] == m_nPageSize)
			iSel = nIdx;
	}
	pCombo->SetCurSel(iSel);
	m_nPageSize = kCapPageSizes[iSel];
}

int CPageCapture::CapLocalToGlobal(int nLocalIndex) const
{
	if (nLocalIndex < 0)
		return -1;

	int nIndex = m_nCurPage * m_nPageSize + nLocalIndex;
	if (nIndex < 0 || nIndex >= m_arrCaptures.GetSize())
		return -1;
	return nIndex;
}

void CPageCapture::RefreshCapPage()
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
	if (pList == NULL)
		return;

	pList->ResetContent();

	int nTotal = (int)m_arrCaptures.GetSize();
	if (nTotal <= 0)
		return;

	int nSize = m_nPageSize > 0 ? m_nPageSize : kCapPageSizes[0];
	int nStart = m_nCurPage * nSize;
	int nEnd = nStart + nSize;
	if (nEnd > nTotal)
		nEnd = nTotal;

	for (int i = nStart; i < nEnd; i++)
	{
		TCHAR szName[MAX_PATH] = { 0 };
		_tcsncpy_s(szName, m_arrCaptures[i], _TRUNCATE);
		::PathStripPath(szName);
		pList->AddString(szName);
	}

	if (m_nCurCapture >= nStart && m_nCurCapture < nEnd)
	{
		int nLocal = m_nCurCapture - nStart;
		pList->SetCurSel(nLocal);
		pList->SetTopIndex(nLocal);
		LoadPreviewAt(m_nCurCapture);
	}
}

void CPageCapture::GotoCapPage(int nPage)
{
	int nTotal = (int)m_arrCaptures.GetSize();
	int nSize = m_nPageSize > 0 ? m_nPageSize : kCapPageSizes[0];
	int nPages = (nTotal > 0) ? (nTotal + nSize - 1) / nSize : 1;

	if (nPage < 0)
		nPage = 0;
	if (nPage >= nPages)
		nPage = nPages - 1;

	m_nCurPage = nPage;
	RefreshCapPage();
	UpdateCapPageInfo();
}

void CPageCapture::UpdateCapPageInfo()
{
	int nTotal = (int)m_arrCaptures.GetSize();
	int nSize = m_nPageSize > 0 ? m_nPageSize : kCapPageSizes[0];

	CString str;
	int nPages = 0;
	if (nTotal <= 0)
	{
		str = _T("0 张");
	}
	else
	{
		nPages = (nTotal + nSize - 1) / nSize;
		int nStart = m_nCurPage * nSize + 1;
		int nEnd = nStart + nSize - 1;
		if (nEnd > nTotal)
			nEnd = nTotal;
		str.Format(_T("第%d页（%d-%d）/%d张 %d页"), m_nCurPage + 1, nStart, nEnd, nTotal, nPages);
	}
	SetDlgItemText(IDC_STATIC_CAP_PAGE, str);

	GetDlgItem(IDC_BTN_CAP_PREV)->EnableWindow(m_nCurPage > 0);
	GetDlgItem(IDC_BTN_CAP_NEXT)->EnableWindow(nPages > 0 && m_nCurPage + 1 < nPages);
}

void CPageCapture::OnBnClickedBtnCapPrev()
{
	GotoCapPage(m_nCurPage - 1);
}

void CPageCapture::OnBnClickedBtnCapNext()
{
	GotoCapPage(m_nCurPage + 1);
}

void CPageCapture::OnCbnSelchangeComboCapSize()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_CAP_SIZE);
	if (pCombo == NULL)
		return;

	int nSel = pCombo->GetCurSel();
	if (nSel < 0 || nSel >= 4)
		return;

	
	int nGlobal = m_nCurCapture;
	m_nPageSize = kCapPageSizes[nSel];
	m_nCurPage = (nGlobal >= 0) ? nGlobal / m_nPageSize : 0;

	RefreshCapPage();
	UpdateCapPageInfo();
}

bool CPageCapture::LoadPreviewAt(int nIndex)
{
	if (nIndex < 0 || nIndex >= m_arrCaptures.GetSize())
		return false;

	CImage image;
	if (FAILED(image.Load(m_arrCaptures[nIndex])))
		return false;

	m_wndPreview.SetImage(&image);
	return true;
}

void CPageCapture::OnLbnSelchangeListCaptures()
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
	if (pList == NULL)
		return;

	int nSel = pList->GetCurSel();
	if (nSel < 0)
		return;

	int nGlobal = CapLocalToGlobal(nSel);
	if (nGlobal < 0)
		return;

	m_nCurCapture = nGlobal;
	LoadPreviewAt(nGlobal);
}

void CPageCapture::ShowCaptureMenu(int x, int y)
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_CAPTURES);
	if (pList == NULL)
		return;

	int nSel = CapLocalToGlobal(pList->GetCurSel());
	if (nSel < 0)
		return;

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, _T("删除图片"));
	menu.AppendMenu(MF_STRING, 2, _T("打开所在文件夹"));

	int nCmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
		x, y, this);
	if (nCmd == 1)
	{
		CString strFile = m_arrCaptures[nSel];
		TCHAR szName[MAX_PATH] = { 0 };
		_tcsncpy_s(szName, strFile, _TRUNCATE);
		::PathStripPath(szName);

		CString strMsg;
		strMsg.Format(_T("确定删除这张截图吗？\r\n\r\n%s"), szName);
		if (AfxMessageBox(strMsg, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;

		::SetFileAttributes(strFile, FILE_ATTRIBUTE_NORMAL);
		if (!::DeleteFile(strFile))
		{
			AfxMessageBox(_T("删除失败: ") + strFile, MB_ICONWARNING);
			return;
		}
		if (m_nDirCount > 0)
			m_nDirCount--;      // 删除一张，内存计数 -1，不再扫盘

		// 直接从内存数组移除，不重新扫描目录
		m_arrCaptures.RemoveAt(nSel);
		int nTotal = (int)m_arrCaptures.GetSize();
		int nSize = m_nPageSize > 0 ? m_nPageSize : kCapPageSizes[0];
		int nPages = (nTotal + nSize - 1) / nSize;
		if (nPages < 1)
			nPages = 1;
		if (m_nCurPage >= nPages)
			m_nCurPage = nPages - 1;

		// 选中原位置的下一条；删的是最后一条则选新的最后一条
		m_nCurCapture = (nSel < nTotal) ? nSel : nTotal - 1;

		if (nTotal <= 0)
			m_wndPreview.SetImage(NULL);

		RefreshCapPage();
		UpdateCapPageInfo();
		UpdateStatus(m_bAuto ? _T("自动截图中") : _T("就绪"));
	}
	else if (nCmd == 2)
	{
		CString strArg;
		strArg.Format(_T("/select,\"%s\""), (LPCTSTR)m_arrCaptures[nSel]);
		::ShellExecute(NULL, _T("open"), _T("explorer.exe"), strArg, NULL, SW_SHOWNORMAL);
	}
}


BOOL CPageCapture::OnHelpInfo(HELPINFO* pHelpInfo)
{

	return 1;
}
