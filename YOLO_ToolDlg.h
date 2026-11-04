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

	afx_msg void OnTcnSelchangeTab(NMHDR* pNMHDR, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()

private:
	
	void ShowPage(int nPage);


	CTabCtrl       m_tab;
	CPageCapture   m_pageCapture;   
	CPageAnnotate  m_pageAnnotate;  
	CPageTrain     m_pageTrain;     
	CPageTest      m_pageTest;      
	int            m_nCurPage;      
public:
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
	afx_msg void OnClose();
};
