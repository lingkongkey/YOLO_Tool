// PageTest.cpp:
//

#include "pch.h"
#include "framework.h"
#include "YOLO_Tool.h"
#include "PageTest.h"
#include "GreenEnv.h"
#include "UiUtil.h"
#include "afxdialogex.h"
#include <afxdlgs.h>
#include <shlwapi.h>
#include <shlobj.h>
#pragma comment(lib, "shlwapi.lib")



#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "net.h"
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

CComboBox* g_pComboTEST = 0;

namespace
{
	const UINT kPwRenderFullContent = 0x00000002;

	struct NcnnBox
	{
		int   nClass;
		float fScore;
		float x, y, w, h;
	};

	bool ReadFileBytes(const CString& strPath, std::string& data)
	{
		FILE* fp = NULL;
		if (_wfopen_s(&fp, (LPCTSTR)strPath, L"rb") != 0 || fp == NULL)
			return false;

		char buf[8192];
		size_t n = 0;
		while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
			data.append(buf, n);
		fclose(fp);
		return true;
	}

	CString Utf8ToText(const std::string& s)
	{
		if (s.empty())
			return CString();

		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
		if (n <= 0)
			return CString();

		std::wstring w((size_t)n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
		return CString(w.c_str());
	}


	void ParseBlobNames(const std::string& text, std::string& strIn, std::string& strOut)
	{
		std::istringstream iss(text);
		std::string line;
		while (std::getline(iss, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			std::istringstream ls(line);
			std::vector<std::string> tk;
			std::string t;
			while (ls >> t)
				tk.push_back(t);
			if (tk.size() < 5)
				continue;


			int nBottom = atoi(tk[2].c_str());
			int nTop = atoi(tk[3].c_str());
			if (nBottom < 0 || nTop < 1)
				continue;
			int nTopIdx = 4 + nBottom + nTop - 1;
			if (nTopIdx >= (int)tk.size())
				continue;

			if (tk[0] == "Input")
				strIn = tk[4 + nBottom];
			strOut = tk[nTopIdx];
		}
	}


	void ParseMetadataNames(const std::string& text, std::vector<std::string>& names)
	{
		std::istringstream iss(text);
		std::string line;
		bool bInNames = false;
		while (std::getline(iss, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (!bInNames)
			{
				if (line.compare(0, 6, "names:") == 0)
					bInNames = true;
				continue;
			}
			if (line.empty())
				continue;
			if (line[0] != ' ' && line[0] != '\t')
				break;

			size_t nColon = line.find(':');
			if (nColon == std::string::npos)
				continue;

			int nIdx = atoi(line.substr(0, nColon).c_str());
			std::string strName = line.substr(nColon + 1);
			size_t nFirst = strName.find_first_not_of(" \t");
			size_t nLast = strName.find_last_not_of(" \t");
			strName = (nFirst == std::string::npos)
				? std::string()
				: strName.substr(nFirst, nLast - nFirst + 1);

			if (nIdx < 0)
				continue;
			if ((int)names.size() <= nIdx)
				names.resize((size_t)nIdx + 1);
			names[(size_t)nIdx] = strName;
		}
	}


	std::vector<std::string> LoadClassNames(const CString& strParamPath)
	{
		std::vector<std::string> names;
		CString strDir = strParamPath.Left(strParamPath.ReverseFind(_T('\\')));

		std::string meta;
		if (ReadFileBytes(strDir + _T("\\metadata.yaml"), meta))
		{
			ParseMetadataNames(meta, names);
			if (!names.empty())
				return names;
		}

		CString strClasses = GetProjectDatasetDir();
		if (!strClasses.IsEmpty())
			strClasses += _T("\\classes.txt");
		if (!::PathFileExists(strClasses))
		{
			CString strGreen = GetGreenRoot();
			strClasses = strGreen.IsEmpty() ? CString() : strGreen + _T("\\config\\classes.txt");
		}

		std::string text;
		if (!strClasses.IsEmpty() && ReadFileBytes(strClasses, text))
		{
			std::istringstream iss(text);
			std::string line;
			while (std::getline(iss, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (!line.empty())
					names.push_back(line);
			}
		}
		return names;
	}


	bool FindNcnnPair(const CString& strDir, CString& strParam, CString& strBin)
	{
		CString strFound = strDir + _T("\\model.ncnn.param");
		if (!::PathFileExists(strFound))
		{
			strFound.Empty();
			CFileFind finder;
			BOOL bMore = finder.FindFile(strDir + _T("\\*.param"));
			while (bMore)
			{
				bMore = finder.FindNextFile();
				if (!finder.IsDirectory())
				{
					strFound = finder.GetFilePath();
					break;
				}
			}
			finder.Close();
		}
		if (strFound.IsEmpty())
			return false;

		CString strBinPath = strFound.Left(strFound.ReverseFind(_T('.'))) + _T(".bin");
		if (!::PathFileExists(strBinPath))
			return false;

		strParam = strFound;
		strBin = strBinPath;
		return true;
	}


	bool RunNcnnExport(const CString& strGreen, const CString& strPython,
		const CString& strWeights, int nImgsz)
	{
		CString strScript = strGreen + _T("\\scripts\\export_model.py");
		if (!::PathFileExists(strScript) || !::PathFileExists(strWeights))
			return false;

		CString strCmd;
		strCmd.Format(_T("\"%s\" -X utf8 -u \"%s\" --weights \"%s\" --format ncnn --imgsz %d"),
			(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strWeights, nImgsz);

		CStringW cmd(strCmd);
		WCHAR* pCmd = cmd.GetBuffer();
		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = { 0 };
		BOOL bCreated = ::CreateProcessW(NULL, pCmd, NULL, NULL, FALSE,
			CREATE_NO_WINDOW, NULL, (LPCTSTR)strGreen, &si, &pi);
		cmd.ReleaseBuffer();
		if (!bCreated)
			return false;

		::CloseHandle(pi.hThread);
		::WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD dwExit = 1;
		::GetExitCodeProcess(pi.hProcess, &dwExit);
		::CloseHandle(pi.hProcess);
		return dwExit == 0;
	}

	float BoxIoU(const NcnnBox& a, const NcnnBox& b)
	{
		float ax2 = a.x + a.w, ay2 = a.y + a.h;
		float bx2 = b.x + b.w, by2 = b.y + b.h;
		float ix1 = (a.x > b.x) ? a.x : b.x;
		float iy1 = (a.y > b.y) ? a.y : b.y;
		float ix2 = (ax2 < bx2) ? ax2 : bx2;
		float iy2 = (ay2 < by2) ? ay2 : by2;
		float iw = ix2 - ix1;
		float ih = iy2 - iy1;
		if (iw <= 0.0f || ih <= 0.0f)
			return 0.0f;

		float fInter = iw * ih;
		float fUnion = a.w * a.h + b.w * b.h - fInter;
		return fUnion > 0.0f ? fInter / fUnion : 0.0f;
	}

	void NmsBoxes(std::vector<NcnnBox>& boxes, float fIouThresh)
	{
		std::sort(boxes.begin(), boxes.end(), [](const NcnnBox& a, const NcnnBox& b)
			{
				return a.fScore > b.fScore;
			});

		std::vector<NcnnBox> keep;
		std::vector<bool> bRemoved(boxes.size(), false);
		for (size_t i = 0; i < boxes.size(); i++)
		{
			if (bRemoved[i])
				continue;
			keep.push_back(boxes[i]);
			for (size_t j = i + 1; j < boxes.size(); j++)
			{
				if (bRemoved[j] || boxes[j].nClass != boxes[i].nClass)
					continue;
				if (BoxIoU(boxes[i], boxes[j]) > fIouThresh)
					bRemoved[j] = true;
			}
		}
		boxes.swap(keep);
	}


	void DecodeOutput(const ncnn::Mat& out, const std::vector<std::string>& names,
		float fConf, int nImgsz, float fScale, int nPadX, int nPadY,
		int imgW, int imgH, std::vector<NcnnBox>& boxes)
	{
		ncnn::Mat m = out;
		if (out.dims == 3)
		{
			if (out.c != 1)
				return;
			m = out.channel(0);
		}
		if (m.dims != 2 || m.w < 5 || m.h < 1)
			return;

		const int nW = m.w;
		const int nH = m.h;
		const int nc = (int)names.size();

		bool bAttrInW = false;
		int nAttrs = 0;
		if (nc > 0 && (nW == nc + 4 || nW == nc + 64))
		{
			bAttrInW = true;
			nAttrs = nW;
		}
		else if (nc > 0 && (nH == nc + 4 || nH == nc + 64))
		{
			bAttrInW = false;
			nAttrs = nH;
		}
		else if (nc > 0)
		{
			return;
		}
		else if (nW >= 5 && nW <= 100 && nW <= nH)
		{
			bAttrInW = true;
			nAttrs = nW;
		}
		else if (nH >= 5 && nH <= 100)
		{
			bAttrInW = false;
			nAttrs = nH;
		}
		else
		{
			return;
		}

		const int nBoxes = bAttrInW ? nH : nW;

		const bool bDfl = (nc > 0) ? (nAttrs == nc + 64) : (nAttrs >= 100);
		const int nCls = bDfl ? (nAttrs - 64) : (nAttrs - 4);
		if (nCls < 1)
			return;

		auto Val = [&](int nBox, int nAttr) -> float
		{
			return bAttrInW ? m.row(nBox)[nAttr] : m.row(nAttr)[nBox];
		};

		const int strides[3] = { 8, 16, 32 };
		int nOffset[3] = { 0, 0, 0 };
		for (int s = 1; s < 3; s++)
		{
			int g = nImgsz / strides[s - 1];
			nOffset[s] = nOffset[s - 1] + g * g;
		}

		for (int j = 0; j < nBoxes; j++)
		{
			int nBest = -1;
			float fBest = 0.0f;
			for (int k = 0; k < nCls; k++)
			{
				float s = Val(j, (bDfl ? 64 : 4) + k);
				if (s > fBest)
				{
					fBest = s;
					nBest = k;
				}
			}
			if (bDfl)
				fBest = 1.0f / (1.0f + expf(-fBest));
			if (nBest < 0 || fBest < fConf)
				continue;

			float x1, y1, x2, y2;
			if (bDfl)
			{
				int s = (j >= nOffset[2]) ? 2 : (j >= nOffset[1] ? 1 : 0);
				int nGrid = nImgsz / strides[s];
				int nIdx = j - nOffset[s];
				float cx = ((float)(nIdx % nGrid) + 0.5f) * strides[s];
				float cy = ((float)(nIdx / nGrid) + 0.5f) * strides[s];

				float d[4];
				for (int k = 0; k < 4; k++)
				{
					float fMax = -FLT_MAX;
					for (int l = 0; l < 16; l++)
					{
						float v = Val(j, k * 16 + l);
						if (v > fMax)
							fMax = v;
					}

					float fSum = 0.0f;
					float fAcc = 0.0f;
					for (int l = 0; l < 16; l++)
					{
						float e = expf(Val(j, k * 16 + l) - fMax);
						fSum += e;
						fAcc += l * e;
					}
					d[k] = (fSum > 0.0f ? fAcc / fSum : 0.0f) * strides[s];
				}

				x1 = cx - d[0];
				y1 = cy - d[1];
				x2 = cx + d[2];
				y2 = cy + d[3];
			}
			else
			{
				float cx = Val(j, 0);
				float cy = Val(j, 1);
				float bw = Val(j, 2);
				float bh = Val(j, 3);
				x1 = cx - bw * 0.5f;
				y1 = cy - bh * 0.5f;
				x2 = cx + bw * 0.5f;
				y2 = cy + bh * 0.5f;
			}


			x1 = (x1 - (float)nPadX) / fScale;
			y1 = (y1 - (float)nPadY) / fScale;
			x2 = (x2 - (float)nPadX) / fScale;
			y2 = (y2 - (float)nPadY) / fScale;
			if (x1 < 0.0f) x1 = 0.0f;
			if (y1 < 0.0f) y1 = 0.0f;
			if (x2 > (float)imgW) x2 = (float)imgW;
			if (y2 > (float)imgH) y2 = (float)imgH;
			if (x2 <= x1 || y2 <= y1)
				continue;

			NcnnBox b;
			b.nClass = nBest;
			b.fScore = fBest;
			b.x = x1;
			b.y = y1;
			b.w = x2 - x1;
			b.h = y2 - y1;
			boxes.push_back(b);
		}
	}

	bool DetectImage(const cv::Mat& img, const CString& strParam, const CString& strBin,
		int nImgsz, float fConf, float fIou, const std::vector<std::string>& names,
		std::vector<NcnnBox>& boxes, int& nMs, CString& strErr)
	{
		ULONGLONG t0 = ::GetTickCount64();

		std::string sIn = "in0";
		std::string sOut = "out0";
		std::string sParam;
		if (ReadFileBytes(strParam, sParam))
			ParseBlobNames(sParam, sIn, sOut);

		ncnn::Net net;
		net.opt.use_vulkan_compute = false;
		net.opt.num_threads = 4;
		if (net.load_param((const wchar_t*)(LPCTSTR)strParam) != 0 ||
			net.load_model((const wchar_t*)(LPCTSTR)strBin) != 0)
		{
			strErr = _T("NCNN 模型加载失败：") + strParam;
			return false;
		}


		float fScale = std::min((float)nImgsz / (float)img.cols, (float)nImgsz / (float)img.rows);
		int w = (int)((float)img.cols * fScale + 0.5f);
		int h = (int)((float)img.rows * fScale + 0.5f);
		if (w < 1) w = 1;
		if (h < 1) h = 1;
		if (w > nImgsz) w = nImgsz;
		if (h > nImgsz) h = nImgsz;
		int nPadX = (nImgsz - w) / 2;
		int nPadY = (nImgsz - h) / 2;

		cv::Mat resized;
		cv::resize(img, resized, cv::Size(w, h));
		cv::Mat inPad;
		cv::copyMakeBorder(resized, inPad, nPadY, nImgsz - h - nPadY,
			nPadX, nImgsz - w - nPadX, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

		ncnn::Mat in = ncnn::Mat::from_pixels(inPad.data, ncnn::Mat::PIXEL_BGR2RGB, nImgsz, nImgsz);
		const float norm[3] = { 1 / 255.f, 1 / 255.f, 1 / 255.f };
		in.substract_mean_normalize(0, norm);

		ncnn::Extractor ex = net.create_extractor();
		ex.set_light_mode(true);
		ex.input(sIn.c_str(), in);

		ncnn::Mat out;
		if (ex.extract(sOut.c_str(), out) != 0 || out.empty())
		{
			strErr = _T("NCNN 推理失败：输出为空");
			return false;
		}

		DecodeOutput(out, names, fConf, nImgsz, fScale, nPadX, nPadY,
			img.cols, img.rows, boxes);
		NmsBoxes(boxes, fIou);

		nMs = (int)(::GetTickCount64() - t0);
		return true;
	}

	void DrawBoxes(HDC hdc, const std::vector<NcnnBox>& boxes,
		const std::vector<std::string>& names)
	{
		static const COLORREF kColors[] = {
			RGB(244, 67, 54), RGB(233, 30, 99), RGB(156, 39, 176), RGB(103, 58, 183),
			RGB(63, 81, 181), RGB(33, 150, 243), RGB(0, 188, 212), RGB(0, 150, 136),
			RGB(76, 175, 80), RGB(139, 195, 74), RGB(255, 152, 0), RGB(255, 87, 34)
		};
		const int nColors = _countof(kColors);

		HFONT hFont = ::CreateFont(-::MulDiv(13, ::GetDeviceCaps(hdc, LOGPIXELSY), 96),
			0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH, _T("Microsoft YaHei"));
		HGDIOBJ hOldFont = ::SelectObject(hdc, hFont);
		int nOldBk = ::SetBkMode(hdc, TRANSPARENT);

		for (size_t i = 0; i < boxes.size(); i++)
		{
			const NcnnBox& b = boxes[i];
			COLORREF clr = kColors[b.nClass % nColors];

			HPEN hPen = ::CreatePen(PS_SOLID, 2, clr);
			HGDIOBJ hOldPen = ::SelectObject(hdc, hPen);
			HGDIOBJ hOldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
			::Rectangle(hdc, (int)b.x, (int)b.y, (int)(b.x + b.w), (int)(b.y + b.h));
			::SelectObject(hdc, hOldBrush);
			::SelectObject(hdc, hOldPen);
			::DeleteObject(hPen);

			CString strName;
			if (b.nClass >= 0 && b.nClass < (int)names.size())
				strName = Utf8ToText(names[(size_t)b.nClass]);
			if (strName.IsEmpty())
				strName.Format(_T("类别%d"), b.nClass);

			CString strText;
			strText.Format(_T("%s %.2f"), (LPCTSTR)strName, b.fScore);

			SIZE sz = { 0, 0 };
			::GetTextExtentPoint32(hdc, strText, strText.GetLength(), &sz);

			int tx = (int)b.x;
			int ty = (int)b.y - sz.cy - 4;
			if (ty < 0)
				ty = (int)b.y + 2;
			RECT rc = { tx, ty, tx + sz.cx + 8, ty + sz.cy + 4 };

			HBRUSH hBrush = ::CreateSolidBrush(clr);
			::FillRect(hdc, &rc, hBrush);
			::DeleteObject(hBrush);

			::SetTextColor(hdc, RGB(255, 255, 255));
			::DrawText(hdc, strText, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
		}

		::SetBkMode(hdc, nOldBk);
		::SelectObject(hdc, hOldFont);
		::DeleteObject(hFont);
	}


	bool LoadImageCv(const CString& strPath, cv::Mat& img)
	{
		std::string data;
		if (!ReadFileBytes(strPath, data) || data.empty())
			return false;

		std::vector<uchar> buf(data.begin(), data.end());
		img = cv::imdecode(buf, cv::IMREAD_COLOR);
		return !img.empty();
	}


	bool CaptureWindowCv(HWND hwnd, cv::Mat& img)
	{
		RECT rc = { 0 };
		if (!::GetWindowRect(hwnd, &rc))
			return false;

		int w = rc.right - rc.left;
		int h = rc.bottom - rc.top;
		if (w <= 0 || h <= 0)
			return false;

		BITMAPINFO bi = { 0 };
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -h;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		HDC hdcMem = ::CreateCompatibleDC(NULL);
		if (hdcMem == NULL)
			return false;

		void* pBits = NULL;
		HBITMAP hbm = ::CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
		if (hbm == NULL || pBits == NULL)
		{
			::DeleteDC(hdcMem);
			return false;
		}
		HGDIOBJ hOld = ::SelectObject(hdcMem, hbm);

		bool bOk = ::PrintWindow(hwnd, hdcMem, kPwRenderFullContent) != FALSE;
		if (!bOk)
		{
			HDC hdcWin = ::GetWindowDC(hwnd);
			if (hdcWin != NULL)
			{
				bOk = ::BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY) != FALSE;
				::ReleaseDC(hwnd, hdcWin);
			}
		}

		if (bOk)
		{
			cv::Mat bgra(h, w, CV_8UC4, pBits);
			cv::cvtColor(bgra, img, cv::COLOR_BGRA2BGR);
		}

		::SelectObject(hdcMem, hOld);
		::DeleteObject(hbm);
		::DeleteDC(hdcMem);
		return bOk;
	}

	bool MatToCImage(const cv::Mat& img, CImage& out)
	{
		if (img.empty() || img.type() != CV_8UC3)
			return false;
		if (!out.Create(img.cols, img.rows, 24))
			return false;

		for (int y = 0; y < img.rows; y++)
			memcpy(out.GetPixelAddress(0, y), img.ptr(y), (size_t)img.cols * 3);
		return true;
	}
}




CPageTest::CPageTest(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_PAGE_TEST, pParent)
	, m_ullResultTime(0)
	, m_bRunning(false)
	, m_bInitializing(false)
	, m_hwndTarget(NULL)
{
}

void CPageTest::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PREVIEW_TEST, m_wndPreview);
	DDX_Control(pDX, IDC_FINDER_TEST, m_wndFinder);
}

BEGIN_MESSAGE_MAP(CPageTest, CDialogEx)
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_BTN_TEST_OPEN, &CPageTest::OnBnClickedBtnTestOpen)
	ON_BN_CLICKED(IDC_BTN_TEST_REFRESH, &CPageTest::OnBnClickedBtnTestRefresh)
	ON_BN_CLICKED(IDC_BTN_TEST_DETECT, &CPageTest::OnBnClickedBtnTestDetect)
	ON_BN_CLICKED(IDC_BTN_TEST_IMAGE, &CPageTest::OnBnClickedBtnTestImage)
	ON_CBN_SELCHANGE(IDC_COMBO_TEST_MODEL, &CPageTest::OnCbnSelchangeComboTestModel)
	ON_MESSAGE(WM_FINDER_PICKED, &CPageTest::OnFinderPicked)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

BOOL CPageTest::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	SetDlgItemText(IDC_EDIT_TEST_IMGSZ, _T("640"));
	SetDlgItemText(IDC_EDIT_TEST_CONF, _T("0.25"));
	SetDlgItemText(IDC_EDIT_TEST_IOU, _T("0.45"));
	g_pComboTEST = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	m_bInitializing = true;
	FillModelCombo();
	RestoreModelSelection();
	m_bInitializing = false;


	CString strOutDir = GetProjectOutputDir();
	if (!strOutDir.IsEmpty())
	{
		::SHCreateDirectoryEx(NULL, strOutDir, NULL);
		m_strResultPath = strOutDir + _T("\\result.jpg");
	}
	else
	{
		m_strResultPath.Empty();
	}

	SetStatus(_T("就绪"));
	Layout();
	return TRUE;
}


BOOL CPageTest::PreTranslateMessage(MSG* pMsg)
{

	if (UiForwardEditPaste(pMsg))
		return TRUE;

	if (UiSwallowDialogKey(pMsg))
		return TRUE;
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CPageTest::OnOK()
{
}

void CPageTest::OnCancel()
{
}

void CPageTest::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	Layout();
}


void CPageTest::Layout()
{
	if (!::IsWindow(GetSafeHwnd()))
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	int W = rcClient.Width();
	int H = rcClient.Height();
	if (W < 200 || H < 150)
		return;

	int dpi = UiDpi(m_hWnd);
	auto mv = [&](UINT nID, int x, int y, int w, int h)
	{
		CWnd* pWnd = GetDlgItem(nID);
		if (pWnd != NULL)
		{
			int rw = ::MulDiv(w, dpi, 96); if (rw < 1) rw = 1;
			int rh = ::MulDiv(h, dpi, 96); if (rh < 1) rh = 1;
			pWnd->MoveWindow(::MulDiv(x, dpi, 96), ::MulDiv(y, dpi, 96), rw, rh);
		}
	};

	const int M = 8, ROW1 = 8, ROW2 = 34, CH = 22, BARH = 16;

	int x = M;
	mv(IDC_LBL_TEST_MODEL, x, ROW1 + 6, 36, 14);
	x += 36 + 4;

	int nTailW = 60 + 4 + 56 + 6 + 48 + 6 + 48;
	int nComboW = W - M - x - nTailW;
	if (nComboW > 420) nComboW = 420;
	if (nComboW < 120) nComboW = 120;
	mv(IDC_COMBO_TEST_MODEL, x, ROW1, nComboW, 220);
	x += nComboW + 8;
	mv(IDC_LBL_TEST_IMGSZ, x, ROW1 + 6, 60, 14);
	x += 60 + 4;
	mv(IDC_EDIT_TEST_IMGSZ, x, ROW1, 56, CH);
	x += 56 + 6;
	mv(IDC_BTN_TEST_OPEN, x, ROW1, 48, CH);
	x += 48 + 6;
	mv(IDC_BTN_TEST_REFRESH, x, ROW1, 48, CH);

	x = M;
	mv(IDC_FINDER_TEST, x, ROW2 + 1, 20, 20);
	x += 20 + 8;
	mv(IDC_LBL_TEST_HANDLE, x, ROW2 + 6, 62, 14);
	x += 62 + 4;
	mv(IDC_EDIT_TEST_HANDLE, x, ROW2, 76, CH);
	x += 76 + 10;
	mv(IDC_BTN_TEST_DETECT, x, ROW2, 56, CH);
	x += 56 + 6;
	mv(IDC_BTN_TEST_IMAGE, x, ROW2, 68, CH);
	x += 68 + 10;
	mv(IDC_LBL_TEST_CONF, x, ROW2 + 6, 48, 14);
	x += 48 + 4;
	mv(IDC_EDIT_TEST_CONF, x, ROW2, 42, CH);
	x += 42 + 6;
	mv(IDC_LBL_TEST_IOU, x, ROW2 + 6, 28, 14);
	x += 28 + 4;
	mv(IDC_EDIT_TEST_IOU, x, ROW2, 42, CH);

	mv(IDC_STATIC_TEST_ZOOM, W - M - 80, ROW2 + 6, 56, 14);

	int nPreviewY = ROW2 + CH + 8;
	int nBarY = H - M - BARH;
	int nPreviewH = nBarY - 6 - nPreviewY;
	if (nPreviewH < 60) nPreviewH = 60;
	mv(IDC_PREVIEW_TEST, M, nPreviewY, W - M * 2, nPreviewH);
	mv(IDC_STATIC_TEST_STATUS, M, nBarY, W - M * 2, BARH);
}


void CPageTest::FillModelCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	CString strKeep = GetSelectedModel();
	pCombo->ResetContent();

	const std::vector<YOLOPT>& weights = ProjectWeights();
	for (size_t i = 0; i < weights.size(); i++)
		pCombo->AddString(weights[i].pt);

	int nSel = -1;
	if (!strKeep.IsEmpty())
	{
		for (size_t i = 0; i < weights.size(); i++)
		{
			if (weights[i].ptName.CompareNoCase(strKeep) == 0)
			{
				nSel = (int)i;
				break;
			}
		}
	}
	if (nSel < 0 && pCombo->GetCount() > 0)
		nSel = 0;
	if (nSel >= 0)
		pCombo->SetCurSel(nSel);
}

CString CPageTest::GetSelectedModel()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return _T("");

