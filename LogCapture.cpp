#include "pch.h"
#include <vector>
#include "LogCapture.h"


namespace
{
	struct WaitParam
	{
		HANDLE hProcess;
		HWND   hNotifyWnd;
	};

	static CStringW Utf8ChunkToWide(const BYTE* data, DWORD len, std::vector<BYTE>& leftover)
	{
		std::vector<BYTE> buf;
		buf.reserve(leftover.size() + len);
		buf.insert(buf.end(), leftover.begin(), leftover.end());
		if (data && len > 0)
			buf.insert(buf.end(), data, data + len);
		leftover.clear();

		if (buf.empty())
			return L"";

		int cut = (int)buf.size();
		while (cut > 0)
		{
			BYTE last = buf[cut - 1];
			if ((last & 0x80) == 0)
				break;
			if ((last & 0xC0) == 0xC0)
			{
				leftover.assign(buf.begin() + cut, buf.end());
				buf.resize(cut);
				break;
			}
			cut--;
		}

		if (buf.empty())
			return L"";

		int wlen = MultiByteToWideChar(CP_UTF8, 0,
			reinterpret_cast<LPCSTR>(buf.data()), (int)buf.size(),
			nullptr, 0);
		if (wlen <= 0)
			return L"";

		CStringW res;
		res.GetBufferSetLength(wlen);
		MultiByteToWideChar(CP_UTF8, 0,
			reinterpret_cast<LPCSTR>(buf.data()), (int)buf.size(),
			res.GetBuffer(), wlen);
		res.ReleaseBuffer();
		return res;
	}
}

// 去掉 VT100 ANSI 转义序列（如 ESC[32m），否则 MFC Edit 控件会显示成方块乱码
static void StripAnsi(CStringW& s)
{
	CStringW out;
	int n = s.GetLength();
	out.Preallocate(n);
	for (int i = 0; i < n; ++i)
	{
		if (s[i] == L'\x1b' && i + 1 < n && s[i + 1] == L'[')
		{
			i += 2;
			while (i < n && !(s[i] >= L'@' && s[i] <= L'~'))
				++i;
			continue;
		}
		out += s[i];
	}
	s = out;
}

void CProcessLogCapture::FeedBytes(
	const BYTE* data, DWORD len,
	std::vector<BYTE>& utf8Leftover,
	std::vector<BYTE>& lineBuf,
	HWND hWnd, BOOL bErr)
{
	// 结束当前行：UTF-8 转码 -> 去 ANSI 转义 -> 投递到 UI 线程
	auto FlushLine = [&]()
	{
		if (lineBuf.empty())
			return;
		CStringW line = Utf8ChunkToWide(lineBuf.data(), (DWORD)lineBuf.size(), utf8Leftover);
		lineBuf.clear();
		StripAnsi(line);
		if (!line.IsEmpty())
		{
			::PostMessage(hWnd, WM_USER_RECVLOG,
				bErr ? 1 : 0,
				(LPARAM)(new CStringW(line)));
		}
	};

	if (!data || len == 0)
	{
		FlushLine();
		return;
	}

	for (DWORD i = 0; i < len; ++i)
	{
		BYTE b = data[i];
		// \n 与 \r 都视为行结束：tqdm 进度条用 \r 原地刷新，按行投递才能实时看到进度
		if (b == '\n' || b == '\r')
			FlushLine();
		else
			lineBuf.push_back(b);
	}
}

DWORD WINAPI CProcessLogCapture::ReadPipeThreadProc(LPVOID lpParam)
{
	LogCaptureParam* pParam = reinterpret_cast<LogCaptureParam*>(lpParam);
	if (!pParam) return 0;

	BYTE buf[8192];
	DWORD dwRead = 0;
	std::vector<BYTE> utf8Leftover;
	std::vector<BYTE> lineBuffer;

	while (TRUE)
	{
		BOOL bOk = ReadFile(pParam->hPipeRead, buf, sizeof(buf), &dwRead, nullptr);
		if (!bOk || dwRead == 0)
		{
			DWORD err = GetLastError();
			if (err == ERROR_BROKEN_PIPE)
			{
				FeedBytes(nullptr, 0, utf8Leftover, lineBuffer, pParam->hNotifyWnd, pParam->bIsStdErr);
			}
			break;
		}

		FeedBytes(buf, dwRead, utf8Leftover, lineBuffer, pParam->hNotifyWnd, pParam->bIsStdErr);
	}

	CloseHandle(pParam->hPipeRead);
	delete pParam;
	return 0;
}

DWORD WINAPI CProcessLogCapture::WaitExitThreadProc(LPVOID lpParam)
{
	WaitParam* pWait = reinterpret_cast<WaitParam*>(lpParam);
	if (!pWait)
		return 0;

	WaitForSingleObject(pWait->hProcess, INFINITE);
	// 退出码一起抛给 UI 线程：0 成功，非 0 失败
	DWORD dwExitCode = 0;
	::GetExitCodeProcess(pWait->hProcess, &dwExitCode);
	::PostMessage(pWait->hNotifyWnd, WM_USER_PROC_EXIT, (WPARAM)dwExitCode, 0);

	CloseHandle(pWait->hProcess);
	delete pWait;
	return 0;
}

