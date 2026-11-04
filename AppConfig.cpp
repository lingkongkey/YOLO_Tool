// AppConfig.cpp: unified config implementation
//
#include "pch.h"
#include "framework.h"
#include "AppConfig.h"
#include "MyIniPuls.h"

// ====== singleton ======
CAppConfig& CAppConfig::Instance()
{
	static CAppConfig instance;
	return instance;
}

CAppConfig::CAppConfig()
	: m_bOpened(false)
{
	SetDefaults();
	LoadAll();
}

CAppConfig::~CAppConfig()
{
}

void CAppConfig::SetDefaults()
{
	strProjectRoot = _T("");

	strAiKey      = _T("");
	strAiProvider = _T("");
	strAiModel    = _T("");

	strTestModel  = _T("");
}

bool CAppConfig::EnsureIniOpen()
{
	if (m_bOpened) return true;
	MyIniPuls::CreateLock();
	MyIniPuls ini;
	m_bOpened = (ini.CreateIniFilePath("app_config.ini") == TRUE);
	return m_bOpened;
}

std::string CAppConfig::ToUtf8(const CString& str)
{
	if (str.IsEmpty()) return std::string();
	CStringA strA;
	int nLen = ::WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)str, -1, NULL, 0, NULL, NULL);
	if (nLen <= 0) return std::string();
	::WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)str, -1, strA.GetBuffer(nLen), nLen, NULL, NULL);
	strA.ReleaseBuffer(nLen - 1);
	return std::string(strA, strA.GetLength());
}

CString CAppConfig::FromUtf8(const std::string& str)
{
	if (str.empty()) return CString();
	int nWide = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
	if (nWide <= 0) return CString();
	CString strOut;
	::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), strOut.GetBuffer(nWide), nWide);
	strOut.ReleaseBuffer(nWide);
	return strOut;
}

CString CAppConfig::ReadStr(const char* section, const char* key, const CString& strDefault)
{
	if (!EnsureIniOpen()) return strDefault;
	MyIniPuls ini;
	ini.CreateIniFilePath("app_config.ini");
	std::string fullKey = std::string(section) + "_" + key;
	std::string val = ini.ReadIni(fullKey);
	if (val.empty()) return strDefault;
	return FromUtf8(val);
}

void CAppConfig::WriteStr(const char* section, const char* key, const CString& strValue)
{
	if (!EnsureIniOpen()) return;
	MyIniPuls ini;
	ini.CreateIniFilePath("app_config.ini");
	std::string fullKey = std::string(section) + "_" + key;
	ini.WriteIni(fullKey, ToUtf8(strValue));
}

bool CAppConfig::LoadAll()
{
	SetDefaults();
	if (!EnsureIniOpen()) return false;

	strProjectRoot = ReadStr("Project", "Root",       strProjectRoot);

	strAiKey       = ReadStr("Annotate", "AiKey",       strAiKey);
	strAiProvider  = ReadStr("Annotate", "AiProvider",  strAiProvider);
	strAiModel     = ReadStr("Annotate", "AiModel",     strAiModel);

	strTestModel   = ReadStr("Test", "Model",          strTestModel);

	return true;
}

bool CAppConfig::SaveAll()
{
	if (!EnsureIniOpen()) return false;

	WriteStr("Project",  "Root",        strProjectRoot);

	WriteStr("Annotate", "AiKey",       strAiKey);
	WriteStr("Annotate", "AiProvider",  strAiProvider);
	WriteStr("Annotate", "AiModel",     strAiModel);

	WriteStr("Test",     "Model",       strTestModel);

	return true;
}
