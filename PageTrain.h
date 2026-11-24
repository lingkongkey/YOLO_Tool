// PageTrain.h:
//
#pragma once

#include "afxdialogex.h"
#include "LogCapture.h"

#define WM_USER_GPU_PROBE (WM_USER + 102)
#define WM_USER_ENV_PROMPT (WM_USER + 103)    // 提示下载运行环境包
#define WM_USER_ENV_PROGRESS (WM_USER + 104)  // 环境包下载进度
#define WM_USER_ENV_DONE (WM_USER + 105)      // 环境包安装结束

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

public:
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

	void GetPtPath();
	std::vector<YOLOPT>&  m_ptArr;

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
	afx_msg LRESULT OnGpuProbeDone(WPARAM wParam, LPARAM lParam);
	bool m_bGpuProbeDone = false;   // 后台探测是否已结束
	bool m_bGpuUsable = false;      // 探测结果：GPU 是否真的能跑
	afx_msg LRESULT OnEnvPrompt(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnEnvProgress(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnEnvDone(WPARAM wParam, LPARAM lParam);
	void PromptDownloadEnv(LPCTSTR lpszName, LPCTSTR lpszWhy);
	void StartEnvDownload(LPCTSTR lpszName);
	bool m_bEnvDownloading = false;
	CString m_strEnvDownloadName;
public:
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnCbnSelchangeComboExportFmt2();
};
