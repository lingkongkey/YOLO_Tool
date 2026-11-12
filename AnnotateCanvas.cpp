// AnnotateCanvas.cpp:
//

#include "pch.h"
#include "framework.h"
#include "AnnotateCanvas.h"
#include <shlwapi.h>
#include <math.h>
#pragma comment(lib, "shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CAnnotateCanvas::CAnnotateCanvas()
	: m_dZoom(1.0)
	, m_ptOrigin(0, 0)
	, m_nClass(0)
	, m_nSelBox(-1)
	, m_nDragMode(0)
	, m_nDragCorner(-1)
	, m_ptDragImg(0, 0)
	, m_ptLast(0, 0)
	, m_bPanning(false)
	, m_bModified(false)
	, m_bUserZoom(false)
	, m_nPendingSel(-1)
{
	m_rcDragStart.SetRectEmpty();
}

BEGIN_MESSAGE_MAP(CAnnotateCanvas, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

BOOL CAnnotateCanvas::Create(CWnd* pParent, UINT nID)
{
	CString strClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
		::LoadCursor(NULL, IDC_CROSS), NULL, NULL);
	return CWnd::Create(strClass, NULL,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_BORDER,
		CRect(0, 0, 100, 100), pParent, nID);
}


bool CAnnotateCanvas::LoadImage(LPCTSTR lpszImage, LPCTSTR lpszLabel)
{
	m_image.Destroy();
	m_arrBoxes.clear();
	m_nSelBox = -1;
	m_nDragMode = 0;
	m_bModified = false;
	ClearUndo();

	if (!::PathFileExists(lpszImage))
	{
		m_strImage.Empty();
		Invalidate();
		NotifyChanged();
		return false;
	}

	if (FAILED(m_image.Load(lpszImage)))
	{
		m_strImage.Empty();
		Invalidate();
		NotifyChanged();
		return false;
	}
	m_strImage = lpszImage;

	CFile file;
	if (::PathFileExists(lpszLabel) && file.Open(lpszLabel, CFile::modeRead | CFile::typeBinary))
	{
		CStringA strAll;
		ULONGLONG nLen = file.GetLength();
		if (nLen > 8 * 1024 * 1024)
			nLen = 8 * 1024 * 1024;
		file.Read(strAll.GetBuffer((int)nLen), (UINT)nLen);
		strAll.ReleaseBuffer((int)nLen);
		file.Close();

		int iw = m_image.GetWidth();
		int ih = m_image.GetHeight();
		if (iw <= 0) iw = 1;
		if (ih <= 0) ih = 1;

		int nPos = 0;
		CStringA strLine = strAll.Tokenize("\n", nPos);
		while (strLine.GetLength() > 0)
		{
			int nClass = -1;
			double cx = 0, cy = 0, w = 0, h = 0;
			if (sscanf_s(strLine, "%d %lf %lf %lf %lf", &nClass, &cx, &cy, &w, &h) == 5 && w > 0 && h > 0)
			{
				AnnoBox box;
				box.nClass = nClass;
				box.rc.left = (LONG)((cx - w / 2) * iw);
				box.rc.top = (LONG)((cy - h / 2) * ih);
				box.rc.right = (LONG)((cx + w / 2) * iw);
				box.rc.bottom = (LONG)((cy + h / 2) * ih);
				ClampBoxRect(box);
				m_arrBoxes.push_back(box);
			}
			strLine = strAll.Tokenize("\n", nPos);
		}
	}

	FitToWindow();
	Invalidate();
	NotifyChanged();
	return true;
}