	int nSel = pCombo->GetCurSel();
	const std::vector<YOLOPT>& weights = ProjectWeights();
	if (nSel < 0 || nSel >= (int)weights.size())
		return _T("");
	return weights[nSel].ptName;
}

void CPageTest::SaveLastModel()
{
	if (m_bInitializing)
		return;
	CString strModel = GetSelectedModel();
	if (strModel.IsEmpty())
		return;
	::SetLastTestModel(strModel);
}

void CPageTest::RestoreModelSelection()
{
	CString strSaved = ::GetLastTestModel();
	if (strSaved.IsEmpty())
		return;

	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	const std::vector<YOLOPT>& weights = ProjectWeights();
	for (size_t i = 0; i < weights.size(); i++)
	{
		if (weights[i].ptName.CompareNoCase(strSaved) == 0)
		{
			pCombo->SetCurSel((int)i);
			break;
		}
	}
}

void CPageTest::OnCbnSelchangeComboTestModel()
{
	SaveLastModel();
}


static bool ParseHandleText(const CString& strText, DWORD& dwValue)
{
	CString s = strText;
	s.Trim();
	if (s.IsEmpty())
		return false;

	int nBase = 10;
	CString upper = s;
	upper.MakeUpper();
	if (upper.Left(2) == _T("0X"))
	{
		nBase = 16;
		s = s.Mid(2);
	}
	else if (upper.FindOneOf(_T("ABCDEF")) >= 0)
	{
		nBase = 16;
	}

	if (s.IsEmpty())
		return false;

	TCHAR* pEnd = NULL;
	unsigned long v = _tcstoul(s, &pEnd, nBase);
	if (pEnd == NULL || *pEnd != _T('\0'))
		return false;

	dwValue = (DWORD)v;
	return dwValue != 0;
}

