// YOLO_Tool.cpp:
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

HANDLE g_Maxmux = 0;
CYOLOToolApp theApp;

BOOL CYOLOToolApp::InitInstance()
{
	/*
	// {E240184D-B1EB-42FE-89CD-5F386DE8C041}
IMPLEMENT_OLECREATE(<<class>>, <<external_name>>,
0xe240184d, 0xb1eb, 0x42fe, 0x89, 0xcd, 0x5f, 0x38, 0x6d, 0xe8, 0xc0, 0x41);

	*/
	g_Maxmux = CreateMutexA(NULL, TRUE, "{E240184D-B1EB-42FE-89CD-5F386DE8C041}");
	DWORD dwErr = GetLastError();
	if (g_Maxmux == NULL || dwErr == ERROR_ALREADY_EXISTS)
	{
		if (g_Maxmux != NULL)
		{
			CloseHandle(g_Maxmux);
			g_Maxmux = NULL;
		}
		return FALSE;
	}


	Gdiplus::GdiplusStartup(&m_gdiplusToken, &m_gdiplusStartupInput, NULL);

	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	CYOLOToolDlg dlg;
	m_pMainWnd = &dlg;
	dlg.DoModal();
	CloseHandle(g_Maxmux);
	return FALSE;
}

int CYOLOToolApp::ExitInstance()
{
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
	return CWinApp::ExitInstance();
}