bool CAnnotateCanvas::SaveLabels(LPCTSTR lpszLabelPath)
{
	if (!HasImage())
		return false;

	int iw = m_image.GetWidth();
	int ih = m_image.GetHeight();
	if (iw <= 0 || ih <= 0)
		return false;

	CStringA strAll;
	for (size_t i = 0; i < m_arrBoxes.size(); i++)
	{
		const CRect& rc = m_arrBoxes[i].rc;
		double cx = ((rc.left + rc.right) / 2.0) / iw;
		double cy = ((rc.top + rc.bottom) / 2.0) / ih;
		double w = rc.Width() / (double)iw;
		double h = rc.Height() / (double)ih;

		CStringA strLine;
		strLine.Format("%d %.6f %.6f %.6f %.6f\r\n",
			m_arrBoxes[i].nClass, cx, cy, w, h);
		strAll += strLine;
	}

	if (strAll.IsEmpty())
	{
		return false;
	}
	CFile file;
	if (!file.Open(lpszLabelPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		return false;
	if (strAll.GetLength() > 0)
		file.Write(strAll, (UINT)strAll.GetLength());
	file.Close();

	m_bModified = false;
	return true;
}

void CAnnotateCanvas::Clear()
{
	m_image.Destroy();
	m_strImage.Empty();
	m_arrBoxes.clear();
	m_nSelBox = -1;
	m_nDragMode = 0;
	m_nDragCorner = -1;
	m_bPanning = false;
	m_bModified = false;
	m_bUserZoom = false;
	m_dZoom = 1.0;
	m_ptOrigin = CPoint(0, 0);
	ClearUndo();
	Invalidate();
	NotifyChanged();
}

bool CAnnotateCanvas::BeginEdit()
{
	m_arrPending = m_arrBoxes;
	m_nPendingSel = m_nSelBox;
	return true;
}


bool CAnnotateCanvas::CommitEdit()
{
	if (SameBoxes(m_arrBoxes, m_arrPending))
		return false;

	UndoState st;
	st.boxes = m_arrPending;
	st.nSelBox = m_nPendingSel;
	m_arrUndo.push_back(st);
	if (m_arrUndo.size() > 50)
		m_arrUndo.erase(m_arrUndo.begin());
	return true;
}

bool CAnnotateCanvas::SameBoxes(const std::vector<AnnoBox>& a,
	const std::vector<AnnoBox>& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++)
	{
		if (a[i].nClass != b[i].nClass)
			return false;
		if (a[i].rc.left != b[i].rc.left || a[i].rc.top != b[i].rc.top ||
			a[i].rc.right != b[i].rc.right || a[i].rc.bottom != b[i].rc.bottom)
			return false;
	}
	return true;
}

void CAnnotateCanvas::ClearUndo()
{
	m_arrUndo.clear();
	m_arrPending.clear();
	m_nPendingSel = -1;
}


bool CAnnotateCanvas::Undo()
{
	if (m_arrUndo.empty())
		return false;

	m_nDragMode = 0;
	m_bPanning = false;
	if (GetCapture() == this)
		ReleaseCapture();

	UndoState st = m_arrUndo.back();
	m_arrUndo.pop_back();

	m_arrBoxes = st.boxes;
	m_nSelBox = st.nSelBox;
	m_bModified = true;

	Invalidate();
	NotifyChanged();
	return true;
}


void CAnnotateCanvas::FitToWindow()
{
	if (m_image.IsNull())
		return;

	CRect rc;
	GetClientRect(&rc);
	int iw = m_image.GetWidth();
	int ih = m_image.GetHeight();
	if (iw <= 0 || ih <= 0 || rc.Width() <= 0 || rc.Height() <= 0)
		return;

	double sx = (rc.Width() - 8.0) / iw;
	double sy = (rc.Height() - 8.0) / ih;
	m_dZoom = min(sx, sy);
	if (m_dZoom < 0.05)
		m_dZoom = 0.05;
	if (m_dZoom > 20.0)
		m_dZoom = 20.0;

	int dw = (int)(iw * m_dZoom + 0.5);
	int dh = (int)(ih * m_dZoom + 0.5);
	m_ptOrigin.x = (rc.Width() - dw) / 2;
	m_ptOrigin.y = (rc.Height() - dh) / 2;
	m_bUserZoom = false;
}

void CAnnotateCanvas::SetZoom(double dZoom, CPoint ptAnchor)
{
	if (m_image.IsNull())
		return;

	if (dZoom < 0.05)
		dZoom = 0.05;
	if (dZoom > 20.0)
		dZoom = 20.0;

	CPoint ptImg = ClientToImage(ptAnchor);
	m_dZoom = dZoom;
	m_ptOrigin.x = ptAnchor.x - (int)(ptImg.x * m_dZoom);
	m_ptOrigin.y = ptAnchor.y - (int)(ptImg.y * m_dZoom);
	m_bUserZoom = true;

	Invalidate();
	NotifyChanged();
}

void CAnnotateCanvas::ZoomBy(double dFactor)
{
	CRect rc;
	GetClientRect(&rc);
	SetZoom(m_dZoom * dFactor, CPoint(rc.Width() / 2, rc.Height() / 2));
}

void CAnnotateCanvas::ZoomIn()
{
	ZoomBy(1.25);
}

void CAnnotateCanvas::ZoomOut()
{
	ZoomBy(1.0 / 1.25);
}

int CAnnotateCanvas::GetZoomPercent() const
{
	return (int)(m_dZoom * 100.0 + 0.5);
}


CPoint CAnnotateCanvas::ClientToImage(CPoint pt) const
{
	if (m_dZoom <= 0)
		return CPoint(0, 0);
	return CPoint((int)((pt.x - m_ptOrigin.x) / m_dZoom),
		(int)((pt.y - m_ptOrigin.y) / m_dZoom));
}

CPoint CAnnotateCanvas::ImageToClient(CPoint pt) const
{
	return CPoint(m_ptOrigin.x + (int)(pt.x * m_dZoom),
		m_ptOrigin.y + (int)(pt.y * m_dZoom));
}

CRect CAnnotateCanvas::ImageRectToClient(const CRect& rc) const
{
	CPoint p1 = ImageToClient(CPoint(rc.left, rc.top));
	CPoint p2 = ImageToClient(CPoint(rc.right, rc.bottom));
	return CRect(p1, p2);
}


COLORREF CAnnotateCanvas::ClassColor(int nClass) const
{
	static const COLORREF s_crPalette[] =
	{
		RGB(255, 96, 96), RGB(96, 220, 96), RGB(96, 160, 255), RGB(255, 200, 64),
		RGB(255, 128, 220), RGB(64, 224, 224), RGB(200, 140, 255), RGB(180, 220, 80),
		RGB(255, 160, 80), RGB(160, 160, 255)
	};
	int nCount = (int)(sizeof(s_crPalette) / sizeof(s_crPalette[0]));
	if (nClass < 0)
		nClass = 0;
	return s_crPalette[nClass % nCount];
}

void CAnnotateCanvas::SetClassNames(const CStringArray& arrNames)
{
	m_arrClassNames.Copy(arrNames);
	Invalidate();
}

void CAnnotateCanvas::OnPaint()
{
	CPaintDC dc(this);
	CRect rcClient;
	GetClientRect(&rcClient);
	int w = max(1, rcClient.Width());
	int h = max(1, rcClient.Height());

	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, w, h);
	CBitmap* pOldBmp = mem.SelectObject(&bmp);

	mem.FillSolidRect(rcClient, RGB(56, 58, 62));

	if (!m_image.IsNull())
	{
		int iw = m_image.GetWidth();
		int ih = m_image.GetHeight();
		int dw = (int)(iw * m_dZoom + 0.5);
		int dh = (int)(ih * m_dZoom + 0.5);

		HDC hdcImg = m_image.GetDC();
		mem.SetStretchBltMode(HALFTONE);
		mem.SetBrushOrg(0, 0);
		mem.StretchBlt(m_ptOrigin.x, m_ptOrigin.y, dw, dh,
			CDC::FromHandle(hdcImg), 0, 0, iw, ih, SRCCOPY);
		m_image.ReleaseDC();

		mem.Draw3dRect(CRect(m_ptOrigin.x, m_ptOrigin.y, m_ptOrigin.x + dw, m_ptOrigin.y + dh),
			RGB(140, 140, 140), RGB(140, 140, 140));

		CBrush* pOldBrush = (CBrush*)mem.SelectStockObject(NULL_BRUSH);
		for (size_t i = 0; i < m_arrBoxes.size(); i++)
		{
			CRect rc = ImageRectToClient(m_arrBoxes[i].rc);
			COLORREF cr = ClassColor(m_arrBoxes[i].nClass);
			int nWidth = (m_nSelBox == (int)i) ? 3 : 2;
			CPen pen(PS_SOLID, nWidth, cr);
			CPen* pOldPen = mem.SelectObject(&pen);
			mem.Rectangle(rc);

			CString strName;
			if (m_arrBoxes[i].nClass >= 0 && m_arrBoxes[i].nClass < m_arrClassNames.GetCount())
				strName = m_arrClassNames[m_arrBoxes[i].nClass];
			else
				strName.Format(_T("%d"), m_arrBoxes[i].nClass);
			if (strName.IsEmpty())
				strName.Format(_T("%d"), m_arrBoxes[i].nClass);
			mem.SetBkMode(TRANSPARENT);
			mem.SetTextColor(cr);
			mem.TextOut(rc.left + 3, max(0, rc.top - 16), strName);

			if (m_nSelBox == (int)i)
			{
				CBrush brush(RGB(255, 255, 255));
				CBrush* pOldB = mem.SelectObject(&brush);
				CPen penWhite(PS_SOLID, 1, RGB(0, 0, 0));
				CPen* pOldP = mem.SelectObject(&penWhite);
				const int nR = 4;
				CPoint pts[4] = { rc.TopLeft(), CPoint(rc.right, rc.top),
					rc.BottomRight(), CPoint(rc.left, rc.bottom) };
				for (int k = 0; k < 4; k++)
					mem.Rectangle(pts[k].x - nR, pts[k].y - nR, pts[k].x + nR, pts[k].y + nR);
				mem.SelectObject(pOldP);
				mem.SelectObject(pOldB);
			}
			mem.SelectObject(pOldPen);
		}
		mem.SelectObject(pOldBrush);
	}

	dc.BitBlt(0, 0, w, h, &mem, 0, 0, SRCCOPY);
	mem.SelectObject(pOldBmp);
}