HWND CPageTest::GetTargetHandle(bool bShowError)
{
	CString strText;
	GetDlgItemText(IDC_EDIT_TEST_HANDLE, strText);

	DWORD dwValue = 0;
	HWND hwnd = NULL;
	if (ParseHandleText(strText, dwValue))
		hwnd = (HWND)(UINT_PTR)dwValue;

	if (hwnd == NULL || !::IsWindow(hwnd))
	{
		if (bShowError)
		{
			AfxMessageBox(_T("窗口句柄无效，请重新填写，或按住准星拖到目标窗口上自动获取。"), MB_ICONWARNING);
		}
		return NULL;
	}
	return hwnd;
}


void CPageTest::OnBnClickedBtnTestDetect()
{
	StartDetect();
}

void CPageTest::OnBnClickedBtnTestImage()
{
	StartImageDetect();
}

void CPageTest::ReadDetectParams(int& nImgsz, double& dConf, double& dIou)
{
	CString strTmp;

	nImgsz = 640;
	GetDlgItemText(IDC_EDIT_TEST_IMGSZ, strTmp);
	int n = _ttoi(strTmp);
	if (n >= 64 && n <= 4096) nImgsz = n;

	dConf = 0.25;
	GetDlgItemText(IDC_EDIT_TEST_CONF, strTmp);
	double d = _tcstod(strTmp, NULL);
	if (d > 0.0 && d <= 1.0) dConf = d;

	dIou = 0.45;
	GetDlgItemText(IDC_EDIT_TEST_IOU, strTmp);
	d = _tcstod(strTmp, NULL);
	if (d > 0.0 && d <= 1.0) dIou = d;
}

