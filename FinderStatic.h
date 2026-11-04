// FinderStatic.h
//
#pragma once


#define WM_FINDER_PICKED    (WM_APP + 201)


class CFinderStatic : public CStatic
{
public:
	CFinderStatic();

protected:
	afx_msg void OnPaint();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	
	void HighlightWindow(HWND hwnd);

	bool    m_bCapturing;  
	HWND    m_hwndHover;   
	HCURSOR m_hCross;      
};