void CAnnotateCanvas::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (!m_image.IsNull() && !m_bUserZoom)
	{
		FitToWindow();
		Invalidate();
	}
}

BOOL CAnnotateCanvas::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}


BOOL CAnnotateCanvas::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint pt)
{
	CPoint ptClient = pt;
	ScreenToClient(&ptClient);

	double dFactor = zDelta > 0 ? 1.2 : 1.0 / 1.2;
	SetZoom(m_dZoom * dFactor, ptClient);
	return TRUE;
}

int CAnnotateCanvas::HitTestCorner(CPoint pt) const
{
	if (m_nSelBox < 0 || m_nSelBox >= (int)m_arrBoxes.size())
		return -1;

	CRect rc = ImageRectToClient(m_arrBoxes[m_nSelBox].rc);
	CPoint pts[4] = { rc.TopLeft(), CPoint(rc.right, rc.top),
		rc.BottomRight(), CPoint(rc.left, rc.bottom) };
	for (int i = 0; i < 4; i++)
	{
		if (abs(pt.x - pts[i].x) <= 6 && abs(pt.y - pts[i].y) <= 6)
			return i;
	}
	return -1;
}

int CAnnotateCanvas::HitTestBox(CPoint pt) const
{
	for (int i = (int)m_arrBoxes.size() - 1; i >= 0; i--)
	{
		CRect rc = ImageRectToClient(m_arrBoxes[i].rc);
		rc.InflateRect(3, 3);
		if (rc.PtInRect(pt))
			return i;

		CRect rcLabel(rc.left, max(0, rc.top - 18),
			rc.left + max(48, rc.Width()), rc.top);
		if (rcLabel.PtInRect(pt))
			return i;
	}
	return -1;
}