bool CPageTest::ResolveSelectedNcnnModel(CString& strParam, CString& strBin)
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return false;

	int nSel = pCombo->GetCurSel();
	std::vector<YOLOPT>& weights = ProjectWeights();
	if (nSel < 0 || nSel >= (int)weights.size())
		return false;

	YOLOPT& weight = weights[nSel];
	if (::PathFileExists(weight.ncnnParam) && ::PathFileExists(weight.ncnnBin))
	{
		strParam = weight.ncnnParam;
		strBin = weight.ncnnBin;
		return true;
	}

	int nSlash = weight.ptName.ReverseFind(_T('\\'));
	if (nSlash <= 0)
		return false;
	CString strDir = weight.ptName.Left(nSlash);
	CString strStem = ::PathFindFileName(weight.ptName);
	int nDot = strStem.ReverseFind(_T('.'));
	if (nDot > 0)
		strStem = strStem.Left(nDot);
	CString strNcnnDir = strDir + _T("\\") + strStem + _T("_ncnn_model");

	// 已导出的模型直接写回共享容器，不再重复转换
	if (FindNcnnPair(strNcnnDir, strParam, strBin))
	{
		weight.ncnnParam = strParam;
		weight.ncnnBin = strBin;
		return true;
	}

	if (!weight.bCanConvertNcnn)
		return false;

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty() ||
		!::PathFileExists(strGreen + _T("\\scripts\\export_model.py")))
		return false;

	int nImgsz = 640;
	double dConf = 0.25, dIou = 0.45;
	ReadDetectParams(nImgsz, dConf, dIou);
	SetRunning(true);
	SetStatus(_T("正在将 PT 转换为 NCNN..."));
	AfxGetApp()->DoWaitCursor(1);

	bool bOk = RunNcnnExport(strGreen, strPython, weight.ptName, nImgsz);
	if (bOk)
		bOk = FindNcnnPair(strNcnnDir, strParam, strBin);

	AfxGetApp()->DoWaitCursor(0);
	SetRunning(false);
	SetStatus(_T("就绪"));

	if (!bOk)
	{
		weight.ncnnParam.Empty();
		weight.ncnnBin.Empty();
		weight.bCanConvertNcnn = false;
		return false;
	}

	weight.ncnnParam = strParam;
	weight.ncnnBin = strBin;
	return true;
}


