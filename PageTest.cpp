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

//namespace
//{
//	// 测试用 NCNN 模型（param 与 bin 同目录同名）
//	const char* k_ncnnBinPath =
//		"E:\\dire\\c++\\YOLO\\maoxian2\\runs\\detect\\train\\weights\\best_ncnn_model\\model.ncnn.bin";
//	const char* k_ncnnParamPath = //model.ncnn.param
//		"E:\\dire\\c++\\YOLO\\maoxian2\\runs\\detect\\train\\weights\\best_ncnn_model\\model.ncnn.param";
//
//	struct NcnnDetection
//	{
//		int   nClass;
//		float fConf;
//		float x, y, w, h;
//	};
//
//	float NcnnBoxIoU(const NcnnDetection& a, const NcnnDetection& b)
//	{
//		float ax2 = a.x + a.w, ay2 = a.y + a.h;
//		float bx2 = b.x + b.w, by2 = b.y + b.h;
//		float ix1 = a.x > b.x ? a.x : b.x;
//		float iy1 = a.y > b.y ? a.y : b.y;
//		float ix2 = ax2 < bx2 ? ax2 : bx2;
//		float iy2 = ay2 < by2 ? ay2 : by2;
//		float iw = ix2 - ix1, ih = iy2 - iy1;
//		if (iw <= 0.0f || ih <= 0.0f)
//			return 0.0f;
//		float fInter = iw * ih;
//		float fUnion = a.w * a.h + b.w * b.h - fInter;
//		return fUnion > 0.0f ? fInter / fUnion : 0.0f;
//	}
//
//	void NcnnNms(std::vector<NcnnDetection>& vec, float fIoUThresh)
//	{
//		if (vec.empty())
//			return;
//
//		std::sort(vec.begin(), vec.end(), [](const NcnnDetection& a, const NcnnDetection& b)
//		{
//			return a.fConf > b.fConf;
//		});
//
//		std::vector<NcnnDetection> keep;
//		std::vector<bool> bRemoved(vec.size(), false);
//		for (size_t i = 0; i < vec.size(); i++)
//		{
//			if (bRemoved[i])
//				continue;
//			keep.push_back(vec[i]);
//			for (size_t j = i + 1; j < vec.size(); j++)
//			{
//				if (bRemoved[j])
//					continue;
//				if (vec[j].nClass != vec[i].nClass)
//					continue;
//				if (NcnnBoxIoU(vec[i], vec[j]) > fIoUThresh)
//					bRemoved[j] = true;
//			}
//		}
//		vec.swap(keep);
//	}
//
//	CString WstrFromUtf8(const std::string& s)
//	{
//		if (s.empty())
//			return CString();
//		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
//		if (n <= 0)
//			return CString();
//		std::wstring w((size_t)n, L'\0');
//		::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
//		return CString(w.c_str());
//	}
//
//	bool ReadTextFileNarrow(const std::string& strPath, std::string& text)
//	{
//		std::ifstream f(strPath.c_str(), std::ios::binary);
//		if (!f.is_open())
//			return false;
//		text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
//		return true;
//	}
//
//	// 解析 param 文本，取输入 blob 和最后一层的输出 blob
//	void ParseNcnnBlobNames(const std::string& paramText, std::string& strIn, std::string& strOut)
//	{
//		std::istringstream iss(paramText);
//		std::string line;
//		while (std::getline(iss, line))
//		{
//			if (!line.empty() && line.back() == '\r')
//				line.pop_back();
//			if (line.empty())
//				continue;
//
//			std::istringstream ls(line);
//			std::vector<std::string> tk;
//			std::string t;
//			while (ls >> t)
//				tk.push_back(t);
//			if (tk.size() < 5)
//				continue;
//
//			// 层格式: 类型 名称 输入数 输出数 输入blob... 输出blob... 参数...
//			int nBottom = atoi(tk[2].c_str());
//			int nTop = atoi(tk[3].c_str());
//			if (nTop < 1)
//				continue;
//			int nTopIdx = 4 + nBottom + nTop - 1;
//			if (nBottom < 0 || nTopIdx >= (int)tk.size())
//				continue;
//			std::string strTopBlob = tk[nTopIdx];
//
//			if (tk[0] == "Input")
//				strIn = strTopBlob;
//			strOut = strTopBlob;
//		}
//	}
//
//	// 解析 metadata.yaml 的 names 段（0: 名称）
//	void ParseNcnnNames(const std::string& metaText, std::vector<std::string>& names)
//	{
//		std::istringstream iss(metaText);
//		std::string line;
//		bool bInNames = false;
//		while (std::getline(iss, line))
//		{
//			if (!line.empty() && line.back() == '\r')
//				line.pop_back();
//			if (!bInNames)
//			{
//				if (line.compare(0, 6, "names:") == 0)
//					bInNames = true;
//				continue;
//			}
//			if (line.empty())
//				continue;
//			if (line[0] != ' ' && line[0] != '\t')
//				break;
//
//			size_t nColon = line.find(':');
//			if (nColon == std::string::npos)
//				continue;
//			int nIdx = atoi(line.substr(0, nColon).c_str());
//			std::string strName = line.substr(nColon + 1);
//			while (!strName.empty() && (strName.front() == ' ' || strName.front() == '\t'))
//				strName.erase(strName.begin());
//			while (!strName.empty() && (strName.back() == ' ' || strName.back() == '\t'))
//				strName.pop_back();
//			if (nIdx < 0)
//				continue;
//			if ((int)names.size() <= nIdx)
//				names.resize((size_t)nIdx + 1);
//			names[(size_t)nIdx] = strName;
//		}
//	}
//
//	// BGRA(top-down) 等比缩放后居中填充到 nSize×nSize，输出 RGB 连续缓冲
//	void BuildLetterboxRgb(const BYTE* pBgra, int W, int H, int nStride, int nSize,
//		std::vector<unsigned char>& rgb, float& fScale, int& nPadX, int& nPadY)
//	{
//		rgb.assign((size_t)nSize * nSize * 3, 114);
//
//		fScale = (std::min)((float)nSize / (float)W, (float)nSize / (float)H);
//		int nNewW = (int)((float)W * fScale + 0.5f);
//		int nNewH = (int)((float)H * fScale + 0.5f);
//		if (nNewW < 1) nNewW = 1;
//		if (nNewH < 1) nNewH = 1;
//		if (nNewW > nSize) nNewW = nSize;
//		if (nNewH > nSize) nNewH = nSize;
//		nPadX = (nSize - nNewW) / 2;
//		nPadY = (nSize - nNewH) / 2;
//
//		int nSrcOff[3] = { 2, 1, 0 }; // BGR → RGB
//
//		for (int dy = 0; dy < nNewH; dy++)
//		{
//			float fy = ((float)dy + 0.5f) / fScale - 0.5f;
//			if (fy < 0.0f) fy = 0.0f;
//			int y0 = (int)fy;
//			if (y0 > H - 1) y0 = H - 1;
//			int y1 = (y0 + 1 < H) ? (y0 + 1) : (H - 1);
//			float wy = fy - (float)y0;
//
//			const BYTE* pRow0 = pBgra + (size_t)y0 * nStride;
//			const BYTE* pRow1 = pBgra + (size_t)y1 * nStride;
//			unsigned char* pDst = &rgb[((size_t)(nPadY + dy) * nSize + (size_t)nPadX) * 3];
//
//			for (int dx = 0; dx < nNewW; dx++)
//			{
//				float fx = ((float)dx + 0.5f) / fScale - 0.5f;
//				if (fx < 0.0f) fx = 0.0f;
//				int x0 = (int)fx;
//				if (x0 > W - 1) x0 = W - 1;
//				int x1 = (x0 + 1 < W) ? (x0 + 1) : (W - 1);
//				float wx = fx - (float)x0;
//
//				const BYTE* p00 = pRow0 + (size_t)x0 * 4;
//				const BYTE* p01 = pRow0 + (size_t)x1 * 4;
//				const BYTE* p10 = pRow1 + (size_t)x0 * 4;
//				const BYTE* p11 = pRow1 + (size_t)x1 * 4;
//
//				for (int k = 0; k < 3; k++)
//				{
//					int off = nSrcOff[k];
//					float v = (1.0f - wy) * ((1.0f - wx) * p00[off] + wx * p01[off])
//						+ wy * ((1.0f - wx) * p10[off] + wx * p11[off]);
//					int iv = (int)(v + 0.5f);
//					if (iv < 0) iv = 0;
//					if (iv > 255) iv = 255;
//					pDst[dx * 3 + k] = (unsigned char)iv;
//				}
//			}
//		}
//	}
//}


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
	ON_BN_CLICKED(IDC_BTN_TEST_NCNN, &CPageTest::OnBnClickedBtnTestNcnn)
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
	x += 42 + 8;
	mv(IDC_BTN_TEST_NCNN, x, ROW2, 68, CH);

	mv(IDC_STATIC_TEST_ZOOM, W - M - 80, ROW2 + 6, 56, 14);

	int nPreviewY = ROW2 + CH + 8;
	int nBarY = H - M - BARH;
	int nPreviewH = nBarY - 6 - nPreviewY;
	if (nPreviewH < 60) nPreviewH = 60;
	mv(IDC_PREVIEW_TEST, M, nPreviewY, W - M * 2, nPreviewH);
	mv(IDC_STATIC_TEST_STATUS, M, nBarY, W - M * 2, BARH);
}


