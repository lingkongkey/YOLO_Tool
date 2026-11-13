#include "pch.h"
#include "FinderStatic.h"
#include "resource.h"
CFinderStatic::CFinderStatic()
{
	
}

BEGIN_MESSAGE_MAP(CFinderStatic, CStatic)
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_TIMER()
END_MESSAGE_MAP()



void CFinderStatic::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetCapture();
	HCURSOR hc = LoadCursor(AfxGetApp()->m_hInstance, MAKEINTRESOURCE(IDC_CURSOR1));
	::SetCursor(hc);

	HICON hIcon = LoadIcon(AfxGetApp()->m_hInstance, MAKEINTRESOURCE(IDI_ICON2));
	this->SetIcon(hIcon);
	SetTimer(1, 100, NULL);
	CStatic::OnLButtonDown(nFlags, point);
}



void CFinderStatic::OnLButtonUp(UINT nFlags, CPoint point)
{
	ReleaseCapture();
	HICON hIcon = LoadIcon(AfxGetApp()->m_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	this->SetIcon(hIcon);
	KillTimer(1);
			GetParent()->SendMessage(WM_FINDER_PICKED, (WPARAM)m_Game_hWnd, 0);
	
	CStatic::OnLButtonUp(nFlags, point);
}

HWND SmallestWindowFromPoint(POINT point)
{
	RECT rect, rcTemp;
	HWND hParent, hWnd, hTemp;

	hWnd = ::WindowFromPoint(point);
	if (hWnd != NULL) {
		::GetWindowRect(hWnd, &rect);
		hParent = ::GetParent(hWnd);

		// Has window a parent ?
		if (hParent != NULL) {
			// Search down the Z-Order
			hTemp = hWnd;
			do {
				hTemp = ::GetWindow(hTemp, GW_HWNDNEXT);

				// Search window contains the point, hase the same parent, and is visible?
				::GetWindowRect(hTemp, &rcTemp);
				if (::PtInRect(&rcTemp, point) && ::GetParent(hTemp) == hParent && ::IsWindowVisible(hTemp)) {
					// Is it smaller?
					if (((rcTemp.right - rcTemp.left) * (rcTemp.bottom - rcTemp.top)) < ((rect.right - rect.left) * (rect.bottom - rect.top))) {
						// Found new smaller window!
						hWnd = hTemp;
						::GetWindowRect(hWnd, &rect);
					}
				}
			} while (hTemp != NULL);
		}
	}

	return hWnd;
}


void CFinderStatic::OnTimer(UINT_PTR nIDEvent)
{
	HWND deskWnd = ::GetDesktopWindow();
	HDC deskDC = ::GetWindowDC(deskWnd);
	int oldRop2 = SetROP2(deskDC, R2_NOTXORPEN);

	CPoint pt;
	GetCursorPos(&pt);


	HWND hTargetWnd = SmallestWindowFromPoint(pt);

	m_Game_hWnd = hTargetWnd;
	

	::GetWindowRect(m_Game_hWnd, &rc);
	CRect mRect;
	::GetWindowRect(hTargetWnd, &mRect);

	if (mRect.left < 0) mRect.left = 0;
	if (mRect.top < 0) mRect.top = 0;

	HPEN newPen = ::CreatePen(0, 3, RGB(125, 0, 125));
	HGDIOBJ oldPen = ::SelectObject(deskDC, newPen);

	::Rectangle(deskDC, mRect.left, mRect.top, mRect.right, mRect.bottom);
	Sleep(200);
	::Rectangle(deskDC, mRect.left, mRect.top, mRect.right, mRect.bottom);

	::SetROP2(deskDC, oldRop2);
	::SelectObject(deskDC, oldPen);

	::DeleteObject(newPen);
	::ReleaseDC(deskWnd, deskDC);
	deskDC = NULL;



	CStatic::OnTimer(nIDEvent);
}