void CPageTest::StartDetect()
{
	if (GetSelectedModel().IsEmpty())
	{
		AfxMessageBox(_T("请先选择 PT 权重（可点「刷新」同步权重列表）。"), MB_ICONWARNING);
		return;
	}

	CString strParam, strBin;
	if (!ResolveSelectedNcnnModel(strParam, strBin))
	{
		AfxMessageBox(_T("PT 转 NCNN 失败或该权重不支持转换。\r\n")
			_T("请检查 YOLO_Green 绿色包及模型文件是否完整。"), MB_ICONWARNING);
		return;
	}

	SaveLastModel();

	HWND hwnd = GetTargetHandle(true);
	if (hwnd == NULL)
		return;

	if (m_strResultPath.IsEmpty())
	{
		AfxMessageBox(_T("未找到输出目录，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	cv::Mat img;
	if (!CaptureWindowCv(hwnd, img))
	{
		AfxMessageBox(_T("窗口截图失败，请确认目标窗口未最小化。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	RunNcnnOnBitmap(strParam, strBin, img.data, img.cols, img.rows);
}

void CPageTest::StartImageDetect()
{
	if (GetSelectedModel().IsEmpty())
	{
		AfxMessageBox(_T("请先选择 PT 权重（可点「刷新」同步权重列表）。"), MB_ICONWARNING);
		return;
	}

	CString strParam, strBin;
	if (!ResolveSelectedNcnnModel(strParam, strBin))
	{
		AfxMessageBox(_T("PT 转 NCNN 失败或该权重不支持转换。\r\n")
			_T("请检查 YOLO_Green 绿色包及模型文件是否完整。"), MB_ICONWARNING);
		return;
	}

	SaveLastModel();

	if (m_strResultPath.IsEmpty())
	{
		AfxMessageBox(_T("未找到输出目录，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	CFileDialog dlg(TRUE, _T("jpg"), NULL,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("图片文件 (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp|所有文件 (*.*)|*.*||"), this);

	CString strInitDir = GetProjectImagesDir();
	if (!::PathIsDirectory(strInitDir))
		strInitDir = GetProjectRoot();
	if (::PathIsDirectory(strInitDir))
		dlg.m_ofn.lpstrInitialDir = (LPCTSTR)strInitDir;

	if (dlg.DoModal() != IDOK)
		return;

	cv::Mat img;
	if (!LoadImageCv(dlg.GetPathName(), img))
	{
		AfxMessageBox(_T("图片加载失败，请确认文件格式（jpg/png/bmp）。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	RunNcnnOnBitmap(strParam, strBin, img.data, img.cols, img.rows);
}

void CPageTest::RunNcnnOnBitmap(const CString& strParam, const CString& strBin,
	void* pBgr, int w, int h)
{
	int nImgsz = 640;
	double dConf = 0.25, dIou = 0.45;
	ReadDetectParams(nImgsz, dConf, dIou);

	std::vector<std::string> names = LoadClassNames(strParam);
	std::vector<NcnnBox> boxes;
	CString strErr;
	int nMs = 0;

	SetRunning(true);
	SetStatus(_T("NCNN 推理中..."));
	AfxGetApp()->DoWaitCursor(1);

	cv::Mat img(h, w, CV_8UC3, pBgr);
	bool bOk = DetectImage(img, strParam, strBin, nImgsz,
		(float)dConf, (float)dIou, names, boxes, nMs, strErr);

	CImage out;
	if (bOk)
	{
		if (MatToCImage(img, out))
		{
			HDC hdc = out.GetDC();
			DrawBoxes(hdc, boxes, names);
			out.ReleaseDC();

			if (FAILED(out.Save(m_strResultPath)))
			{
				bOk = false;
				strErr = _T("结果图写入失败：") + m_strResultPath;
			}
		}
		else
		{
			bOk = false;
			strErr = _T("结果图创建失败。");
		}
	}

	AfxGetApp()->DoWaitCursor(0);
	SetRunning(false);

	if (!bOk)
	{
		AfxMessageBox(strErr, MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	m_ullResultTime = 0;
	LoadResultImage();

	CString strStatus;
	if (boxes.empty())
		strStatus.Format(_T("检测完成：未检测到目标，用时 %d ms"), nMs);
	else
		strStatus.Format(_T("检测完成：%d 个目标，用时 %d ms"), (int)boxes.size(), nMs);
	SetStatus(strStatus);
}


void CPageTest::SetRunning(bool bRunning)
{
	m_bRunning = bRunning;

	CWnd* pBtn = GetDlgItem(IDC_BTN_TEST_DETECT);
	if (pBtn != NULL)
		pBtn->EnableWindow(bRunning ? FALSE : TRUE);

	BOOL bEnable = bRunning ? FALSE : TRUE;
	UINT ids[] = {
		IDC_COMBO_TEST_MODEL, IDC_BTN_TEST_OPEN, IDC_BTN_TEST_REFRESH, IDC_BTN_TEST_IMAGE,
		IDC_EDIT_TEST_IMGSZ, IDC_EDIT_TEST_HANDLE, IDC_FINDER_TEST,
		IDC_EDIT_TEST_CONF, IDC_EDIT_TEST_IOU
	};
	for (int i = 0; i < _countof(ids); i++)
	{
		CWnd* pWnd = GetDlgItem(ids[i]);
		if (pWnd != NULL)
			pWnd->EnableWindow(bEnable);
	}
}

void CPageTest::SetStatus(LPCTSTR lpszText)
{
	SetDlgItemText(IDC_STATIC_TEST_STATUS, lpszText);
}

void CPageTest::LoadResultImage()
{
	if (m_strResultPath.IsEmpty())
		return;

	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!::GetFileAttributesEx(m_strResultPath, GetFileExInfoStandard, &fad))
		return;
	if (fad.nFileSizeLow == 0 && fad.nFileSizeHigh == 0)
		return;

	ULONGLONG t = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) |
		fad.ftLastWriteTime.dwLowDateTime;
	if (t == m_ullResultTime)
		return;

	CImage image;
	if (FAILED(image.Load(m_strResultPath)))
		return;

	m_ullResultTime = t;
	m_wndPreview.SetImage(&image);
}



void CPageTest::OnBnClickedBtnTestOpen()
{
	CFileDialog dlg(TRUE, _T("pt"), NULL,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("PT 权重文件 (*.pt)|*.pt||"), this);

	CString strInitDir = GetProjectModelsDir();
	if (!::PathIsDirectory(strInitDir))
	{
		CString strGreen = GetGreenRoot();
		if (!strGreen.IsEmpty())
			strInitDir = strGreen + _T("\\models");
	}
	if (::PathIsDirectory(strInitDir))
		dlg.m_ofn.lpstrInitialDir = (LPCTSTR)strInitDir;

	if (dlg.DoModal() != IDOK)
		return;

	CString strPath = dlg.GetPathName();
	std::vector<YOLOPT>& weights = ProjectWeights();
	int nSel = -1;
	for (size_t i = 0; i < weights.size(); i++)
	{
		if (weights[i].ptName.CompareNoCase(strPath) == 0)
		{
			nSel = (int)i;
			break;
		}
	}

	if (nSel < 0)
	{
		YOLOPT weight;
		weight.pt = dlg.GetFileName();
		weight.ptName = strPath;
		weight.ptdex = (long)weights.size();
		weights.push_back(weight);
		nSel = (int)weights.size() - 1;
	}

	RefreshWeightCombos();
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo != NULL)
		pCombo->SetCurSel(nSel);
	SaveLastModel();
	SetStatus(_T("已加载模型: ") + dlg.GetFileName());
}

void CPageTest::OnBnClickedBtnTestRefresh()
{
	FillModelCombo();
	SetStatus(_T("模型列表已刷新"));
}

LRESULT CPageTest::OnFinderPicked(WPARAM wParam, LPARAM /*lParam*/)
{
	HWND hwnd = (HWND)(UINT_PTR)wParam;
	if (hwnd != NULL && ::IsWindow(hwnd))
	{
		CString strHwnd;
		strHwnd.Format(_T("0x%08X"), (DWORD)(DWORD_PTR)hwnd);
		SetDlgItemText(IDC_EDIT_TEST_HANDLE, strHwnd);
		m_hwndTarget = hwnd;

		TCHAR szTitle[256] = { 0 };
		::GetWindowText(hwnd, szTitle, _countof(szTitle));
		CString strMsg;
		strMsg.Format(_T("已选中窗口: %s"), szTitle);
		SetStatus(strMsg);
	}
	return 0;
}

BOOL CPageTest::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	return TRUE;
}
