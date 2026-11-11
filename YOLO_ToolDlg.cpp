// YOLO_ToolDlg.cpp:
//


#include "pch.h"
#include "YOLO_Tool.h"
#include "YOLO_ToolDlg.h"
#include "UiUtil.h"
#include "AppConfig.h"
#include "afxdialogex.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


#define WM_TRAYICON_CALLBACK (WM_USER + 200)
#define IDM_TRAY_SHOW        40001
#define IDM_TRAY_EXIT        40002
#define TRAY_ICON_ID         1



class CCloseChoiceDlg : public CDialogEx
{
public:
	CCloseChoiceDlg(CWnd* pParent = nullptr)
		: CDialogEx(IDD_CLOSE_CHOICE, pParent)
		, m_nAction(0)
		, m_nNoAsk(0)
	{
	}

	enum { IDD = IDD_CLOSE_CHOICE };

	int m_nAction;  //
	int m_nNoAsk;   // 

protected:
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CDialogEx::DoDataExchange(pDX);
		DDX_Check(pDX, IDC_CLOSE_NOASK, m_nNoAsk);
	}

	afx_msg void OnBnClickedTray()
	{
		if (!UpdateData(TRUE))
			return;
		m_nAction = 1;
		EndDialog(IDOK);
	}

	afx_msg void OnBnClickedExit()
	{
		if (!UpdateData(TRUE))
			return;
		m_nAction = 2;
		EndDialog(IDOK);
	}

	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CCloseChoiceDlg, CDialogEx)
	ON_BN_CLICKED(IDC_CLOSE_TRAY, &CCloseChoiceDlg::OnBnClickedTray)
	ON_BN_CLICKED(IDC_CLOSE_EXIT, &CCloseChoiceDlg::OnBnClickedExit)
END_MESSAGE_MAP()


// CYOLOToolDlg 
CYOLOToolDlg::CYOLOToolDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_YOLO_TOOL_DIALOG, pParent)
	, m_nCurPage(-1)
	, m_hTrayIcon(nullptr)
	, m_bTrayAdded(false)
	, m_bExiting(false)
{
	ZeroMemory(&m_nid, sizeof(m_nid));
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CYOLOToolDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TAB, m_tab);
}

BEGIN_MESSAGE_MAP(CYOLOToolDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB, &CYOLOToolDlg::OnTcnSelchangeTab)
	ON_MESSAGE(WM_TRAYICON_CALLBACK, &CYOLOToolDlg::OnTrayIcon)
	ON_WM_HELPINFO()
	ON_WM_CLOSE()
END_MESSAGE_MAP()


// CYOLOToolDlg 
BOOL CYOLOToolDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	SetIcon(m_hIcon, FALSE);
	extern const WCHAR* g_yema[];
	m_tab.InsertItem(0, g_yema[0]);
	m_tab.InsertItem(1, g_yema[1]);
	m_tab.InsertItem(2, g_yema[2]);
	m_tab.InsertItem(3, g_yema[3]);

	m_pageCapture.Create(IDD_PAGE_CAPTURE, &m_tab);
	m_pageAnnotate.Create(IDD_PAGE_ANNOTATE, &m_tab);
	m_pageTrain.Create(IDD_PAGE_TRAIN, &m_tab);
	m_pageTest.Create(IDD_PAGE_TEST, &m_tab);


	
	CTabCtrl* p = (CTabCtrl*)GetDlgItem(IDC_TAB);
	CRect tabRect, tabRect2;
	p->GetClientRect(tabRect);
	tabRect.left += 1;
	tabRect.right -= 1;
	tabRect.top += 25;
	tabRect.bottom -= 1;

	GetClientRect(tabRect2);
	tabRect.right = tabRect2.right-1;
	tabRect.bottom = tabRect2.bottom - 30;


	m_pageCapture.MoveWindow(&tabRect);
	m_pageAnnotate.MoveWindow(&tabRect);
	m_pageTrain.MoveWindow(&tabRect);
	m_pageTest.MoveWindow(&tabRect);
	m_tab.SetCurSel(0);
	ShowPage(0);

	InitTrayIcon();

	return TRUE;
}




BOOL CYOLOToolDlg::PreTranslateMessage(MSG* pMsg)
{
	if (UiSwallowDialogKey(pMsg))
		return TRUE;
	return CDialogEx::PreTranslateMessage(pMsg);
}



void CYOLOToolDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);

	if (nType == SIZE_MINIMIZED && !m_bExiting)
	{
		HideToTray();
		return;
	}

	CTabCtrl* p = (CTabCtrl*)GetDlgItem(IDC_TAB);
	if (p)
	{
		CRect rcCtrl;
		p->GetWindowRect(&rcCtrl);
		ScreenToClient(&rcCtrl);  
		CRect rcNew = rcCtrl;
		rcNew.bottom = cy - 5;   
		rcNew.right = cx - 5;
		p->MoveWindow(&rcNew);

		CRect tabRect, tabRect2;
		p->GetClientRect(tabRect);
		tabRect.left += 1;
		tabRect.right -= 1;
		tabRect.top += 25;
		tabRect.bottom -= 1;

		GetClientRect(tabRect2);
		tabRect.right = tabRect2.right - 3;
		tabRect.bottom = tabRect2.bottom - 3;


		m_pageCapture.MoveWindow(&tabRect);
		m_pageAnnotate.MoveWindow(&tabRect);
		m_pageTrain.MoveWindow(&tabRect);
		m_pageTest.MoveWindow(&tabRect);
	}
}
	



