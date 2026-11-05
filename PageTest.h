// PageTest.h:
//
#pragma once

#include "afxdialogex.h"
#include "FinderStatic.h"
#include "PreviewStatic.h"

class CPageTest : public CDialogEx
{
public:
	CPageTest(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PAGE_TEST };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnSize(UINT nType, int cx, int cy);

	afx_msg void OnBnClickedBtnTestOpen();
	afx_msg void OnBnClickedBtnTestRefresh();
	afx_msg void OnBnClickedBtnTestDetect();
	afx_msg void OnBnClickedBtnTestImage();
	afx_msg void OnCbnSelchangeComboTestModel();
	afx_msg LRESULT OnFinderPicked(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	
	void FillModelCombo();
	
	CString GetSelectedModel();
	
	void SaveLastModel();
	
	void RestoreModelSelection();
	
	HWND GetTargetHandle(bool bShowError);
	
	void Layout();
	
	void StartDetect();
	void StartImageDetect();
	void ReadDetectParams(int& nImgsz, double& dConf, double& dIou);
	void ExecuteInference(const CString& strCmd, const CString& strGreen);
	void SetRunning(bool bRunning);
	
	void SetStatus(LPCTSTR lpszText);

	void LoadResultImage();

	CPreviewStatic  m_wndPreview;      
	CFinderStatic   m_wndFinder;       
	CStringArray    m_arrModels;       
	CString         m_strResultPath;   
	ULONGLONG       m_ullResultTime;   
	bool            m_bRunning;        
	bool            m_bInitializing;   
	HWND            m_hwndTarget;      
public:
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
};
