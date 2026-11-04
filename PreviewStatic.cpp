// PreviewStatic.cpp:
//
#include "pch.h"
#include "PreviewStatic.h"

CPreviewStatic::CPreviewStatic()
	: m_bHasImage(false)
{
}

BEGIN_MESSAGE_MAP(CPreviewStatic, CStatic)
	ON_WM_PAINT()
END_MESSAGE_MAP()

void CPreviewStatic::SetImage(CImage* pImage)
{
	m_bHasImage = false;
	if (pImage != NULL && !pImage->IsNull())
	{
		int w = pImage->GetWidth();
		int h = pImage->GetHeight();
		m_image.Destroy();
		if (m_image.Create(w, h, 32))
		{
			HDC hdc = m_image.GetDC();
			pImage->BitBlt(hdc, 0, 0, SRCCOPY);
			m_image.ReleaseDC();
			m_bHasImage = true;
		}
	}
	if (GetSafeHwnd() != NULL)
		Invalidate(FALSE);
}

void CPreviewStatic::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);


	dc.FillSolidRect(&rc, RGB(35, 35, 35));

	if (m_bHasImage && !m_image.IsNull())
	{
		int iw = m_image.GetWidth();
		int ih = m_image.GetHeight();
		if (iw > 0 && ih > 0)
		{
			
			double sx = (double)rc.Width() / iw;
			double sy = (double)rc.Height() / ih;
			double s = min(sx, sy);
			int w = max(1, (int)(iw * s));
			int h = max(1, (int)(ih * s));
			int x = rc.left + (rc.Width() - w) / 2;
			int y = rc.top + (rc.Height() - h) / 2;

			dc.SetStretchBltMode(HALFTONE);
			m_image.StretchBlt(dc.m_hDC, x, y, w, h, SRCCOPY);
		}
	}
	else
	{
		
		dc.SetTextColor(RGB(150, 150, 150));
		dc.SetBkMode(TRANSPARENT);
		CFont* pFont = GetFont();
		CFont* pOld = dc.SelectObject(pFont);
		dc.DrawText(_T("截图预览"), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		dc.SelectObject(pOld);
	}
}
