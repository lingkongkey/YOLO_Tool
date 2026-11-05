// MyIniPuls uses ANSI Win32 APIs; undef UNICODE from command line before any include.
#include "pch.h"
#include "framework.h"
#include "MyIniPuls.h"
#include <windows.h>
#include <sstream>
#include <corecrt_io.h>

HANDLE g_MyIniPuls_lock_Ha = 0;

int _MyIniPuls::file_exist(const std::string str)
{
	if (_access(str.c_str(), 0) == 0)
	{
		return 1;
	}
	return 0;
}

std::vector<std::string> _MyIniPuls::SplitString(const std::string& str, char delimiter)
{
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(str);
	while (std::getline(tokenStream, token, delimiter)) {
		long ret = -1;
		for (long i = 0; i < token.length(); i++)
		{
			if (token[i] == '\r')
			{
				token[i] = 0;
				ret = i;
			}
		}
		std::string str = token.c_str();

		tokens.push_back(str);
	}
	return tokens;
}

int _MyIniPuls::ReverseFind(std::string str, const char ch)
{
	if (str.empty())
	{
		return -1;
	}

	for (int i = static_cast<int>(str.size()) - 1; i >= 0; --i)
	{
		if (str[i] == ch)
		{
			return i; 
		}
	}

	return -1;
}

std::string _MyIniPuls::QuYunxunmul()
{
	std::string strPath;

	char strBuff[512] = { 0 };
	GetModuleFileNameA(NULL, strBuff, 512);

	int pos = ReverseFind(strBuff,'\\');
	

	char strBuff1[512] = { 0 };
	memset(strBuff1, 0, pos + 1);
	memcpy(strBuff1, strBuff, pos);
	strPath = strBuff1;

	return strPath;
}

std::string _MyIniPuls::GetIniDLLpat()
{
	std::string iniPath;


	iniPath = QuYunxunmul();
	iniPath += "\\pulg";



	if (!file_exist(iniPath)) 
	{

		::CreateDirectoryA(iniPath.c_str(), 0);
	}

	return iniPath;
}

void _MyIniPuls::CreateLock()
{
	if (g_MyIniPuls_lock_Ha == 0)
	{
		g_MyIniPuls_lock_Ha = CreateMutexA(NULL, FALSE, "MyIniPuls config");
	}
}

void _MyIniPuls::CloseLock()
{
	if (g_MyIniPuls_lock_Ha)
	{
		CloseHandle(g_MyIniPuls_lock_Ha);
		g_MyIniPuls_lock_Ha = 0;
	}

}

void _MyIniPuls::Lock()
{
	if (g_MyIniPuls_lock_Ha)
	{
		WaitForSingleObject(g_MyIniPuls_lock_Ha, INFINITE);
	}

}

void _MyIniPuls::LockUn()
{
	if (g_MyIniPuls_lock_Ha)
	{
		ReleaseMutex(g_MyIniPuls_lock_Ha);
	}

}

_MyIniPuls::_MyIniPuls()
{
	m_Ret = FALSE;
	m_Path = "";
}

_MyIniPuls::~_MyIniPuls()
{
	_MyIniPuls::CloseLock();
}


BOOL _MyIniPuls::CreateIniFilePath(const std::string IniPath)
{
	if (IniPath.empty())
	{
		return FALSE;
	}
	m_Path = GetIniDLLpat();
	std::string str = m_Path + "\\" + IniPath;
	if (!file_exist(str))
	{
		SetListPusl(str, "");
		if (!file_exist(str))
		{
			return FALSE;
		}
	}
	m_Path = str;
	m_Ret = TRUE;
	return TRUE;
}


BOOL _MyIniPuls::WriteIni(const std::string IniRva, const std::string IniStr)
{
	if (IniRva.empty() || !m_Ret)
	{
		return FALSE;
	}
	MyIniPuls::Lock();

	std::string str = GetListPuls(m_Path);
	std::vector<std::string> t_sp;
	if (!str.empty())
	{
		t_sp = SplitString(str, '\n');
	}

	bool bFind = false;
	std::string keyPrefix = IniRva + "["; 

	for (auto& line : t_sp)
	{
		if (line.size() >= keyPrefix.size() && line.substr(0, keyPrefix.size()) == keyPrefix)
		{
			line = keyPrefix + IniStr + "]";
			bFind = true;
			break;
		}
	}
	if (!bFind)
	{
		t_sp.push_back(keyPrefix + IniStr + "]");
	}

	std::string writeContent;
	for (size_t i = 0; i < t_sp.size(); i++)
	{
		writeContent += t_sp[i];
		if (i != t_sp.size() - 1)
		{
			writeContent += "\n";
		}
	}

	BOOL bRet = SetListPusl(m_Path, writeContent);

	MyIniPuls::LockUn();
	return bRet;
}



std::string _MyIniPuls::ReadIni(const std::string& IniRva)
{
	if (IniRva.empty() || !m_Ret)
	{
		return "";
	}

	MyIniPuls::Lock();
	std::string str = GetListPuls(m_Path);
	std::vector<std::string> t_sp;
	if (!str.empty())
	{
		t_sp = SplitString(str, '\n');
	}

	std::string keyPrefix = IniRva + "[";
	std::string res = "";
	for (auto& line : t_sp)
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		if (line.size() >= keyPrefix.size() && line.substr(0, keyPrefix.size()) == keyPrefix)
		{
			size_t startPos = keyPrefix.size();
			size_t endPos = line.find(']', startPos);
			if (endPos != std::string::npos)
			{
				res = line.substr(startPos, endPos - startPos);
			}
			break;
		}
	}

	MyIniPuls::LockUn();
	return res;
}


BOOL _MyIniPuls::SetListPusl(const std::string& filePath, const std::string& st)
{
	if (filePath.empty())
	{
		return FALSE;
	}

	if (file_exist(filePath))
	{
		DeleteFileA(filePath.c_str());
	}

	HANDLE hFile = CreateFileA(
		filePath.c_str(),
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,     
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	DWORD dwBytesToWrite = (DWORD)st.size();
	DWORD dwBytesWrite = 0;
	const char* tmpBuf = st.c_str();

	do
	{
		WriteFile(hFile, tmpBuf, dwBytesToWrite, &dwBytesWrite, NULL);
		dwBytesToWrite -= dwBytesWrite;
		tmpBuf += dwBytesWrite;
	} while (dwBytesToWrite > 0);

	CloseHandle(hFile);
	return TRUE; 
}


std::string _MyIniPuls::GetListPuls(const std::string& str)
{
	HANDLE hFile = CreateFileA(
		str.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return "";
	}

	DWORD fileSize = GetFileSize(hFile, NULL);
	if (fileSize == INVALID_FILE_SIZE)
	{
		CloseHandle(hFile);
		return "";
	}

	if (fileSize == 0)
	{
		CloseHandle(hFile);
		return "";
	}

	char* fileBuff = new char[fileSize + 1]{};
	DWORD realRead = 0;
	BOOL ret = ReadFile(hFile, fileBuff, fileSize, &realRead, NULL);
	CloseHandle(hFile);

	std::string res;
	if (ret)
	{
		res.assign(fileBuff, realRead);
	}

	delete[] fileBuff;
	return res;
}
