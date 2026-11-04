#pragma once
#include <string>
#include <vector>
#include <windows.h>
typedef struct _MyIniPuls
{
public:
	static void CreateLock();

	static void CloseLock();
	static void  Lock();
	static void  LockUn();
	_MyIniPuls();
	~_MyIniPuls();
public:
	
	BOOL CreateIniFilePath(const std::string IniPath);

	
	BOOL WriteIni(const std::string IniRva, const std::string IniStr);

	
	std::string ReadIni(const std::string& IniRva);
	
	int  file_exist(const std::string str);
	
	std::vector<std::string> SplitString(const std::string& str, char delimiter);

	
	int ReverseFind(std::string str, const char ch);

	std::string QuYunxunmul();
	
	std::string  GetIniDLLpat();
	


private:
	BOOL    m_Ret;//ini
	
	std::string m_Path;//inipath



	BOOL SetListPusl(const std::string& filePath, const std::string& st);


	std::string GetListPuls(const std::string& str);

}MyIniPuls, * PMyIniPuls;

