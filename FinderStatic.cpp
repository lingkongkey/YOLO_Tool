#include "pch.h"
#include "FinderStatic.h"

CFinderStatic::CFinderStatic()
	: m_bCapturing(false)
	, m_hwndHover(NULL)
{
	m_hCross = ::LoadCursor(NULL, IDC_CROSS);
}

BEGIN_MESSAGE_MAP(CFinderStatic, CStatic)
	ON_WM_PAINT()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

void CFinderStatic::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);

	dc.FillSolidRect(&rc, ::GetSysColor(COLOR_BTNFACE));

	int cx = rc.Width() / 2;
	int cy = rc.Height() / 2;
	int r = min(rc.Width(), rc.Height()) / 2 - 4;
	if (r < 3)
		r = 3;

	CPen pen(PS_SOLID, 2, RGB(200, 0, 0));
	CPen* pOldPen = dc.SelectObject(&pen);
	CBrush* pOldBrush = (CBrush*)dc.SelectStockObject(HOLLOW_BRUSH);

	dc.Ellipse(cx - r, cy - r, cx + r + 1, cy + r + 1);
	dc.MoveTo(cx - r - 2, cy);
	dc.LineTo(cx + r + 3, cy);
	dc.MoveTo(cx, cy - r - 2);
	dc.LineTo(cx, cy + r + 3);

	dc.SelectObject(pOldBrush);
	dc.SelectObject(pOldPen);
}

void CFinderStatic::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_bCapturing = true;
	m_hwndHover = NULL;
	SetCapture();
	::SetCursor(m_hCross);
	CStatic::OnLButtonDown(nFlags, point);
}

void CFinderStatic::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_bCapturing)
	{
		::SetCursor(m_hCross);

		CPoint pt = point;
		ClientToScreen(&pt);

		HWND hwnd = ::WindowFromPoint(pt);
		if (hwnd != NULL)
		{
			
			HWND hwndRoot = ::GetAncestor(hwnd, GA_ROOT);
		
			HWND hwndSelf = ::GetAncestor(GetSafeHwnd(), GA_ROOT);
			if (hwndRoot != NULL && hwndRoot != hwndSelf && hwndRoot != m_hwndHover)
			{
				if (m_hwndHover != NULL)
					HighlightWindow(m_hwndHover);   
				m_hwndHover = hwndRoot;
				HighlightWindow(m_hwndHover);      
			}
		}
	}
	CStatic::OnMouseMove(nFlags, point);
}

void CFinderStatic::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bCapturing)
	{
		m_bCapturing = false;
		::ReleaseCapture();

		HWND hwndPicked = m_hwndHover;
		if (m_hwndHover != NULL)
		{
			HighlightWindow(m_hwndHover);  
			m_hwndHover = NULL;
		}

		if (hwndPicked != NULL)
			GetParent()->SendMessage(WM_FINDER_PICKED, (WPARAM)hwndPicked, 0);
	}
	CStatic::OnLButtonUp(nFlags, point);
}

void CFinderStatic::HighlightWindow(HWND hwnd)
{
	if (!::IsWindow(hwnd))
		return;

	CRect rc;
	::GetWindowRect(hwnd, &rc);

	HDC hdc = ::GetDC(NULL);  
	if (hdc == NULL)
		return;

	HPEN hPen = ::CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
	HGDIOBJ hOldPen = ::SelectObject(hdc, hPen);
	HGDIOBJ hOldBrush = ::SelectObject(hdc, ::GetStockObject(HOLLOW_BRUSH));
	int nOldROP = ::SetROP2(hdc, R2_NOT);

	::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

	::SetROP2(hdc, nOldROP);
	::SelectObject(hdc, hOldBrush);
	::SelectObject(hdc, hOldPen);
	::DeleteObject(hPen);
	::ReleaseDC(NULL, hdc);
}
