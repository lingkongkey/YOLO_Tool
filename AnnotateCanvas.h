#pragma once

#include <atlimage.h>
#include <vector>

#define WM_ANNO_CHANGED     (WM_APP + 220)

struct AnnoBox
{
	int   nClass;
	CRect rc;
};

class CAnnotateCanvas : public CWnd
{
public:
	CAnnotateCanvas();

	BOOL Create(CWnd* pParent, UINT nID);
	
	bool LoadImage(LPCTSTR lpszImage, LPCTSTR lpszLabel);
	bool SaveLabels(LPCTSTR lpszLabelPath);
	void Clear();
	bool HasImage() const { return !m_image.IsNull(); }
	CString GetImagePath() const { return m_strImage; }

	void SetClassIndex(int nClass) { m_nClass = nClass; }
	void SetClassNames(const CStringArray& arrNames);
	void ZoomIn();
	void ZoomOut();
	void FitToWindow();
	bool Undo();
	bool CanUndo() const { return !m_arrUndo.empty(); }

	int GetZoomPercent() const;
	int GetBoxCount() const { return (int)m_arrBoxes.size(); }
	bool IsModified() const { return m_bModified; }
	void SetModified(bool bModified) { m_bModified = bModified; }
	void DeleteSelected();

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	DECLARE_MESSAGE_MAP()

private:
	CPoint ClientToImage(CPoint pt) const;
	CPoint ImageToClient(CPoint pt) const;
	CRect  ImageRectToClient(const CRect& rc) const;
	
	void   SetZoom(double dZoom, CPoint ptAnchor);
	
	void   ZoomBy(double dFactor);
	
	int    HitTestBox(CPoint pt) const;
	int    HitTestCorner(CPoint pt) const;   
	void   ClampBoxRect(AnnoBox& box) const;
	void   NotifyChanged();
	COLORREF ClassColor(int nClass) const;

	struct UndoState
	{
		std::vector<AnnoBox> boxes;
		int                  nSelBox;
	};
	bool BeginEdit();                      
	bool CommitEdit();                     
	static bool SameBoxes(const std::vector<AnnoBox>& a,
		const std::vector<AnnoBox>& b);
	void ClearUndo();

	std::vector<UndoState> m_arrUndo;       
	std::vector<AnnoBox>   m_arrPending;    
	int                    m_nPendingSel;

	CImage          m_image;
	CString         m_strImage;
	CStringArray    m_arrClassNames;   
	std::vector<AnnoBox> m_arrBoxes;

	double          m_dZoom;     
	CPoint          m_ptOrigin;  

	int             m_nClass;     
	int             m_nSelBox;    
	int             m_nDragMode;  
	int             m_nDragCorner;
	CPoint          m_ptDragImg;  
	CPoint          m_ptLast;     
	CRect           m_rcDragStart;
	bool            m_bPanning;
	bool            m_bModified;
	bool            m_bUserZoom;  
};