void CAnnotateCanvas::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	if (!HasImage())
		return;

	if (nFlags & MK_SHIFT)
	{
		m_bPanning = true;
		m_ptLast = point;
		SetCapture();
		return;
	}

	BeginEdit();

	int nHit = HitTestBox(point);
	if (nHit >= 0)
	{
		
		int nCorner = HitTestCorner(point);
		if (nHit == m_nSelBox && nCorner >= 0)
		{
			m_nDragMode = 2;
			m_nDragCorner = nCorner;
			m_rcDragStart = m_arrBoxes[m_nSelBox].rc;
			m_ptDragImg = ClientToImage(point);
			SetCapture();
			return;
		}

		m_nSelBox = nHit;
		m_nDragMode = 1;             
		m_rcDragStart = m_arrBoxes[nHit].rc;
		m_ptDragImg = ClientToImage(point);
		SetCapture();
		Invalidate();
		return;
	}

	
	CPoint ptImg = ClientToImage(point);
	AnnoBox box;
	box.nClass = m_nClass;
	box.rc = CRect(ptImg, ptImg);
	m_arrBoxes.push_back(box);
	m_nSelBox = (int)m_arrBoxes.size() - 1;
	m_nDragMode = 3;
	m_ptDragImg = ptImg;
	SetCapture();
	Invalidate();
}

