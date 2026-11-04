
#pragma once

#include <atlimage.h>

class CPreviewStatic : public CStatic
{
public:
	CPreviewStatic();
	void SetImage(CImage* pImage);

protected:
	afx_msg void OnPaint();
	DECLARE_MESSAGE_MAP()

private:
	CImage  m_image;
	bool    m_bHasImage;
};
