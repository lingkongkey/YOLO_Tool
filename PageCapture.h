// PageCapture.h:
//
#pragma once

#include "afxdialogex.h"
#include <atlimage.h>
#include "FinderStatic.h"
#include "PreviewStatic.h"

class CPageCapture : public CDialogEx
{
public:
	CPageCapture(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PAGE_CAPTURE };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnLbnSelchangeListCaptures();

	afx_msg void OnBnClickedBtnAuto();
	afx_msg void OnBnClickedBtnManual();
	afx_msg void OnBnClickedBtnBrowse();
	afx_msg void OnBnClickedBtnCheck();
	afx_msg LRESULT OnFinderPicked(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	
	bool ValidateTarget(bool bShowError);

	void UpdateTargetTitle();

	bool CaptureOnce();

	bool CaptureWindowToImage(HWND hwnd, bool bClientOnly, CImage& image);
	
	static void MakeThumb(CImage& src, CImage& dst);
	
	static double ThumbDiff(CImage& a, CImage& b, double& dChangedRatio);
	
	void StartAuto();
	void StopAuto();
	
	void SetUiIdle(bool bIdle);
	
	void UpdateStatus(LPCTSTR lpszState);
	
	void RefreshDirCount();
	
	CString GetDefaultSaveDir();

	void FillCaptureList();
	
	bool LoadPreviewAt(int nIndex);
	
	void ShowCaptureMenu(int x, int y);

	int  GetMaxCount();

	HWND            m_hwndTarget;   
	bool            m_bAuto;        
	int             m_nSaved;       
	int             m_nDirCount;    
	CString         m_strState;     
	CString         m_strSaveDir;   
	CStringArray    m_arrCaptures;  

	CFinderStatic   m_wndFinder;    
	CPreviewStatic  m_wndPreview;   
	CImage          m_imgPrevThumb; 
public:
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
};
