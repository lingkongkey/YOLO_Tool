// YOLO_Tool.cpp: 定义应用程序的类行为。
//

#include "pch.h"
#include "YOLO_Tool.h"
#include "YOLO_ToolDlg.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CYOLOToolApp

BEGIN_MESSAGE_MAP(CYOLOToolApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()



CYOLOToolApp::CYOLOToolApp()
	: m_gdiplusToken(0)
{
	
}

CYOLOToolApp theApp;

BOOL CYOLOToolApp::InitInstance()
{

	Gdiplus::GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, NULL);

	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	CYOLOToolDlg dlg;
	m_pMainWnd = &dlg;
	dlg.DoModal();

	return FALSE;
}

int CYOLOToolApp::ExitInstance()
{
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
	return CWinApp::ExitInstance();
}
