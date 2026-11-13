// FinderStatic.h
//
#pragma once


#define WM_FINDER_PICKED    (WM_APP + 201)


class CFinderStatic : public CStatic
{
public:
	CFinderStatic();
	HWND  m_Game_hWnd;
	DWORD m_Game_tid;
	DWORD m_Game_pid;
	DWORD m_BackColor = RGB(0x6c, 0xe2, 0x6c); 
	RECT rc;
protected:
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()

  
	    
};