void CYOLOToolDlg::ShowPage(int nPage)
{
	m_pageCapture.ShowWindow(nPage == 0 ? SW_SHOW : SW_HIDE);
	m_pageAnnotate.ShowWindow(nPage == 1 ? SW_SHOW : SW_HIDE);
	m_pageTrain.ShowWindow(nPage == 2 ? SW_SHOW : SW_HIDE);
	m_pageTest.ShowWindow(nPage == 3 ? SW_SHOW : SW_HIDE);
	m_nCurPage = nPage;
}

void CYOLOToolDlg::OnTcnSelchangeTab(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	ShowPage(m_tab.GetCurSel());
	*pResult = 0;
}

void CYOLOToolDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this);

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

HCURSOR CYOLOToolDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


BOOL CYOLOToolDlg::OnHelpInfo(HELPINFO* pHelpInfo)
{
	return 1;
}


void CYOLOToolDlg::OnClose()
{
	if (!m_bExiting)
	{
		int nAction = 0;
		CString strSaved = CAppConfig::Instance().strCloseAction;
		if (strSaved == _T("tray"))
			nAction = 1;
		else if (strSaved == _T("exit"))
			nAction = 2;
		else
			nAction = AskCloseAction();

		if (nAction == 0)
			return;
		if (nAction == 1)
		{
			HideToTray();
			return;
		}
	}

	m_bExiting = true;
	m_pageAnnotate.SaveConfig();
	CAppConfig::Instance().SaveAll();
	RemoveTrayIcon();
	CDialogEx::OnClose();
}

void CYOLOToolDlg::OnDestroy()
{
	RemoveTrayIcon();
	CDialogEx::OnDestroy();
}

void CYOLOToolDlg::InitTrayIcon()
{
	if (m_bTrayAdded)
		return;

	m_hTrayIcon = (HICON)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME),
		IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	if (m_hTrayIcon == nullptr)
		m_hTrayIcon = m_hIcon;

	ZeroMemory(&m_nid, sizeof(m_nid));
	m_nid.cbSize = sizeof(m_nid);
	m_nid.hWnd = GetSafeHwnd();
	m_nid.uID = TRAY_ICON_ID;
	m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_nid.uCallbackMessage = WM_TRAYICON_CALLBACK;
	m_nid.hIcon = m_hTrayIcon;
	lstrcpyn(m_nid.szTip, _T("YOLO 综合工具"), _countof(m_nid.szTip));

	m_bTrayAdded = (::Shell_NotifyIcon(NIM_ADD, &m_nid) == TRUE);
}

void CYOLOToolDlg::RemoveTrayIcon()
{
	if (m_bTrayAdded)
	{
		::Shell_NotifyIcon(NIM_DELETE, &m_nid);
		m_bTrayAdded = false;
	}
	if (m_hTrayIcon != nullptr && m_hTrayIcon != m_hIcon)
	{
		::DestroyIcon(m_hTrayIcon);
		m_hTrayIcon = nullptr;
	}
}

void CYOLOToolDlg::HideToTray()
{
	InitTrayIcon();
	if (!m_bTrayAdded)
		return; //

	ShowWindow(SW_HIDE);
}

void CYOLOToolDlg::RestoreFromTray()
{
	ShowWindow(SW_RESTORE);
	::SetForegroundWindow(GetSafeHwnd());
}

int CYOLOToolDlg::AskCloseAction()
{
	CCloseChoiceDlg dlg(this);
	if (dlg.DoModal() != IDOK || dlg.m_nAction == 0)
		return 0;

	if (dlg.m_nNoAsk == BST_CHECKED)
	{
		CAppConfig& cfg = CAppConfig::Instance();
		cfg.strCloseAction = (dlg.m_nAction == 1) ? _T("tray") : _T("exit");
		cfg.SaveAll();
	}

	return dlg.m_nAction;
}

LRESULT CYOLOToolDlg::OnTrayIcon(WPARAM wParam, LPARAM lParam)
{
	if (wParam != TRAY_ICON_ID)
		return 0;


	if (lParam == WM_LBUTTONDBLCLK)
	{
		RestoreFromTray();
		return 0;
	}

	if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
	{
		CMenu menu;
		if (!menu.CreatePopupMenu())
			return 0;
		menu.AppendMenu(MF_STRING, IDM_TRAY_SHOW, _T("还原"));
		menu.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)nullptr);
		menu.AppendMenu(MF_STRING, IDM_TRAY_EXIT, _T("退出"));

		CPoint pt;
		::GetCursorPos(&pt);
		::SetForegroundWindow(GetSafeHwnd());

		UINT nCmd = (UINT)menu.TrackPopupMenu(
			TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
			pt.x, pt.y, this);
		PostMessage(WM_NULL);
		menu.DestroyMenu();

		if (nCmd == IDM_TRAY_SHOW)
		{
			RestoreFromTray();
		}
		else if (nCmd == IDM_TRAY_EXIT)
		{

			m_bExiting = true;
			PostMessage(WM_CLOSE);
		}
	}

	return 0;
}
