// PageTrain.h:
//
#pragma once

#include "afxdialogex.h"
#include "LogCapture.h"

class CPageTrain : public CDialogEx
{
public:
	CPageTrain(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PAGE_TRAIN };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnBnClickedBtnTrainStart();
	afx_msg void OnBnClickedBtnTrainStop();
	afx_msg void OnBnClickedBtnExport();
	DECLARE_MESSAGE_MAP()

private:
	void FillWeightsCombo();
	
	void FillExportFormats();

	CString GetBestWeightPath();
	
	void StartExport();

	bool StartKeyoloPack();
	
	void AppendLog(const CString& strText);
	
	void SetStatus(LPCTSTR lpszText);
	
	void SetRunning(bool bRunning);
	
	void Layout();
	
	int  CountTrainImages();

	void RefreshEpochsDefault();

	int m_nLastEpochsDefault = 0;



	CRichEditCtrl m_richLog;
	CProcessLogCapture m_capture;
	bool            m_bExporting;   
	bool            m_bPackKeyolo = false;  
	CString         m_strBestPath;  
public:
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
protected:
	afx_msg LRESULT OnRecvLog(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnProcExit(WPARAM wParam, LPARAM lParam);
	long m_Maxline=0; 
	long m_line=0;	
public:
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
};