static CString MakeModelDisplayName(const CString& strFull)
{
	CStringArray arrPrefixes;
	CString strProj = GetProjectRoot();
	if (!strProj.IsEmpty())
		arrPrefixes.Add(strProj + _T("\\"));
	CString strGreen = GetGreenRoot();
	if (!strGreen.IsEmpty())
		arrPrefixes.Add(strGreen + _T("\\"));

	for (INT_PTR i = 0; i < arrPrefixes.GetSize(); i++)
	{
		CString strPrefix = arrPrefixes[i];
		if (strFull.GetLength() > strPrefix.GetLength() &&
			strFull.Left(strPrefix.GetLength()).CompareNoCase(strPrefix) == 0)
			return strFull.Mid(strPrefix.GetLength());
	}
	return strFull;
}

void CPageTest::FillModelCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	CString strKeep = GetSelectedModel();

	pCombo->ResetContent();
	m_arrModels.RemoveAll();

	CStringArray arrFiles;
	CString strBest = ::GetBestWeightPath();
	if (!strBest.IsEmpty() && ::PathFileExists(strBest))
		arrFiles.Add(strBest);

	CStringArray arrDirs;
	{
		CString strProj = GetProjectModelsDir();
		if (!strProj.IsEmpty())
			arrDirs.Add(strProj);
		CString strGreen = GetGreenRoot();
		if (!strGreen.IsEmpty())
			arrDirs.Add(strGreen + _T("\\models"));
	}

	for (INT_PTR d = 0; d < arrDirs.GetSize(); d++)
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(arrDirs[d] + _T("\\*.pt"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory())
				arrFiles.Add(finder.GetFileURL());
		}
		finder.Close();
	}

	for (INT_PTR i = 0; i < arrFiles.GetSize(); i++)
	{
		pCombo->AddString(MakeModelDisplayName(arrFiles[i]));
		m_arrModels.Add(arrFiles[i]);
	}

	int nSel = -1;
	if (!strKeep.IsEmpty())
	{
		for (INT_PTR i = 0; i < m_arrModels.GetSize(); i++)
		{
			if (m_arrModels[i].CompareNoCase(strKeep) == 0)
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
	if (nSel < 0 || nSel >= m_arrModels.GetSize())
		return _T("");
	return m_arrModels[nSel];
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

	int nSel = -1;
	for (INT_PTR i = 0; i < m_arrModels.GetSize(); i++)
	{
		if (m_arrModels[i].CompareNoCase(strSaved) == 0)
		{
			nSel = (int)i;
			break;
		}
	}

	if (nSel < 0 && ::PathFileExists(strSaved))
	{
		pCombo->AddString(MakeModelDisplayName(strSaved));
		m_arrModels.Add(strSaved);
		nSel = pCombo->GetCount() - 1;
	}

	if (nSel >= 0)
		pCombo->SetCurSel(nSel);
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

void CPageTest::StartDetect()
{
	CString strModel = GetSelectedModel();
	if (strModel.IsEmpty())
	{
		AfxMessageBox(_T("请先选择模型（可点「刷新」重新扫描 models 目录）。"), MB_ICONWARNING);
		return;
	}
	SaveLastModel();

	HWND hwnd = GetTargetHandle(true);
	if (hwnd == NULL)
		return;

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	CString strScript = strGreen + _T("\\scripts\\infer_ui.py");
	if (strGreen.IsEmpty() || !::PathFileExists(strPython) || !::PathFileExists(strScript))
	{
		AfxMessageBox(_T("未找到 scripts\\infer_ui.py，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	if (m_strResultPath.IsEmpty())
	{
		AfxMessageBox(_T("未找到输出目录，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

	int nImgsz = 640;
	double dConf = 0.25, dIou = 0.45;
	ReadDetectParams(nImgsz, dConf, dIou);

	CString strCmd;
	strCmd.Format(_T("\"%s\" \"%s\" --weights \"%s\" --hwnd 0x%I64X --imgsz %d --conf %.2f --iou %.2f --mode single --out \"%s\""),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strModel,
		(unsigned __int64)(UINT_PTR)hwnd, nImgsz, dConf, dIou, (LPCTSTR)m_strResultPath);

	ExecuteInference(strCmd, strGreen);
}

void CPageTest::StartImageDetect()
{
	CString strModel = GetSelectedModel();
	if (strModel.IsEmpty())
	{
		AfxMessageBox(_T("请先选择模型（可点「刷新」重新扫描 models 目录）。"), MB_ICONWARNING);
		return;
	}
	SaveLastModel();

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	CString strScript = strGreen + _T("\\scripts\\infer_ui.py");
	if (strGreen.IsEmpty() || !::PathFileExists(strPython) || !::PathFileExists(strScript))
	{
		AfxMessageBox(_T("未找到 scripts\\infer_ui.py，请检查绿色包是否完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
		return;
	}

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

	CString strImage = dlg.GetPathName();

	int nImgsz = 640;
	double dConf = 0.25, dIou = 0.45;
	ReadDetectParams(nImgsz, dConf, dIou);

	CString strCmd;
	strCmd.Format(_T("\"%s\" \"%s\" --weights \"%s\" --image \"%s\" --imgsz %d --conf %.2f --iou %.2f --out \"%s\""),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strModel,
		(LPCTSTR)strImage, nImgsz, dConf, dIou, (LPCTSTR)m_strResultPath);

	ExecuteInference(strCmd, strGreen);
}

void CPageTest::OnBnClickedBtnTestNcnn()
{
	/*if (::GetFileAttributesA(k_ncnnBinPath) == INVALID_FILE_ATTRIBUTES|| ::GetFileAttributesA(k_ncnnParamPath) == INVALID_FILE_ATTRIBUTES)
	{
		AfxMessageBox(_T("未找到 NCNN 测试模型:\n") + CString(k_ncnnBinPath), MB_ICONWARNING);
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

	SetRunning(true);
	SetStatus(_T("NCNN 推理中..."));
	AfxGetApp()->DoWaitCursor(1);

	CString strResult = RunNcnnDetect(dlg.GetPathName());

	AfxGetApp()->DoWaitCursor(0);
	SetRunning(false);

	if (strResult.IsEmpty())
	{
		SetStatus(_T("NCNN 推理完成：未检测到目标"));
		AfxMessageBox(_T("未检测到目标。"), MB_ICONINFORMATION);
	}
	else
	{
		SetStatus(_T("NCNN 推理完成"));
		AfxMessageBox(strResult, MB_ICONINFORMATION);
	}*/
}

// 使用固定的 NCNN 模型对单张图片推理，返回 "类名,置信度,x,y,w,h|..." 格式的坐标框
CString CPageTest::RunNcnnDetect(const CString& strImagePath)
{
	//std::string sBin(k_ncnnBinPath);
	//if (sBin.size() < 5)
	//	return _T("[NCNN] 模型路径无效");
	//std::string sParam = sBin.substr(0, sBin.size() - 4) + ".param";
	//std::string sDir = sBin.substr(0, sBin.find_last_of("\\/"));
	//int nImgsz = 640;
	//double dConf = 0.25, dIou = 0.45;
	//ReadDetectParams(nImgsz, dConf, dIou);
	//// 类别名：优先从模型目录的 metadata.yaml 读取
	//std::vector<std::string> names;
	//{
	//	std::string sText;
	//	if (ReadTextFileNarrow(sDir + "\\metadata.yaml", sText))
	//		ParseNcnnNames(sText, names);
	//}
	//// blob 名：从 param 解析，退回 in0/out0
	//std::string sIn = "in0", sOut = "out0";
	//{
	//	std::string sText;
	//	if (ReadTextFileNarrow(sParam, sText))
	//		ParseNcnnBlobNames(sText, sIn, sOut);
	//}
	//CImage img;
	//if (FAILED(img.Load(strImagePath)))
	//	return _T("[NCNN] 图片加载失败");
	//int W = img.GetWidth();
	//int H = img.GetHeight();
	//if (W <= 0 || H <= 0)
	//	return _T("[NCNN] 图片尺寸无效");
	//// 统一转成 32 位 top-down DIB，便于按 BGRA 顺序取样
	//BITMAPINFO bi = { 0 };
	//bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	//bi.bmiHeader.biWidth = W;
	//bi.bmiHeader.biHeight = -H;
	//bi.bmiHeader.biPlanes = 1;
	//bi.bmiHeader.biBitCount = 32;
	//bi.bmiHeader.biCompression = BI_RGB;
	//void* pBits = NULL;
	//HDC hdcMem = ::CreateCompatibleDC(NULL);
	//HBITMAP hbm = ::CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
	//if (hbm == NULL || pBits == NULL)
	//{
	//	::DeleteDC(hdcMem);
	//	return _T("[NCNN] 图像缓冲创建失败");
	//}
	//HGDIOBJ hOld = ::SelectObject(hdcMem, hbm);
	//img.Draw(hdcMem, 0, 0, W, H, 0, 0, W, H);
	//std::vector<unsigned char> rgb;
	//float fScale = 1.0f;
	//int nPadX = 0, nPadY = 0;
	//BuildLetterboxRgb((const BYTE*)pBits, W, H, W * 4, nImgsz, rgb, fScale, nPadX, nPadY);
	//::SelectObject(hdcMem, hOld);
	//::DeleteObject(hbm);
	//::DeleteDC(hdcMem);
	//ncnn::Net net;
	//net.opt.use_vulkan_compute = false;
	//net.opt.num_threads = 4;
	//if (net.load_param(k_ncnnParamPath) != 0 || net.load_model(k_ncnnBinPath) != 0)
	//	return _T("[NCNN] 模型加载失败");
	//ncnn::Mat in = ncnn::Mat::from_pixels(rgb.data(), ncnn::Mat::PIXEL_RGB, nImgsz, nImgsz);
	//const float normVals[3] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
	//in.substract_mean_normalize(0, normVals);
	//ncnn::Extractor ex = net.create_extractor();
	//ex.set_light_mode(true);
	//ex.input(sIn.c_str(), in);
	//ncnn::Mat out;
	//if (ex.extract(sOut.c_str(), out) != 0 || out.empty())
	//	return _T("[NCNN] 推理输出为空");
	//// 输出布局自适应：属性数(4+类别数) 可能落在 w / h / c 任一方向
	//int nAttrs = 0, nBoxes = 0;
	//int nAttrAxis = -1; // 0=w 1=h 2=c
	//int nTotal = out.w * out.h * (out.dims == 3 ? out.c : 1);
	//if (out.h > 1 && out.h <= 100 && out.h <= out.w)
	//{
	//	nAttrAxis = 1;
	//	nAttrs = out.h;
	//	nBoxes = out.w;
	//}
	//else if (out.w > 1 && out.w <= 100)
	//{
	//	nAttrAxis = 0;
	//	nAttrs = out.w;
	//	nBoxes = out.h;
	//}
	//else if (out.dims == 3 && out.c > 1 && out.c <= 100)
	//{
	//	nAttrAxis = 2;
	//	nAttrs = out.c;
	//	nBoxes = out.w * out.h;
	//}
	//else
	//{
	//	CString strErr;
	//	strErr.Format(_T("[NCNN] 输出维度无法识别 (dims=%d w=%d h=%d c=%d)"),
	//		out.dims, out.w, out.h, out.c);
	//	return strErr;
	//}
	//if (nAttrs < 5 || nBoxes < 1 || nAttrs * nBoxes != nTotal)
	//{
	//	CString strErr;
	//	strErr.Format(_T("[NCNN] 输出维度无法识别 (dims=%d w=%d h=%d c=%d)"),
	//		out.dims, out.w, out.h, out.c);
	//	return strErr;
	//}
	//ncnn::Mat mat2D = out;
	//if (out.dims == 3 && out.c == 1)
	//	mat2D = out.channel(0);
	//auto GetVal = [&](int nBox, int nAttr) -> float
	//{
	//	if (nAttrAxis == 1)
	//		return mat2D.row(nAttr)[nBox];
	//	if (nAttrAxis == 0)
	//		return mat2D.row(nBox)[nAttr];
	//	// 属性在 channel 方向
	//	const ncnn::Mat& mc = out.channel(nAttr);
	//	return (mc.h == 1) ? mc.row(0)[nBox] : mc.row(nBox)[0];
	//};
	//const int nCls = nAttrs - 4;
	//std::vector<NcnnDetection> dets;
	//for (int j = 0; j < nBoxes; j++)
	//{
	//	float cx = GetVal(j, 0);
	//	float cy = GetVal(j, 1);
	//	float bw = GetVal(j, 2);
	//	float bh = GetVal(j, 3);
	//	int nBest = -1;
	//	float fBest = 0.0f;
	//	for (int k = 0; k < nCls; k++)
	//	{
	//		float s = GetVal(j, 4 + k);
	//		if (s > fBest)
	//		{
	//			fBest = s;
	//			nBest = k;
	//		}
	//	}
	//	if (nBest < 0 || fBest < (float)dConf)
	//		continue;
	//	float x1 = (cx - bw * 0.5f - (float)nPadX) / fScale;
	//	float y1 = (cy - bh * 0.5f - (float)nPadY) / fScale;
	//	float x2 = (cx + bw * 0.5f - (float)nPadX) / fScale;
	//	float y2 = (cy + bh * 0.5f - (float)nPadY) / fScale;
	//	if (x1 < 0.0f) x1 = 0.0f;
	//	if (y1 < 0.0f) y1 = 0.0f;
	//	if (x2 > (float)W) x2 = (float)W;
	//	if (y2 > (float)H) y2 = (float)H;
	//	if (x2 <= x1 || y2 <= y1)
	//		continue;
	//	NcnnDetection d;
	//	d.nClass = nBest;
	//	d.fConf = fBest;
	//	d.x = x1;
	//	d.y = y1;
	//	d.w = x2 - x1;
	//	d.h = y2 - y1;
	//	dets.push_back(d);
	//}
	//NcnnNms(dets, (float)dIou);
	//CString strResult;
	//for (size_t i = 0; i < dets.size(); i++)
	//{
	//	CString strName;
	//	if (dets[i].nClass < (int)names.size() && !names[(size_t)dets[i].nClass].empty())
	//		strName = WstrFromUtf8(names[(size_t)dets[i].nClass]);
	//	else
	//		strName.Format(_T("类别%d"), dets[i].nClass);
	//	CString strItem;
	//	strItem.Format(_T("%s,%.2f,%d,%d,%d,%d"),
	//		(LPCTSTR)strName, dets[i].fConf,
	//		(int)dets[i].x, (int)dets[i].y, (int)dets[i].w, (int)dets[i].h);
	//	if (!strResult.IsEmpty())
	//		strResult += _T("|");
	//	strResult += strItem;
	//}
	//return strResult;
return L"";
}

void CPageTest::ExecuteInference(const CString& strCmd, const CString& strGreen)
{
	m_ullResultTime = 0;
	::DeleteFile(m_strResultPath);
	m_wndPreview.SetImage(NULL);

	SetRunning(true);
	SetStatus(_T("检测中..."));
	AfxGetApp()->DoWaitCursor(1);

	STARTUPINFO si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = { 0 };
	DWORD dwExit = (DWORD)-1;

	if (::CreateProcess(NULL, (LPTSTR)(LPCTSTR)strCmd, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, strGreen, &si, &pi))
	{
		::WaitForSingleObject(pi.hProcess, INFINITE);
		::GetExitCodeProcess(pi.hProcess, &dwExit);
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
	}

	AfxGetApp()->DoWaitCursor(0);
	SetRunning(false);

	if (dwExit == 0 && ::PathFileExists(m_strResultPath))
	{
		SetStatus(_T("检测完成"));
		LoadResultImage();
	}
	else
	{
		AfxMessageBox(_T("检测失败，请确认模型与绿色包环境完整。"), MB_ICONWARNING);
		SetStatus(_T("就绪"));
	}
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
		IDC_EDIT_TEST_CONF, IDC_EDIT_TEST_IOU, IDC_BTN_TEST_NCNN
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
		_T("模型文件 (*.pt)|*.pt|所有文件 (*.*)|*.*||"), this);

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
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_TEST_MODEL);
	if (pCombo == NULL)
		return;

	int nSel = -1;
	for (INT_PTR i = 0; i < m_arrModels.GetSize(); i++)
	{
		if (m_arrModels[i].CompareNoCase(strPath) == 0)
		{
			nSel = (int)i;
			break;
		}
	}
	if (nSel < 0)
	{
		pCombo->AddString(MakeModelDisplayName(strPath));
		m_arrModels.Add(strPath);
		nSel = pCombo->GetCount() - 1;
	}
	pCombo->SetCurSel(nSel);
	SetStatus(_T("已加载模型: ") + MakeModelDisplayName(strPath));
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
