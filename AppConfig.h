// AppConfig.h: unified config file (bridge Unicode CStringW <-> MyIniPuls ANSI)
//
#pragma once
#include <afxwin.h>
#include <string>

class CAppConfig
{
public:
	static CAppConfig& Instance();

	bool LoadAll();
	bool SaveAll();

	// ===== Project =====
	CString strProjectRoot;

	// ===== Annotate page =====
	CString strAiKey;
	CString strAiProvider;
	CString strAiModel;

	// ===== Test page =====
	CString strTestModel;

	// ===== Capture page =====
	CString strCaptureInterval;
	CString strCaptureLimit;

	// ===== UI =====
	CString strCloseAction;

private:
	CAppConfig();
	~CAppConfig();
	CAppConfig(const CAppConfig&) = delete;
	CAppConfig& operator=(const CAppConfig&) = delete;

	static std::string ToUtf8(const CString& str);
	static CString     FromUtf8(const std::string& str);

	void SetDefaults();

	bool EnsureIniOpen();
	CString ReadStr(const char* section, const char* key, const CString& strDefault);
	void    WriteStr(const char* section, const char* key, const CString& strValue);

	bool m_bOpened;
};