void CAnnotateCanvas::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	if (m_image.IsNull())
		return;

	if (m_bPanning)
	{
		m_ptOrigin += CPoint(point.x - m_ptLast.x, point.y - m_ptLast.y);
		m_ptLast = point;
		Invalidate();
		return;
	}

	if (m_nDragMode == 0 || m_nSelBox < 0 || m_nSelBox >= (int)m_arrBoxes.size())
		return;

	CPoint ptImg = ClientToImage(point);
	int iw = m_image.GetWidth();
	int ih = m_image.GetHeight();

	if (m_nDragMode == 3)
	{
		
		AnnoBox& box = m_arrBoxes[m_nSelBox];
		box.rc.left = min(m_ptDragImg.x, ptImg.x);
		box.rc.top = min(m_ptDragImg.y, ptImg.y);
		box.rc.right = max(m_ptDragImg.x, ptImg.x);
		box.rc.bottom = max(m_ptDragImg.y, ptImg.y);
		ClampBoxRect(box);
	}
	else if (m_nDragMode == 1)
	{
		int dx = ptImg.x - m_ptDragImg.x;
		int dy = ptImg.y - m_ptDragImg.y;
		AnnoBox box;
		box.nClass = m_arrBoxes[m_nSelBox].nClass;
		box.rc = m_rcDragStart;
		box.rc.OffsetRect(dx, dy);
		if (box.rc.left < 0) box.rc.OffsetRect(-box.rc.left, 0);
		if (box.rc.top < 0) box.rc.OffsetRect(0, -box.rc.top);
		if (box.rc.right > iw) box.rc.OffsetRect(iw - box.rc.right, 0);
		if (box.rc.bottom > ih) box.rc.OffsetRect(0, ih - box.rc.bottom);
		m_arrBoxes[m_nSelBox].rc = box.rc;
	}
	else if (m_nDragMode == 2)
	{
		
		CRect rc = m_rcDragStart;
		switch (m_nDragCorner)
		{
		case 0: rc.left = ptImg.x; rc.top = ptImg.y; break;     
		case 1: rc.right = ptImg.x; rc.top = ptImg.y; break;    
		case 2: rc.right = ptImg.x; rc.bottom = ptImg.y; break; 
		case 3: rc.left = ptImg.x; rc.bottom = ptImg.y; break;  
		default: break;
		}
		CRect rcNorm(min(rc.left, rc.right), min(rc.top, rc.bottom),
			max(rc.left, rc.right), max(rc.top, rc.bottom));
		m_arrBoxes[m_nSelBox].rc = rcNorm;
		ClampBoxRect(m_arrBoxes[m_nSelBox]);
	}

	Invalidate();
	NotifyChanged();
}

void CAnnotateCanvas::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	if (GetCapture() == this)
		ReleaseCapture();

	if (m_bPanning)
	{
		m_bPanning = false;
		return;
	}

	if (m_nDragMode != 0)
	{
		if (m_nDragMode == 3 && m_nSelBox >= 0 && m_nSelBox < (int)m_arrBoxes.size())
		{
			CRect rc = m_arrBoxes[m_nSelBox].rc;
			
			if (rc.Width() < 4 || rc.Height() < 4)
			{
				m_arrBoxes.erase(m_arrBoxes.begin() + m_nSelBox);
				m_nSelBox = -1;
			}
		}
	
		if (CommitEdit())
			m_bModified = true;
		m_nDragMode = 0;
		Invalidate();
		NotifyChanged();
	}
}

void CAnnotateCanvas::OnRButtonDown(UINT /*nFlags*/, CPoint point)
{
	
	SetFocus();
	m_bPanning = true;
	m_ptLast = point;
	SetCapture();
}

void CAnnotateCanvas::OnRButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
	if (m_bPanning)
	{
		m_bPanning = false;
		if (GetCapture() == this)
			ReleaseCapture();
	}
}

void CAnnotateCanvas::OnMButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	m_bPanning = true;
	m_ptLast = point;
	SetCapture();
}

void CAnnotateCanvas::OnMButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
	if (m_bPanning)
	{
		m_bPanning = false;
		if (GetCapture() == this)
			ReleaseCapture();
	}
}

void CAnnotateCanvas::OnCaptureChanged(CWnd* /*pWnd*/)
{
	m_bPanning = false;
	m_nDragMode = 0;
}

UINT CAnnotateCanvas::OnGetDlgCode()
{
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CAnnotateCanvas::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_DELETE || nChar == VK_BACK)
		DeleteSelected();
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}



void CAnnotateCanvas::DeleteSelected()
{
	if (m_nSelBox < 0 || m_nSelBox >= (int)m_arrBoxes.size())
		return;
	BeginEdit();
	m_arrBoxes.erase(m_arrBoxes.begin() + m_nSelBox);
	m_nSelBox = -1;
	CommitEdit();
	m_bModified = true;
	Invalidate();
	NotifyChanged();
}

void CAnnotateCanvas::ClampBoxRect(AnnoBox& box) const
{
	if (m_image.IsNull())
		return;
	int iw = m_image.GetWidth();
	int ih = m_image.GetHeight();
	box.rc.left = max(0, min(box.rc.left, iw - 1));
	box.rc.top = max(0, min(box.rc.top, ih - 1));
	box.rc.right = max(box.rc.left + 1, min(box.rc.right, iw));
	box.rc.bottom = max(box.rc.top + 1, min(box.rc.bottom, ih));
}

void CAnnotateCanvas::NotifyChanged()
{
	if (::IsWindow(GetParent()->GetSafeHwnd()))
		::PostMessage(GetParent()->GetSafeHwnd(), WM_ANNO_CHANGED,
			(WPARAM)m_arrBoxes.size(), 0);
}
