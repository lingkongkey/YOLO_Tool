// YOLO_Tool.h: YOLO_Tool
//
#pragma once

#ifndef __AFXWIN_H__
	#error ""
#endif

#include "resource.h"     




class CYOLOToolApp : public CWinApp
{
public:
	CYOLOToolApp();


public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();


	DECLARE_MESSAGE_MAP()

private:
	Gdiplus::GdiplusStartupInput m_gdiplusStartupInput;
	ULONG_PTR m_gdiplusToken;
};

extern CYOLOToolApp theApp;
