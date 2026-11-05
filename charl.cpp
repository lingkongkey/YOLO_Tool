#include "pch.h"








#include <vector>
#include <string>

#include <winhttp.h>
//#include <windows.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Crypt32.lib")



const WCHAR* g_yema[] =
{
	L"截图",
	L"标注",
	L"训练",
	L"测试"
};
std::vector<char> HttpPost(const std::wstring& wstrUrl, const char* pUtf8Body, DWORD dwBodyLen, const std::wstring& wstrAuthHeader, DWORD& dwCode)
{
	HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
	URL_COMPONENTS urlComp{};
	WCHAR szHost[2048]{};
	WCHAR szPath[2048]{};

	urlComp.dwStructSize = sizeof(urlComp);
	urlComp.lpszHostName = szHost;
	urlComp.dwHostNameLength = ARRAYSIZE(szHost) - 1;
	urlComp.lpszUrlPath = szPath;
	urlComp.dwUrlPathLength = ARRAYSIZE(szPath) - 1;

	dwCode = 0;
	std::string respBuf;
	char szRead[4096]{};
	DWORD dwRead = 0;
	DWORD dwResolveTimeout = 15000;
	DWORD dwConnectTimeout = 15000;
	DWORD dwSendTimeout    = 60000;
	DWORD dwRecvTimeout    = 120000;
	BOOL bHttps = 0;
	std::wstring headers = L"Content-Type: application/json\r\n";
	DWORD cbCode = sizeof(dwCode);

	if (!WinHttpCrackUrl(wstrUrl.c_str(), (DWORD)wstrUrl.size(), 0, &urlComp))
		goto clean;

	hSession = WinHttpOpen(L"MFC-HttpClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
	if (!hSession) goto clean;

	
	WinHttpSetOption(hSession, WINHTTP_OPTION_RESOLVE_TIMEOUT, &dwResolveTimeout, sizeof(dwResolveTimeout));
	WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwConnectTimeout, sizeof(dwConnectTimeout));
	WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &dwSendTimeout, sizeof(dwSendTimeout));
	WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwRecvTimeout, sizeof(dwRecvTimeout));
	WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &dwRecvTimeout, sizeof(dwRecvTimeout));

	 bHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
	hConnect = WinHttpConnect(hSession, szHost, urlComp.nPort, 0);
	if (!hConnect) goto clean;

	hRequest = WinHttpOpenRequest(hConnect, L"POST", szPath, nullptr, nullptr, nullptr, bHttps ? WINHTTP_FLAG_SECURE : 0);
	if (!hRequest) goto clean;

	if (bHttps)
	{
		DWORD dwSecOpt = SECURITY_FLAG_IGNORE_UNKNOWN_CA
			| SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
			| SECURITY_FLAG_IGNORE_CERT_CN_INVALID
			| SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
		WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecOpt, sizeof(dwSecOpt));
	}

	
	if (!wstrAuthHeader.empty())
	{
		headers += wstrAuthHeader;
	}
	WinHttpAddRequestHeaders(hRequest, headers.c_str(), (ULONG)headers.size(), WINHTTP_ADDREQ_FLAG_ADD);

	if (pUtf8Body && dwBodyLen > 0)
	{
		if (!WinHttpSendRequest(hRequest, nullptr, 0, (LPVOID)pUtf8Body, dwBodyLen, dwBodyLen, 0))
			goto clean;
	}
	else
	{
		if (!WinHttpSendRequest(hRequest, nullptr, 0, nullptr, 0, 0, 0))
			goto clean;
	}

	if (!WinHttpReceiveResponse(hRequest, nullptr))
		goto clean;


	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &dwCode, &cbCode, nullptr);


	for (;;)
	{
		ZeroMemory(szRead, sizeof(szRead));
		dwRead = 0;
		if (!WinHttpReadData(hRequest, szRead, sizeof(szRead), &dwRead) || dwRead == 0)
			break;
		respBuf.append(szRead, dwRead);
	}

clean:
	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	if (hSession) WinHttpCloseHandle(hSession);

	if (respBuf.empty())
		return {};

	std::vector<char> ret(respBuf.size() + 1);
	memcpy(ret.data(), respBuf.data(), respBuf.size());
	ret[respBuf.size()] = 0;
	return ret;
}


#include <gdiplus.h>
#include <wincrypt.h>

#pragma comment(lib,"gdiplus.lib")
using namespace Gdiplus;

GdiplusStartupInput gdiplusStartupInput;
ULONG_PTR gdiplusToken;

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
	UINT num = 0;
	UINT size = 0;
	GetImageEncodersSize(&num, &size);
	if (size == 0) return -1;
	ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL) return -1;
	GetImageEncoders(num, size, pImageCodecInfo);
	for (UINT j = 0; j < num; j++)
	{
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;
		}
	}
	free(pImageCodecInfo);
	return -1;
}


std::string ImageToJpegBase64(LPCTSTR szPath, LONG nMaxLongSide = 1280, ULONG nQuality = 80)
{
	Image img(szPath);
	if (img.GetLastStatus() != Ok) return "";


	INT w = img.GetWidth();
	INT h = img.GetHeight();
	if (w <= 0 || h <= 0) return "";

	
	LONG newW = w;
	LONG newH = h;
	if (w > nMaxLongSide || h > nMaxLongSide)
	{
		double scale = (double)nMaxLongSide / max(w, h);
		newW = (LONG)(w * scale);
		newH = (LONG)(h * scale);
	}

	Bitmap bmpDst(newW, newH, PixelFormat24bppRGB);
	Graphics g(&bmpDst);
	g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
	g.DrawImage(&img, 0, 0, newW, newH);


	IStream* pStream = NULL;
	CreateStreamOnHGlobal(NULL, TRUE, &pStream);

	CLSID clsidJpeg;
	GetEncoderClsid(L"image/jpeg", &clsidJpeg);

	EncoderParameters encoderParams;
	encoderParams.Count = 1;
	encoderParams.Parameter[0].Guid = EncoderQuality;
	encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
	encoderParams.Parameter[0].NumberOfValues = 1;
	encoderParams.Parameter[0].Value = &nQuality;

	bmpDst.Save(pStream, &clsidJpeg, &encoderParams);


	HGLOBAL hGlobal = NULL;
	GetHGlobalFromStream(pStream, &hGlobal);
	DWORD bufLen = GlobalSize(hGlobal);
	LPBYTE pData = (LPBYTE)GlobalLock(hGlobal);

	std::string b64;
	if (pData && bufLen > 0)
	{
		DWORD outLen = 0;
		CryptBinaryToStringA(pData, bufLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &outLen);
		b64.resize(outLen);
		CryptBinaryToStringA(pData, bufLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, (LPSTR)b64.c_str(), &outLen);
	}

	GlobalUnlock(hGlobal);
	pStream->Release();
	return b64;
}


typedef struct _Myaasmmddd
{
	_Myaasmmddd()
	{
		GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	}

	~_Myaasmmddd()
	{
		GdiplusShutdown(gdiplusToken);
	}
}Myaasmmddd;


Myaasmmddd g_Myaasmmddd;