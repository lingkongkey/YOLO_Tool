// PageAnnotate.h:
//
#pragma once

#include "afxdialogex.h"

#include <vector>

#include "AnnotateCanvas.h"
#include "LogCapture.h"

class CPageAnnotate : public CDialogEx
{
public:
	CPageAnnotate(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PAGE_ANNOTATE };
#endif

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedBtnSaveClasses();
	afx_msg void OnBnClickedBtnAnnotate();
	afx_msg void OnBnClickedBtnAiAnnotate();
	afx_msg void OnCbnSelchangeComboAiProvider();
	afx_msg void OnBnClickedBtnLabelImg();
	afx_msg void OnBnClickedBtnPrev();
	afx_msg void OnBnClickedBtnNext();
	afx_msg void OnBnClickedBtnRefresh();
	afx_msg void OnBnClickedBtnZoomIn();
	afx_msg void OnBnClickedBtnZoomOut();
	afx_msg void OnBnClickedBtnZoomFit();
	afx_msg void OnBnClickedBtnSaveLabels();
	afx_msg void OnBnClickedBtnDelBox();
	afx_msg void OnLbnSelchangeListImages();
	afx_msg void OnCbnSelchangeComboClasses();
	afx_msg LRESULT OnAnnoChanged(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnRecvLog(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnProcExit(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	
	void FillWeightsCombo();

	bool ParseClassNames(CStringArray& arrNames);
	
	bool SaveClasses();
	
	void FillClassCombo();
	//
	void FillImageList();
	//
	void ShowListMenu(int x, int y);

	bool LoadImageAt(int nIndex, bool bKeepListSelection = false);
	
	int  GetSelectedCount();
	
	bool HasMultiSelection();
	
	bool SaveCurrentLabels();

	bool SaveAndNext();
	
	CString GetLabelPathFor(LPCTSTR lpszImage);

	void AppendLog(const CString& strText);
	
	void SetStatus(LPCTSTR lpszText);
	
	void UpdateAnnoStatus();
	
	void SetRunning(bool bRunning);
	
	void Layout();
	void StartAiAnnotateImage();
	
	void HandleAiProcExit(DWORD dwExit);
	
	int  MergeAiClass(const CString& strAiName);

	void SetAiRunningUI(bool bRun);


	CAnnotateCanvas m_canvas;
	CProcessLogCapture m_capture;
	CStringArray    m_arrImages;   
	int             m_nCurImage;

	struct AiBox { double cx, cy, w, h; CString strName; };
	bool            m_bAiRunning = false;      
	bool            m_bAiStop = false;         
	bool            m_bAiGotResult = false;    
	bool            m_bAiError = false;        
	bool            m_bAiClassesDirty = false; 
	int             m_nAiIndex = 0;            
	CString         m_strAiKey;                
	CString         m_strAiBase;               
	CString         m_strAiModel;              
	CStringArray    m_arrAiTodo;               
	std::vector<AiBox> m_vecAiBoxes;           
public:
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
};
