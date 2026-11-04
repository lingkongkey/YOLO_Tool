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
	// Capture
	strCaptureSaveDir = _T("");
	nCaptureInterval  = 1000;
	bCaptureClientOnly = true;

	// Annotate
	strAiKey       = _T("");
	strAiProvider  = _T("doubao");
	strAiModel     = _T("Doubao - Seed - Evolving");
	strAnnotateConf = _T("0.5");

	// Train
	strTrainWeights      = _T("");
	strTrainEpochs       = _T("100");
	strTrainImgsz        = _T("640");
	strTrainBatch        = _T("4");
	bTrainGpu            = false;
	strTrainExportFormat = _T("ncnn");

	// Test
	bTestClientOnly = true;

	// Window
	nWinLeft   = CW_USEDEFAULT;
	nWinTop    = CW_USEDEFAULT;
	nWinWidth  = 1280;
	nWinHeight = 800;
	bWinMaximized = false;
	nCurPage   = 0;
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

int CAppConfig::ToInt(const CString& str, int nDefault)
{
	if (str.IsEmpty()) return nDefault;
	return _ttoi(str);
}

bool CAppConfig::ToBool(const CString& str, bool bDefault)
{
	if (str.IsEmpty()) return bDefault;
	CString s = str;
	s.Trim();
	if (s == _T("1") || s.CompareNoCase(_T("true")) == 0) return true;
	if (s == _T("0") || s.CompareNoCase(_T("false")) == 0) return false;
	return bDefault;
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

int CAppConfig::ReadInt(const char* section, const char* key, int nDefault)
{
	CString v = ReadStr(section, key, CString());
	if (v.IsEmpty()) return nDefault;
	return ToInt(v, nDefault);
}

void CAppConfig::WriteInt(const char* section, const char* key, int nValue)
{
	CString v; v.Format(_T("%d"), nValue);
	WriteStr(section, key, v);
}

bool CAppConfig::ReadBool(const char* section, const char* key, bool bDefault)
{
	CString v = ReadStr(section, key, CString());
	if (v.IsEmpty()) return bDefault;
	return ToBool(v, bDefault);
}

void CAppConfig::WriteBool(const char* section, const char* key, bool bValue)
{
	WriteStr(section, key, bValue ? _T("1") : _T("0"));
}

bool CAppConfig::LoadAll()
{
	SetDefaults();
	if (!EnsureIniOpen()) return false;

	strCaptureSaveDir  = ReadStr("Capture", "SaveDir",     strCaptureSaveDir);
	nCaptureInterval   = ReadInt ("Capture", "Interval",   nCaptureInterval);
	bCaptureClientOnly = ReadBool("Capture", "ClientOnly", bCaptureClientOnly);

	strAiKey       = ReadStr("Annotate", "AiKey",       strAiKey);
	strAiProvider  = ReadStr("Annotate", "AiProvider",  strAiProvider);
	strAiModel     = ReadStr("Annotate", "AiModel",     strAiModel);
	strAnnotateConf= ReadStr("Annotate", "Conf",        strAnnotateConf);

	strTrainWeights      = ReadStr("Train", "Weights",      strTrainWeights);
	strTrainEpochs       = ReadStr("Train", "Epochs",       strTrainEpochs);
	strTrainImgsz        = ReadStr("Train", "Imgsz",        strTrainImgsz);
	strTrainBatch        = ReadStr("Train", "Batch",        strTrainBatch);
	bTrainGpu            = ReadBool("Train", "Gpu",         bTrainGpu);
	strTrainExportFormat = ReadStr("Train", "ExportFormat", strTrainExportFormat);

	bTestClientOnly = ReadBool("Test", "ClientOnly", bTestClientOnly);

	nWinLeft      = ReadInt ("Window", "Left",      nWinLeft);
	nWinTop       = ReadInt ("Window", "Top",       nWinTop);
	nWinWidth     = ReadInt ("Window", "Width",     nWinWidth);
	nWinHeight    = ReadInt ("Window", "Height",    nWinHeight);
	bWinMaximized = ReadBool("Window", "Maximized", bWinMaximized);
	nCurPage      = ReadInt ("Window", "CurPage",   nCurPage);

	return true;
}

bool CAppConfig::SaveAll()
{
	if (!EnsureIniOpen()) return false;

	WriteStr ("Capture", "SaveDir",     strCaptureSaveDir);
	WriteInt ("Capture", "Interval",    nCaptureInterval);
	WriteBool("Capture", "ClientOnly",  bCaptureClientOnly);

	WriteStr ("Annotate", "AiKey",       strAiKey);
	WriteStr ("Annotate", "AiProvider",  strAiProvider);
	WriteStr ("Annotate", "AiModel",     strAiModel);
	WriteStr ("Annotate", "Conf",       strAnnotateConf);

	WriteStr ("Train", "Weights",        strTrainWeights);
	WriteStr ("Train", "Epochs",         strTrainEpochs);
	WriteStr ("Train", "Imgsz",          strTrainImgsz);
	WriteStr ("Train", "Batch",          strTrainBatch);
	WriteBool("Train", "Gpu",            bTrainGpu);
	WriteStr ("Train", "ExportFormat",   strTrainExportFormat);

	WriteBool("Test", "ClientOnly",     bTestClientOnly);

	WriteInt ("Window", "Left",          nWinLeft);
	WriteInt ("Window", "Top",           nWinTop);
	WriteInt ("Window", "Width",         nWinWidth);
	WriteInt ("Window", "Height",        nWinHeight);
	WriteBool("Window", "Maximized",     bWinMaximized);
	WriteInt ("Window", "CurPage",       nCurPage);

	return true;
}
