#pragma once
#include <afxwin.h>
#include <vector>
#define WM_USER_RECVLOG   (WM_USER + 100)
#define WM_USER_PROC_EXIT (WM_USER + 101)

struct LogCaptureParam
{
	HWND hNotifyWnd;
	HANDLE hPipeRead;
	BOOL bIsStdErr;
};

class CProcessLogCapture
{
public:
	CProcessLogCapture();
	~CProcessLogCapture();

	BOOL StartProcess(LPCTSTR szCmdLine, HWND hNotifyWnd);
	void Stop();
	BOOL IsRunning() const { return m_bRunning; }

private:
	static DWORD WINAPI ReadPipeThreadProc(LPVOID lpParam);
	static DWORD WINAPI WaitExitThreadProc(LPVOID lpParam);

	static void FeedBytes(
		const BYTE* data, DWORD len,
		std::vector<BYTE>& utf8Leftover,
		std::vector<BYTE>& lineBuf,
		HWND hWnd, BOOL bErr);

	HANDLE m_hOutRead;
	HANDLE m_hOutWrite;
	HANDLE m_hErrRead;
	HANDLE m_hErrWrite;

	HANDLE m_hProcess;
	HANDLE m_hThreadOut;
	HANDLE m_hThreadErr;
	HANDLE m_hThreadWaitExit;

	BOOL m_bRunning;
};
