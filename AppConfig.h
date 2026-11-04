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

	// ===== Capture page =====
	CString strCaptureSaveDir;
	int     nCaptureInterval;
	bool    bCaptureClientOnly;

	// ===== Annotate page =====
	CString strAiKey;
	CString strAiProvider;
	CString strAiModel;
	CString strAnnotateConf;

	// ===== Train page =====
	CString strTrainWeights;
	CString strTrainEpochs;
	CString strTrainImgsz;
	CString strTrainBatch;
	bool    bTrainGpu;
	CString strTrainExportFormat;

	// ===== Test page =====
	bool    bTestClientOnly;

	// ===== Main window =====
	int     nWinLeft, nWinTop;
	int     nWinWidth, nWinHeight;
	bool    bWinMaximized;
	int     nCurPage;

private:
	CAppConfig();
	~CAppConfig();
	CAppConfig(const CAppConfig&) = delete;
	CAppConfig& operator=(const CAppConfig&) = delete;

	static std::string ToUtf8(const CString& str);
	static CString     FromUtf8(const std::string& str);
	static int  ToInt(const CString& str, int nDefault);
	static bool ToBool(const CString& str, bool bDefault);

	void SetDefaults();

	bool EnsureIniOpen();
	CString ReadStr(const char* section, const char* key, const CString& strDefault);
	void    WriteStr(const char* section, const char* key, const CString& strValue);
	int     ReadInt(const char* section, const char* key, int nDefault);
	void    WriteInt(const char* section, const char* key, int nValue);
	bool    ReadBool(const char* section, const char* key, bool bDefault);
	void    WriteBool(const char* section, const char* key, bool bValue);

	bool m_bOpened;
};
