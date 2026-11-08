#pragma once

#include "afxdialogex.h"
#include "PageCapture.h"
#include "PageAnnotate.h"
#include "PageTrain.h"
#include "PageTest.h"


class CYOLOToolDlg : public CDialogEx
{

public:
	CYOLOToolDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_YOLO_TOOL_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);   

protected:
	HICON m_hIcon;

	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();

	afx_msg void OnTcnSelchangeTab(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnTrayIcon(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	
	void ShowPage(int nPage);

	void InitTrayIcon();
	void RemoveTrayIcon();
	void HideToTray();
	void RestoreFromTray();

	// 返回 0=取消，1=最小化到托盘，2=退出程序
	int  AskCloseAction();

	CTabCtrl       m_tab;
	CPageCapture   m_pageCapture;   
	CPageAnnotate  m_pageAnnotate;  
	CPageTrain     m_pageTrain;     
	CPageTest      m_pageTest;      
	int            m_nCurPage;      

	NOTIFYICONDATA m_nid;
	HICON          m_hTrayIcon;
	bool           m_bTrayAdded;
	bool           m_bExiting;
public:
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
	afx_msg void OnClose();
};
