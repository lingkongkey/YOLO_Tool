// EnvDownloader.h: 运行环境包按需下载与安装
//
#pragma once
#include <vector>

// 环境包信息，来自 env_manifest.ini
struct EnvPackageInfo
{
	CString   strName;
	CString   strUrl;
	CString   strSha256;
	ULONGLONG nSize = 0;
};

// 进度回调：nPercent < 0 表示进度未知；nPhase 0=下载 1=校验 2=解压
typedef void (*EnvProgressFn)(int nPercent, int nPhase, LPVOID pUser);

// 读取环境包清单；清单文件不存在时会在程序目录生成模板
bool LoadEnvManifest(std::vector<EnvPackageInfo>& arrPkgs);

// 确保 env_manifest.ini 存在（启动时调用一次），返回其路径
CString EnsureEnvManifest();

// 按名称查找清单项（不区分大小写）
bool FindEnvPackage(LPCTSTR lpszName, EnvPackageInfo& info);

// 环境包安装根目录（与环境包查找规则一致）
CString GetEnvInstallBaseDir();

// 已安装：能找到目录且含 python\python.exe
bool IsEnvInstalled(LPCTSTR lpszName);

// 系统里是否存在 NVIDIA 显卡驱动，用于判断是否值得提示下载 GPU 包
bool HasNvidiaDriver();

// 下载并安装指定环境包，失败时 strErr 给出原因
bool DownloadAndInstallEnv(LPCTSTR lpszName, EnvProgressFn pfn, LPVOID pUser, CString& strErr);
