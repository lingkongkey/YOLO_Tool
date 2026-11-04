// YOLO_ToolDlg.cpp:
//

#include "pch.h"
#include "framework.h"
#include "YOLO_Tool.h"
#include "YOLO_ToolDlg.h"
#include "UiUtil.h"
#include "AppConfig.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CYOLOToolDlg 
CYOLOToolDlg::CYOLOToolDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_YOLO_TOOL_DIALOG, pParent)
	, m_nCurPage(-1)
{
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
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB, &CYOLOToolDlg::OnTcnSelchangeTab)
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
	
	CAppConfig& cfg = CAppConfig::Instance();

	cfg.nCurPage = m_tab.GetCurSel();

	WINDOWPLACEMENT wp = { 0 };
	wp.length = sizeof(wp);
	if (GetWindowPlacement(&wp))
	{
		cfg.bWinMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
		if (cfg.bWinMaximized)
		{
			cfg.nWinLeft   = wp.rcNormalPosition.left;
			cfg.nWinTop    = wp.rcNormalPosition.top;
			cfg.nWinWidth  = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
			cfg.nWinHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
		}
		else
		{
			cfg.nWinLeft   = wp.rcNormalPosition.left;
			cfg.nWinTop    = wp.rcNormalPosition.top;
			cfg.nWinWidth  = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
			cfg.nWinHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
		}
	}

	cfg.SaveAll();

	CDialogEx::OnClose();
}