CProcessLogCapture::CProcessLogCapture()
	: m_hOutRead(NULL), m_hOutWrite(NULL)
	, m_hErrRead(NULL), m_hErrWrite(NULL)
	, m_hProcess(NULL)
	, m_hThreadOut(NULL), m_hThreadErr(NULL), m_hThreadWaitExit(NULL)
	, m_bRunning(FALSE)
{
}

CProcessLogCapture::~CProcessLogCapture()
{
	Stop();
}

BOOL CProcessLogCapture::StartProcess(LPCTSTR szCmdLine, HWND hNotifyWnd)
{
	Stop();

	SECURITY_ATTRIBUTES sa;
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	if (!CreatePipe(&m_hOutRead, &m_hOutWrite, &sa, 16 * 1024))
		return FALSE;
	if (!CreatePipe(&m_hErrRead, &m_hErrWrite, &sa, 16 * 1024))
	{
		CloseHandle(m_hOutRead);  m_hOutRead = nullptr;
		CloseHandle(m_hOutWrite); m_hOutWrite = nullptr;
		return FALSE;
	}

	SetHandleInformation(m_hOutRead, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(m_hErrRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.hStdOutput = m_hOutWrite;
	si.hStdError = m_hErrWrite;
	si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));

	CStringW cmd(szCmdLine);
	WCHAR* pCmd = cmd.GetBuffer();

	BOOL bRet = CreateProcessW(
		nullptr,
		pCmd,
		nullptr,
		nullptr,
		TRUE,
		0,
		nullptr,
		nullptr,
		&si,
		&pi
	);
	cmd.ReleaseBuffer();

	if (!bRet)
	{
		CloseHandle(m_hOutRead);
		CloseHandle(m_hOutWrite);
		CloseHandle(m_hErrRead);
		CloseHandle(m_hErrWrite);
		m_hOutRead = m_hOutWrite = nullptr;
		m_hErrRead = m_hErrWrite = nullptr;
		return FALSE;
	}

	m_hProcess = pi.hProcess;
	CloseHandle(pi.hThread);

	CloseHandle(m_hOutWrite);
	CloseHandle(m_hErrWrite);
	m_hOutWrite = nullptr;
	m_hErrWrite = nullptr;

	m_bRunning = TRUE;

	// stdout 读线程
	LogCaptureParam* pOut = new LogCaptureParam;
	pOut->hNotifyWnd = hNotifyWnd;
	pOut->hPipeRead = m_hOutRead;
	pOut->bIsStdErr = FALSE;
	m_hThreadOut = CreateThread(nullptr, 0, ReadPipeThreadProc, pOut, 0, nullptr);

	// stderr 读线程
	LogCaptureParam* pErr = new LogCaptureParam;
	pErr->hNotifyWnd = hNotifyWnd;
	pErr->hPipeRead = m_hErrRead;
	pErr->bIsStdErr = TRUE;
	m_hThreadErr = CreateThread(nullptr, 0, ReadPipeThreadProc, pErr, 0, nullptr);

	// 等待进程退出线程【修复点】
	WaitParam* pWaitParam = new WaitParam;
	pWaitParam->hProcess = nullptr;
	pWaitParam->hNotifyWnd = hNotifyWnd;
	DuplicateHandle(GetCurrentProcess(), m_hProcess,
		GetCurrentProcess(), &pWaitParam->hProcess,
		0, FALSE, DUPLICATE_SAME_ACCESS);
	if (pWaitParam->hProcess)
		m_hThreadWaitExit = CreateThread(nullptr, 0, WaitExitThreadProc, pWaitParam, 0, nullptr);
	if (!m_hThreadWaitExit)
		delete pWaitParam;

	return TRUE;
}

void CProcessLogCapture::Stop()
{
	// 进程还在运行：必须先结束，否则读取线程卡在 ReadFile、等待线程卡在
	// WaitForSingleObject，本函数会永久阻塞
	_try{
	if (m_hProcess)
	{
		DWORD dwExit = 0;
		if (::GetExitCodeProcess(m_hProcess, &dwExit) && dwExit == STILL_ACTIVE)
			::TerminateProcess(m_hProcess, 1);
	}

	m_bRunning = FALSE;

	if (m_hThreadOut)
	{
		WaitForSingleObject(m_hThreadOut, INFINITE);
		CloseHandle(m_hThreadOut);
		m_hThreadOut = nullptr;
	}
	if (m_hThreadErr)
	{
		WaitForSingleObject(m_hThreadErr, INFINITE);
		CloseHandle(m_hThreadErr);
		m_hThreadErr = nullptr;
	}
	if (m_hThreadWaitExit)
	{
		WaitForSingleObject(m_hThreadWaitExit, INFINITE);
		CloseHandle(m_hThreadWaitExit);
		m_hThreadWaitExit = nullptr;
	}

	/*if (m_hOutRead) { CloseHandle(m_hOutRead); m_hOutRead = nullptr; }
	if (m_hErrRead) { CloseHandle(m_hErrRead); m_hErrRead = nullptr; }*/

	if (m_hProcess)
	{
		CloseHandle(m_hProcess);
		m_hProcess = nullptr;
	}
	}
		_except(EXCEPTION_EXECUTE_HANDLER)
	{

	}
}
