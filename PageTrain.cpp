// PageTrain.cpp

#include "pch.h"
#include "framework.h"
#include "YOLO_Tool.h"
#include "PageTrain.h"
#include "GreenEnv.h"
#include "UiUtil.h"
#include "AppConfig.h"
#include "afxdialogex.h"
#include <shlwapi.h>
#include <vector>
#include "PageAnnotate.h"

#include "File/zlib/zlib.h"
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "zlib.lib")

CPageTrain* g_CPageTrain = NULL;
CComboBox* g_pComboXUN = 0;
CComboBox* g_pExter = 0;

extern CComboBox* g_pComboBIAOZHU;
extern CComboBox* g_pComboTEST;
#ifdef _DEBUG
#define new DEBUG_NEW
#endif

static bool ReadAllBytes(const CString& strPath, std::vector<BYTE>& buf)
{
	CFile file;
	if (!file.Open(strPath, CFile::modeRead | CFile::shareDenyWrite))
		return false;
	ULONGLONG nLen = file.GetLength();
	buf.resize((size_t)nLen);
	if (nLen > 0)
		file.Read(buf.data(), (UINT)nLen);
	file.Close();
	return true;
}

static bool CompressBytes(const std::vector<BYTE>& src, std::vector<BYTE>& dst)
{
	if (src.empty())
	{
		dst.clear();
		return true;
	}
	uLong bound = compressBound((uLong)src.size());
	dst.resize(bound);
	uLongf nDst = bound;
	if (compress2(dst.data(), &nDst, src.data(), (uLong)src.size(), Z_BEST_COMPRESSION) != Z_OK)
		return false;
	dst.resize((size_t)nDst);
	return true;
}

static void WriteU32(BYTE* p, unsigned int v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (BYTE)(v >> (8 * i));
}


static bool ShutdownComputer(int nDelaySeconds)
{
	HANDLE hToken = NULL;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		return false;

	bool bOk = false;
	TOKEN_PRIVILEGES tkp = { 0 };
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (::LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid))
	{
		::SetLastError(ERROR_SUCCESS);
		bOk = (::AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL) != FALSE
			&& ::GetLastError() == ERROR_SUCCESS);
	}
	::CloseHandle(hToken);

	if (!bOk)
		return false;

	return ::InitiateSystemShutdownExW(NULL,
		L"训练已完成，电脑将在倒计时结束后关机。如需取消，请在命令行执行 shutdown /a",
		(DWORD)nDelaySeconds, FALSE, FALSE,
		SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_MAINTENANCE | SHTDN_REASON_FLAG_PLANNED) != FALSE;
}


static bool LoadClassTable(std::vector<BYTE>& table)
{
	table.clear();

	CString strPath;
	CString strDataset = GetProjectDatasetDir();
	if (!strDataset.IsEmpty())
		strPath = strDataset + _T("\\classes.txt");
	if (strPath.IsEmpty() || !::PathFileExists(strPath))
	{
		CString strGreen = GetGreenRoot();
		if (!strGreen.IsEmpty())
			strPath = strGreen + _T("\\config\\classes.txt");
	}
	if (strPath.IsEmpty() || !::PathFileExists(strPath))
		return false;

	std::vector<BYTE> raw;
	if (!ReadAllBytes(strPath, raw))
		return false;

	size_t nBegin = 0;
	if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
		nBegin = 3;
	if (nBegin >= raw.size())
		return false;

	std::vector<BYTE> ansi;
	{
		LPCSTR psz = (LPCSTR)&raw[nBegin];
		int nLen = (int)(raw.size() - nBegin);

		int nWide = ::MultiByteToWideChar(CP_UTF8, 0, psz, nLen, NULL, 0);
		if (nWide > 0)
		{
			std::vector<wchar_t> wbuf((size_t)nWide);
			::MultiByteToWideChar(CP_UTF8, 0, psz, nLen, wbuf.data(), nWide);

			int nAnsi = ::WideCharToMultiByte(CP_ACP, 0, wbuf.data(), nWide, NULL, 0, NULL, NULL);
			if (nAnsi > 0)
			{
				ansi.resize((size_t)nAnsi);
				::WideCharToMultiByte(CP_ACP, 0, wbuf.data(), nWide, (LPSTR)ansi.data(), nAnsi, NULL, NULL);
			}
		}
	}

	const std::vector<BYTE>& src = ansi.empty() ? raw : ansi;
	size_t nStart = ansi.empty() ? nBegin : 0;

	for (size_t i = nStart; i < src.size(); i++)
	{
		if (src[i] == '\r')
			continue;
		table.push_back(src[i]);
	}
	while (!table.empty() && (table.back() == '\n' || table.back() == ' ' || table.back() == '\t'))
		table.pop_back();

	return !table.empty();
}

static UINT AFX_CDECL GpuProbeThreadProc(LPVOID pParam)
{
	// 结果写入 IsGpuUsable 的进程内缓存
	IsGpuUsable();

	HWND hWnd = (HWND)pParam;
	if (::IsWindow(hWnd))
		::PostMessage(hWnd, WM_USER_GPU_PROBE, 0, 0);
	return 0;
}

CPageTrain::CPageTrain(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_PAGE_TRAIN, pParent)
	, m_ptArr(ProjectWeights())
	, m_bExporting(false)
{
}

BEGIN_MESSAGE_MAP(CPageTrain, CDialogEx)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_SHOWWINDOW()
	ON_BN_CLICKED(IDC_BTN_TRAIN_START, &CPageTrain::OnBnClickedBtnTrainStart)
	ON_BN_CLICKED(IDC_BTN_TRAIN_STOP, &CPageTrain::OnBnClickedBtnTrainStop)
	ON_BN_CLICKED(IDC_BTN_EXPORT, &CPageTrain::OnBnClickedBtnExport)
	ON_MESSAGE(WM_USER_RECVLOG, &CPageTrain::OnRecvLog)
	ON_MESSAGE(WM_USER_PROC_EXIT, &CPageTrain::OnProcExit)
	ON_MESSAGE(WM_USER_GPU_PROBE, &CPageTrain::OnGpuProbeDone)
	ON_WM_HELPINFO()
	ON_WM_CTLCOLOR()
	ON_CBN_SELCHANGE(IDC_COMBO_EXPORT_FMT2, &CPageTrain::OnCbnSelchangeComboExportFmt2)
END_MESSAGE_MAP()

void CPageTrain::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_LOG_TRAIN, m_richLog);
}

BOOL CPageTrain::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	CEdit* p = (CEdit*)GetDlgItem(IDC_EDIT_LOG_TRAIN);
	g_pComboXUN = (CComboBox*)GetDlgItem(IDC_COMBO_WEIGHTS_TRAIN);
	g_pExter = (CComboBox*)GetDlgItem(IDC_COMBO_EXPORT_FMT2);
	g_CPageTrain = this;
	p->SetReadOnly(TRUE);

	FillWeightsCombo();
	FillExportFormats();

	RefreshEpochsDefault();
	SetDlgItemText(IDC_EDIT_IMGSZ, _T("640"));
	SetDlgItemText(IDC_EDIT_BATCH, _T("4"));

	GetDlgItem(IDC_BTN_TRAIN_STOP)->EnableWindow(FALSE);

	CheckDlgButton(IDC_CHK_GPU, BST_UNCHECKED);
	if (!HasAnyGpuEnv())
	{
		GetDlgItem(IDC_CHK_GPU)->EnableWindow(FALSE);
	}
	else
	{
		// 后台探测显卡是否真能跑，避免使用者自己折腾 CUDA/驱动
		AfxBeginThread(GpuProbeThreadProc, GetSafeHwnd(),
			THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
	}

	SetStatus(_T("就绪"));
	Layout();

	CEdit* pEditLog = (CEdit*)GetDlgItem(IDC_EDIT_LOG_TRAIN);
	if (pEditLog == nullptr)
	{
		m_Maxline = 0;
		return TRUE;
	}

	CRect rcClient;
	pEditLog->GetClientRect(&rcClient);
	CDC* pDC = pEditLog->GetDC();


	CFont* pOldFont = pDC->SelectObject(pEditLog->GetFont());

	TEXTMETRIC tm = { 0 };
	pDC->GetTextMetrics(&tm);
	int nLineHeight = tm.tmHeight + tm.tmExternalLeading;

	pDC->SelectObject(pOldFont);
	pEditLog->ReleaseDC(pDC);

	if (nLineHeight <= 0)
	{
		m_Maxline = 0;
		return TRUE;
	}

	m_Maxline = rcClient.Height() / nLineHeight;

	return TRUE;
}


BOOL CPageTrain::PreTranslateMessage(MSG* pMsg)
{

	if (UiForwardEditPaste(pMsg))
		return TRUE;

	if (UiSwallowDialogKey(pMsg))
		return TRUE;
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CPageTrain::OnOK()
{
}


void CPageTrain::OnCancel()
{
}

void CPageTrain::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	Layout();

	CEdit* pEditLog = (CEdit*)GetDlgItem(IDC_EDIT_LOG_TRAIN);
	if (pEditLog == nullptr || !pEditLog->IsWindowVisible())
	{
		return;
	}

	CRect rcClient;
	pEditLog->GetClientRect(&rcClient);
	int nClientHeight = rcClient.Height();
	if (nClientHeight <= 0)
	{
		m_Maxline = 0;
		return;
	}

	CDC* pDC = pEditLog->GetDC();
	if (pDC == nullptr)
	{
		m_Maxline = 0;
		return;
	}

	CFont* pOldFont = pDC->SelectObject(pEditLog->GetFont());

	TEXTMETRIC tm = { 0 };
	pDC->GetTextMetrics(&tm);
	int nLineHeight = tm.tmHeight + tm.tmExternalLeading;

	pDC->SelectObject(pOldFont);
	pEditLog->ReleaseDC(pDC);

	if (nLineHeight <= 0)
	{
		m_Maxline = 0;
		return;
	}

	m_Maxline = nClientHeight / nLineHeight;
}


void CPageTrain::Layout()
{
	CRect rcClient;
	GetClientRect(&rcClient);
	int W = rcClient.Width();
	int H = rcClient.Height();
	if (W < 100 || H < 100)
		return;

	int dpi = UiDpi(m_hWnd);
	auto mv = [&](UINT nID, int x, int y, int w, int h)
	{
		CWnd* pWnd = GetDlgItem(nID);
		if (pWnd != NULL)
			pWnd->MoveWindow(::MulDiv(x, dpi, 96), ::MulDiv(y, dpi, 96),
				max(1, ::MulDiv(w, dpi, 96)), max(1, ::MulDiv(h, dpi, 96)));
	};

	const int M = 8, GAP = 8, RIGHTW = 240, BARH = 22;


	int rightX = W - M - RIGHTW;
	int colH = H - M * 2;

	int logW = rightX - GAP - M;
	if (logW < 200)
		logW = 200;

	mv(IDC_GRP_LOG_TRAIN, M, M, logW, colH - 20);
	mv(IDC_EDIT_LOG_TRAIN, M + 8, M + 20, logW - 16, colH - 48);

	mv(IDC_GRP_TRAIN_PARAMS, rightX, M, RIGHTW, colH);
	mv(IDC_LBL_WEIGHTS_TRAIN, rightX + 10, M + 26, 44, 14);
	mv(IDC_COMBO_WEIGHTS_TRAIN, rightX + 10, M + 44, RIGHTW - 20, 300);
	mv(IDC_LBL_EPOCHS, rightX + 10, M + 80, 56, 14);
	mv(IDC_EDIT_EPOCHS, rightX + 70, M + 78, 64, 21);
	mv(IDC_LBL_IMGSZ, rightX + 10, M + 110, 56, 14);
	mv(IDC_EDIT_IMGSZ, rightX + 70, M + 108, 64, 21);
	mv(IDC_LBL_BATCH, rightX + 10, M + 140, 56, 14);
	mv(IDC_EDIT_BATCH, rightX + 70, M + 138, 64, 21);
	mv(IDC_CHK_RESUME, rightX + 10, M + 168, RIGHTW - 20, 18);
	mv(IDC_CHK_GPU, rightX + 10, M + 192, RIGHTW - 20, 18);

	//

	mv(IDC_LBL_EXPORT_FMT, rightX + 10, M + 268, RIGHTW - 20, 18);
	mv(IDC_COMBO_EXPORT_FMT, rightX + 10, M + 292, RIGHTW - 54, 25);

	mv(IDC_LBL_EPOCHS2, rightX + 10, M + 328, RIGHTW - 180, 30);
	mv(IDC_COMBO_EXPORT_FMT2, rightX + 70, M + 322, RIGHTW - 130, 200);
	mv(IDC_BTN_EXPORT, rightX + 180, M + 322, RIGHTW - 200, 25);
	
	int btnW = (RIGHTW - 28) / 2;
	int btnY = M + colH - 8 - 30;

	mv(IDC_CHK_EXIT, rightX + 10, btnY - 35, btnW, 28);

	mv(IDC_BTN_TRAIN_START, rightX + 10, btnY, btnW, 28);
	mv(IDC_BTN_TRAIN_STOP, rightX + 18 + btnW, btnY, btnW, 28);

	mv(IDC_STATIC_TRAIN_STATUS, 50, btnY + 20, btnW, 28);

}


int CPageTrain::CountTrainImages()
{
	// 与截图页、标注页共用同一份内存数组，不扫盘
	EnsureProjectImages(GetProjectImagesDir());
	return GetProjectImageCount();
}


void CPageTrain::RefreshEpochsDefault()
{
	int nCount = CountTrainImages();
	if (nCount <= 0)
		nCount = 50;

	CString strCur;
	GetDlgItemText(IDC_EDIT_EPOCHS, strCur);
	strCur.Trim();

	CString strLast;
	strLast.Format(_T("%d"), m_nLastEpochsDefault);
	if (!strCur.IsEmpty() && strCur != strLast)
		return;
	CString strNew;
	strNew.Format(_T("%d"), nCount);
	SetDlgItemText(IDC_EDIT_EPOCHS, strNew);
	m_nLastEpochsDefault = nCount;
}


void CPageTrain::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CDialogEx::OnShowWindow(bShow, nStatus);
	if (bShow)
	{
		RefreshEpochsDefault();
		RepairDataYamlPaths();
	}
}



void RefreshWeightCombos()
{
	std::vector<YOLOPT>& weights = ProjectWeights();
	if (g_pExter)
	{
		g_pExter->ResetContent();
	}

	long idx = 0;
	for (size_t i = 0; i < weights.size(); i++)
	{
		weights[i].ptdex = (long)i;
		if (g_pExter)
		{
			CStringA str(weights[i].pt.Mid(0, 1));

			if (str[0] >= L'0' && str[0] <= L'9')
			{
				long  dx = StrToInt(weights[i].pt.Mid(0, 1));
				g_pExter->InsertString(g_pExter->GetCount(), weights[i].pt);
				if (dx > idx)
				{
					idx = dx;

					g_pExter->SetCurSel(g_pExter->GetCount() - 1);
				}

			}
		}

	}


	CComboBox* combos[] = { g_pComboXUN, g_pComboBIAOZHU, g_pComboTEST };
	for (int c = 0; c < 3; c++)
	{
		CComboBox* pCombo = combos[c];
		if (pCombo == NULL)
			continue;

		int nOldSel = pCombo->GetCurSel();
		CString strOldSel, strOldPath;
		if (pCombo == g_pComboTEST && nOldSel >= 0 && nOldSel < (int)weights.size())
			strOldPath = weights[nOldSel].ptName;
		else if (nOldSel >= 0)
			pCombo->GetLBText(nOldSel, strOldSel);

		pCombo->ResetContent();
		for (size_t i = 0; i < weights.size(); i++)
		{
			pCombo->InsertString((int)i, weights[i].pt);
		}

		int nNewSel = -1;
		if (pCombo == g_pComboTEST && !strOldPath.IsEmpty())
		{
			for (size_t i = 0; i < weights.size(); i++)
			{
				if (weights[i].ptName.CompareNoCase(strOldPath) == 0)
				{
					nNewSel = (int)i;
					break;
				}
			}
		}
		else
			nNewSel = pCombo->FindStringExact(-1, strOldSel);
		if (nNewSel < 0 && pCombo->GetCount() > 0)
			nNewSel = min(max(nOldSel, 0), pCombo->GetCount() - 1);
		pCombo->SetCurSel(nNewSel);
	}
}


void IsTestPt()
{
	std::vector<YOLOPT>& weights = ProjectWeights();
	for (size_t i = weights.size(); i > 0; i--)
	{
		if (!filexist(weights[i - 1].ptName))
			weights.erase(weights.begin() + i - 1);
	}
	RefreshWeightCombos();
}


void CPageTrain::GetPtPath()
{
	CString strRuns = GetProjectRunsDir();
	if (!strRuns.IsEmpty())
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(strRuns + _T("\\*"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory() || finder.IsDots())
				continue;

			CString strPath = strRuns + _T("\\") + finder.GetFileName() + _T("\\weights\\best.pt");
			CFileFind finderCheck;
			if (finderCheck.FindFile(strPath) && !IsPt(m_ptArr, strPath))
			{
				YOLOPT t_p;
				t_p.pt = GetRunWeightDisplayName(finder.GetFileName());
				t_p.ptName = strPath;
				t_p.ptdex = (long)m_ptArr.size();
				m_ptArr.push_back(t_p);

			}
			finderCheck.Close();
		}
		finder.Close();
	}
	RefreshWeightCombos();
}



void CPageTrain::FillWeightsCombo()
{
	CComboBox* pCombo = g_pComboXUN;
	if (pCombo == nullptr)
		return;

	g_pExter->ResetContent();
	pCombo->ResetContent();

	CStringArray arrDirs;
	{
		CString strProj = GetProjectModelsDir();
		if (!strProj.IsEmpty())
			arrDirs.Add(strProj);
		CString strGreen = GetGreenRoot();
		if (!strGreen.IsEmpty())
			arrDirs.Add(strGreen + _T("\\models"));
	}
	long idx = 0;
	for (INT_PTR d = 0; d < arrDirs.GetSize(); d++)
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(arrDirs[d] + _T("\\*.pt"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (!finder.IsDirectory())
			{
				if (pCombo->FindStringExact(-1, finder.GetFileName()) < 0)
				{
					if (IsPt(m_ptArr, arrDirs[d] + "\\" + finder.GetFileName()))
					{
						continue;
					}
					YOLOPT t_p;
					t_p.pt = finder.GetFileName();
					t_p.ptName = arrDirs[d] + "\\" + finder.GetFileName();
					t_p.ptdex = idx;
					pCombo->InsertString(t_p.ptdex, finder.GetFileName());
					idx += 1;
					m_ptArr.push_back(t_p);
				}
			}
		}
		finder.Close();
	}

	GetPtPath(); //取自己训练后的模型
	/*if (pCombo->GetCount() == 0)
	{
		pCombo->AddString(_T("yolo11n.pt"));
		pCombo->AddString(_T("yolo11s.pt"));
		pCombo->AddString(_T("yolov8n.pt"));
		pCombo->AddString(_T("yolov8s.pt"));
	}*/

	int nSel = pCombo->FindStringExact(-1, _T("yolo11n.pt"));
	pCombo->SetCurSel(nSel >= 0 ? nSel : 0);
}


void CPageTrain::FillExportFormats()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_EXPORT_FMT);
	if (pCombo == NULL)
		return;
	pCombo->ResetContent();
	pCombo->AddString(_T("KEYOLO"));
	pCombo->AddString(_T("NCNN"));
	pCombo->AddString(_T("ONNX"));
	pCombo->AddString(_T("TorchScript"));
	pCombo->AddString(_T("TensorRT"));
	pCombo->AddString(_T("TFLite"));
	pCombo->AddString(_T("OpenVINO"));
	pCombo->AddString(_T("TensorFlow"));

	pCombo->SetCurSel(0);
}

void CPageTrain::OnBnClickedBtnTrainStart()
{
	Invalidate();
	UpdateWindow();

	bool bGpu = (IsDlgButtonChecked(IDC_CHK_GPU) == BST_CHECKED);
	if (bGpu)
	{
		// 勾了 GPU 但显卡跑不动（驱动过旧、算力不支持等）时自动退回 CPU
		CString strReason;
		if (!IsGpuUsable(&strReason))
		{
			bGpu = false;
			m_bGpuProbeDone = true;
			m_bGpuUsable = false;
			CheckDlgButton(IDC_CHK_GPU, BST_UNCHECKED);
			CString strTip;
			strTip.Format(_T("==== GPU 不可用：%s，已自动切换为 CPU 训练 ====\r\n"),
				(LPCTSTR)strReason);
			AppendLog(strTip);
		}
	}

	CString strGreen = bGpu ? GetGpuEnvRoot() : GetGreenRoot();
	CString strPython = bGpu ? GetEnvPython(strGreen) : GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		if (bGpu)
			AfxMessageBox(_T("YOLO_Green_GPU standalone package or python.exe not found.\nPlease ensure YOLO_Green_GPU and YOLO_Green are placed side by side."), MB_ICONWARNING);
		else
			AfxMessageBox(_T("YOLO_Green package or python.exe not found."), MB_ICONWARNING);
		return;
	}

	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_WEIGHTS_TRAIN);
	CString strWeights;
	int nSel = pCombo->GetCurSel();
	if (nSel >= 0)
		pCombo->GetLBText(nSel, strWeights);
	if (strWeights.IsEmpty())
	{
		AfxMessageBox(_T("Please select a training weight file."), MB_ICONWARNING);
		return;
	}

	CString strEpochs, strImgsz, strBatch;
	GetDlgItemText(IDC_EDIT_EPOCHS, strEpochs);
	GetDlgItemText(IDC_EDIT_IMGSZ, strImgsz);
	GetDlgItemText(IDC_EDIT_BATCH, strBatch);
	strEpochs.Trim(); strImgsz.Trim(); strBatch.Trim();
	if (_ttoi(strEpochs) <= 0) strEpochs = _T("100");
	if (_ttoi(strImgsz) <= 0) strImgsz = _T("640");
	if (_ttoi(strBatch) <= 0) strBatch = _T("4");
	CString strData = GetProjectDataYaml();
	if (strData.IsEmpty() || !::PathFileExists(strData))
	{
		AfxMessageBox(_T("data.yaml not found. Please select project root on Capture page first (auto-creates dataset\\data.yaml)."), MB_ICONWARNING);
		return;
	}

	if (RepairDataYamlPaths())
		AppendLog(_T("data.yaml 中的路径参数已修正为当前项目位置\r\n"));

	// Weights: dropdown shows file names (e.g. yolo11n.pt or train-3/best.pt)



	bool bResume = (IsDlgButtonChecked(IDC_CHK_RESUME) == BST_CHECKED);


	CString strRuns = GetProjectRunsDir();
	if (strRuns.IsEmpty())
	{
		AfxMessageBox(_T("Training output dir not found. Please select project root on Capture page first."), MB_ICONWARNING);
		return;
	}

	m_strBestPath.Empty();

	CString strScript = strGreen + _T("\\scripts\\train.py");
	if (!::PathFileExists(strScript))
	{
		AfxMessageBox(_T("scripts\train.py not found. Please check green package integrity."), MB_ICONWARNING);
		return;
	}

	CString strCmd;

	LPCTSTR pszDevice = bGpu ? _T("0") : _T("cpu");
	if (bResume)
	{
		CString bestPath = GetBestWeightPath();
		CString lastPath;
		int nPos = bestPath.ReverseFind(_T('\\'));
		if (nPos != -1)
		{
			lastPath = bestPath.Left(nPos + 1) + _T("last.pt");
		}
		else
		{
			return;
		}

		strCmd.Format(
			_T("\"%s\" -X utf8 -u \"%s\" --weights \"%s\" --resume"),
			(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)lastPath);
	}
	else
	{
		long idx = 0;
		CString strWeightsPath;
		for (size_t i = 0; i < m_ptArr.size(); i++)
		{
			if (m_ptArr[i].pt == strWeights || m_ptArr[i].ptdex == nSel)
			{
				strWeightsPath = m_ptArr[i].ptName;
				idx = i;
				break;
			}
		}

		if (strWeightsPath.IsEmpty())
		{
			AppendLog(_T("未找到pt权重文件"));
			return;
		}

		CFileFind finderCheck;
		BOOL bExist = finderCheck.FindFile(strWeightsPath);
		if (bExist)
		{
			finderCheck.FindNextFile();

			if (!finderCheck.IsDirectory())
			{

			}
			else
			{

				finderCheck.Close();
				IsTestPt();
				AppendLog(_T("该pt权重文件已经被删除"));
				return;
			}
		}
		else
		{

			finderCheck.Close();
			IsTestPt();
			AppendLog(_T("该pt权重文件已经被删除"));

			return;

		}
		finderCheck.Close();

		strCmd.Format(
			_T("\"%s\" -X utf8 -u \"%s\" --data \"%s\" --weights \"%s\" --epochs %s --imgsz %s --batch %s --device %s --project \"%s\" --name train"),
			(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strData, (LPCTSTR)strWeightsPath,
			(LPCTSTR)strEpochs, (LPCTSTR)strImgsz, (LPCTSTR)strBatch, pszDevice, (LPCTSTR)strRuns);
	}




	CString strLog;
	LPCTSTR pszDevName = bGpu ? _T("GPU") : _T("CPU");
	if (bResume)
		strLog.Format(_T("==== 续训开始（%s，从 %s）====\r\n%s\r\n"), pszDevName, (LPCTSTR)strWeights, (LPCTSTR)strCmd);
	else
		strLog.Format(_T("==== 开始训练（%s）====\r\n%s\r\n"), pszDevName, (LPCTSTR)strCmd);
	AppendLog(strLog);

	m_bExporting = false;
	if (!m_capture.StartProcess(strCmd, GetSafeHwnd()))
	{
		AppendLog(_T("创建训练进程失败。\r\n"));
		SetStatus(_T("训练启动失败"));
		return;
	}
	SetRunning(true);
	SetStatus(_T("训练中..."));
}

void CPageTrain::OnBnClickedBtnTrainStop()
{
	if (!m_capture.IsRunning())
		return;

	AppendLog(_T("==== 正在停止，请稍等 ====\r\n"));
	SetStatus(_T("正在停止..."));
	m_capture.Stop();
	SetRunning(false);
	SetStatus(_T("已停止"));
}

void CPageTrain::OnBnClickedBtnExport()
{
	StartExport();
}

CString CPageTrain::GetBestWeightPath()
{
	return ::GetBestWeightPath();
}


void CPageTrain::StartExport()
{
	if (m_bExporting)
	{
		AppendLog(_T("正在导出，请等待完成...\r\n"));
		return;
	}

	bool bUseGpu = (IsDlgButtonChecked(IDC_CHK_GPU) == BST_CHECKED);
	if (bUseGpu)
	{
		CString strReason;
		if (!IsGpuUsable(&strReason))
		{
			bUseGpu = false;
			m_bGpuProbeDone = true;
			m_bGpuUsable = false;
			CheckDlgButton(IDC_CHK_GPU, BST_UNCHECKED);
			CString strTip;
			strTip.Format(_T("GPU 不可用：%s，导出改用 CPU 环境。\r\n"), (LPCTSTR)strReason);
			AppendLog(strTip);
		}
	}
	CString strGreen = bUseGpu ? GetGpuEnvRoot() : GetGreenRoot();
	CString strPython = bUseGpu ? GetEnvPython(strGreen) : GetGreenPython();

	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		AfxMessageBox(_T("python.exe not found in YOLO_Green or YOLO_Green_GPU package."), MB_ICONWARNING);
		return;
	}

	CString strBest;
	int nSel = g_pExter->GetCurSel();
	if (nSel >= 0)
	{
		g_pExter->GetLBText(nSel, strBest);
	}

	for (size_t i = 0; i < m_ptArr.size(); i++)
	{
		if (m_ptArr[i].pt== strBest)
		{
			strBest = m_ptArr[i].ptName;
			break;
		}
	}

	
	if (!::PathFileExists(strBest))
	{
		AppendLog(_T("best.pt not found, please train first\r\n"));
		SetStatus(_T("best.pt not found, please train first"));
		return;
	}

	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_EXPORT_FMT);
	if (pCombo == nullptr)
	{
		AppendLog(_T("导出格式下拉框控件获取失败\r\n"));
		return;
	}
	 nSel = pCombo->GetCurSel();
	if (nSel == CB_ERR)
	{
		AfxMessageBox(_T("请先选择导出格式！"), MB_ICONWARNING);
		return;
	}

	CString strFormat;
	pCombo->GetLBText(nSel, strFormat);
	strFormat.Trim();
	strFormat.MakeLower();
	if (strFormat == _T("tensorrt"))
		strFormat = _T("engine");
	else if (strFormat == _T("tensorflow"))
		strFormat = _T("pb");
	if (strFormat.IsEmpty())
	{
		AppendLog(_T("导出格式解析失败\r\n"));
		return;
	}

	m_bPackKeyolo = (strFormat == _T("keyolo"));
	if (m_bPackKeyolo)
		strFormat = _T("ncnn");

	CString strImgsz;
	GetDlgItemText(IDC_EDIT_IMGSZ, strImgsz);
	strImgsz.Trim();
	if (_ttoi(strImgsz) <= 0)
		strImgsz = _T("640");

	CString strScript = strGreen + _T("\\scripts\\export_model.py");
	if (!::PathFileExists(strScript))
	{
		AfxMessageBox(_T("scripts\\export_model.py not found. Please check green package integrity."), MB_ICONWARNING);
		return;
	}

	CString strCmd;
	strCmd.Format(
		_T("\"%s\" -X utf8 -u \"%s\" --weights \"%s\" --format %s --imgsz %s"),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strBest, (LPCTSTR)strFormat, (LPCTSTR)strImgsz);

	CString strLog;
	strLog.Format(_T("==== 开始导出 %s ====\r\n%s\r\n"), (LPCTSTR)strFormat, (LPCTSTR)strCmd);
	AppendLog(strLog);

	m_bExporting = true;
	if (!m_capture.StartProcess(strCmd, GetSafeHwnd()))
	{
		m_bExporting = false;
		AppendLog(_T("创建导出进程失败。\r\n"));
		SetStatus(_T("导出启动失败"));
		return;
	}
	SetRunning(true);
	SetStatus(_T("导出中..."));
}


bool CPageTrain::StartKeyoloPack()
{
	CString strBest = GetBestWeightPath();
	CString strNcnnDir;
	if (!strBest.IsEmpty())
	{
		CString strStem = strBest;
		int nDot = strStem.ReverseFind(_T('.'));
		if (nDot > 0)
			strStem = strStem.Left(nDot);
		strNcnnDir = strStem + _T("_ncnn");
	}

	if (!::PathIsDirectory(strNcnnDir))
	{
		int nSlash = strBest.ReverseFind(_T('\\'));
		CString strDir = (nSlash > 0) ? strBest.Left(nSlash + 1) : CString(_T(""));
		if (!strDir.IsEmpty())
		{
			CFileFind finder;
			BOOL bMore = finder.FindFile(strDir + _T("*_ncnn_model"));
			while (bMore)
			{
				bMore = finder.FindNextFile();
				if (finder.IsDirectory())
					strNcnnDir = finder.GetFilePath();
			}
			finder.Close();
		}
	}

	if (!::PathIsDirectory(strNcnnDir))
	{
		AppendLog(_T("未找到 ncnn 导出目录，无法打包 .keyolo\r\n"));
		return false;
	}

	CString strBinFile, strParamFile;
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(strNcnnDir + _T("\\*.*"));
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (finder.IsDirectory())
				continue;
			CString strName = finder.GetFileName();
			CString strLower = strName;
			strLower.MakeLower();
			if (strLower.Right(4) == _T(".bin"))
				strBinFile = finder.GetFilePath();
			else if (strLower.Right(6) == _T(".param"))
				strParamFile = finder.GetFilePath();
		}
		finder.Close();
	}

	if (strBinFile.IsEmpty() || strParamFile.IsEmpty())
	{
		AppendLog(_T("ncnn 目录中未找到 .bin/.param 文件，无法打包 .keyolo\r\n"));
		return false;
	}

	std::vector<BYTE> binRaw, paramRaw;
	if (!ReadAllBytes(strBinFile, binRaw) || !ReadAllBytes(strParamFile, paramRaw))
	{
		AppendLog(_T("读取 .bin/.param 失败，无法打包 .keyolo\r\n"));
		return false;
	}

	std::vector<BYTE> z_file(binRaw.size() + paramRaw.size());
	memcpy(z_file.data(), binRaw.data(), binRaw.size());
	memcpy(z_file.data() + binRaw.size(), paramRaw.data(), paramRaw.size());

	std::vector<BYTE> binZip;
	if (!CompressBytes(z_file, binZip))
	{
		AppendLog(_T("zlib 压缩失败\r\n"));
		return false;
	}

	std::vector<BYTE> clsTable;
	if (!LoadClassTable(clsTable))
		AppendLog(_T("未找到 classes.txt，.keyolo 中将不含类别表\r\n"));
	long pos = strNcnnDir.ReverseFind(_T('\\'));

#pragma pack(push, 1)
	typedef struct _ModelPackHeader
	{
		int32_t magic;
		int32_t bin_raw_size;
		int32_t param_raw_size;
		int32_t binZip_size;
		int32_t table_size;
		char data;
	}ModelPackHeader, * PModelPackHeader;
#pragma pack(pop)

	ModelPackHeader  t_ModelPackHeader;
	t_ModelPackHeader.magic = 0x4F4C4F59;
	t_ModelPackHeader.bin_raw_size = binRaw.size();
	t_ModelPackHeader.param_raw_size = paramRaw.size();
	t_ModelPackHeader.binZip_size = binZip.size();
	t_ModelPackHeader.table_size = clsTable.size();

	std::string  str = (char*)clsTable.data();
	srand((unsigned)GetTickCount());
	int nPad = 16 + rand() % 241;

	std::vector<BYTE> SVAE_file(sizeof(ModelPackHeader) + t_ModelPackHeader.binZip_size + t_ModelPackHeader.table_size + nPad);

	memcpy(SVAE_file.data(), &t_ModelPackHeader, sizeof(ModelPackHeader));
	PModelPackHeader p = (PModelPackHeader)SVAE_file.data();
	memcpy(&p->data, binZip.data(), t_ModelPackHeader.binZip_size);
	memcpy(&p->data + t_ModelPackHeader.binZip_size, clsTable.data(), t_ModelPackHeader.table_size);



	CString strOut = strNcnnDir.Mid(0, pos + 1) + _T("ncnn.keyolo");
	if (::PathFileExists(strOut))
		::DeleteFile(strOut);

	CFile file;
	if (!file.Open(strOut, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
	{
		AppendLog(_T("创建 .keyolo 文件失败\r\n"));
		return false;
	}
	if (!SVAE_file.empty())
		file.Write(SVAE_file.data(), (UINT)SVAE_file.size());
	file.Close();

	int nClassCount = 0;
	if (!clsTable.empty())
	{
		nClassCount = 1;
		for (size_t i = 0; i < clsTable.size(); i++)
			if (clsTable[i] == '\n')
				nClassCount++;
	}

	CString strMsg;
	strMsg.Format(
		_T("==== 打包完成: %s\r\n"),
		(LPCTSTR)strOut, (unsigned int)clsTable.size(), nClassCount);
	AppendLog(strMsg);
	return true;
}


void CPageTrain::OnDestroy()
{
	m_capture.Stop();
	CDialogEx::OnDestroy();
}

void CPageTrain::AppendLog(const CString& strText)
{
	CEdit* pEdit = (CEdit*)GetDlgItem(IDC_EDIT_LOG_TRAIN);
	if (pEdit == NULL)
		return;

	CStringW line = strText;


	if (m_line >= m_Maxline)
	{
		m_richLog.SetWindowText(L"");

		m_line = 0;
		return;
	}

	m_line += 1;
	long endPos = m_richLog.GetTextLength();
	m_richLog.SetSel(endPos, endPos);
	m_richLog.ReplaceSel(line);
	m_richLog.LineScroll(m_richLog.GetLineCount() - 1);
}

void CPageTrain::SetStatus(LPCTSTR lpszText)
{
	SetDlgItemText(IDC_STATIC_TRAIN_STATUS, lpszText);
}

void CPageTrain::SetRunning(bool bRunning)
{
	GetDlgItem(IDC_BTN_TRAIN_START)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_BTN_TRAIN_STOP)->EnableWindow(bRunning ? TRUE : FALSE);
	GetDlgItem(IDC_BTN_EXPORT)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_COMBO_EXPORT_FMT)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_COMBO_WEIGHTS_TRAIN)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_EPOCHS)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_IMGSZ)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_BATCH)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_CHK_RESUME)->EnableWindow(bRunning ? FALSE : TRUE);
	// 探测结果判定为不可用时，不再让使用者勾选 GPU
	BOOL bEnableGpu = (!bRunning && HasAnyGpuEnv()
		&& (!m_bGpuProbeDone || m_bGpuUsable)) ? TRUE : FALSE;
	GetDlgItem(IDC_CHK_GPU)->EnableWindow(bEnableGpu);
}


BOOL CPageTrain::OnHelpInfo(HELPINFO* pHelpInfo)
{


	return 1;
}

LRESULT CPageTrain::OnRecvLog(WPARAM wParam, LPARAM lParam)
{
	CStringW* pText = reinterpret_cast<CStringW*>(lParam);
	if (!pText)
		return 0;

	CStringW line = *pText;
	BOOL bStdErr = (wParam == 1);
	delete pText;

	CHARFORMAT2 cf;
	cf.cbSize = sizeof(CHARFORMAT2);
	cf.dwMask = CFM_COLOR;

	if (bStdErr)
	{
		cf.crTextColor = RGB(220, 40, 40);
	}
	else if (line.Find(L"best weight found") != -1)
	{
		cf.crTextColor = RGB(0, 160, 0);
	}
	else
	{
		cf.crTextColor = RGB(0, 0, 0);
	}

	if (!m_richLog.GetSafeHwnd() || !m_richLog.IsWindowVisible())
		return 0;

	if (m_line >= m_Maxline)
	{
		m_richLog.SetWindowText(L"");
		m_line = 0;
	}

	m_line += 1;
	line += L"\r\n";

	long endPos = m_richLog.GetTextLength();
	m_richLog.SetSel(endPos, endPos);
	m_richLog.SetSelectionCharFormat(cf);
	m_richLog.ReplaceSel(line);

	m_richLog.LineScroll(m_richLog.GetLineCount() - 1);

	return 0;
}


LRESULT CPageTrain::OnGpuProbeDone(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	CString strReason, strName;
	bool bUsable = IsGpuUsable(&strReason, &strName);
	m_bGpuProbeDone = true;
	m_bGpuUsable = bUsable;

	CWnd* pChk = GetDlgItem(IDC_CHK_GPU);
	if (pChk == NULL)
		return 0;

	CString strLog;
	if (!bUsable)
	{
		CheckDlgButton(IDC_CHK_GPU, BST_UNCHECKED);
		pChk->EnableWindow(FALSE);
		strLog.Format(_T("==== GPU 不可用：%s，已自动使用 CPU 训练 ====\r\n"),
			(LPCTSTR)strReason);
	}
	else
	{
		pChk->EnableWindow(m_capture.IsRunning() ? FALSE : TRUE);
		strLog.Format(_T("==== 已检测到 GPU：%s（%s）====\r\n"),
			(LPCTSTR)strName, (LPCTSTR)strReason);
	}
	AppendLog(strLog);
	return 0;
}

LRESULT CPageTrain::OnProcExit(WPARAM wParam, LPARAM lParam)
{
	DWORD dwExit = (DWORD)wParam;
	bool bExport = m_bExporting;
	m_bExporting = false;

	m_capture.Stop();
	SetRunning(false);

	CHARFORMAT2W cf;
	cf.cbSize = sizeof(CHARFORMAT2W);
	cf.dwMask = CFM_COLOR;
	cf.crTextColor = RGB(0, 128, 0);

	long endPos = m_richLog.GetTextLength();
	m_richLog.SetSel(endPos, endPos);
	m_richLog.SetSelectionCharFormat(cf);

	if (bExport && dwExit == 0 && m_bPackKeyolo)
	{
		m_bPackKeyolo = false;
		bool bPackOk = StartKeyoloPack();
		SetStatus(bPackOk ? _T("打包完成") : _T("打包失败"));
		return 0;
	}

	CStringW tip;
	if (bExport)
		tip.Format(dwExit == 0 ? L"\r\n==== 导出完成 ====\r\n"
			: L"\r\n==== 导出结束，退出码 %lu ====\r\n", dwExit);
	else
		tip.Format(dwExit == 0 ? L"\r\n==== 训练完成 ====\r\n"
			: L"\r\n==== 训练结束，退出码 %lu ====\r\n", dwExit);

	m_richLog.ReplaceSel(tip);
	m_richLog.LineScroll(m_richLog.GetLineCount() - 1);

	if (bExport)
		SetStatus(dwExit == 0 ? _T("导出完成") : _T("导出失败"));
	else
		SetStatus(dwExit == 0 ? _T("训练完成") : _T("训练失败"));

	if (!bExport && dwExit == 0)
	{
		RefreshWeightCombos();
		UpPt();
	}
	if (!bExport && dwExit == 0 && IsDlgButtonChecked(IDC_CHK_EXIT) == BST_CHECKED)
	{
		const int nShutdownDelay = 60;
		AppendLog(_T("==== 训练完成，60 秒后自动关机（如需取消请在命令行执行 shutdown /a）====\r\n"));
		if (!ShutdownComputer(nShutdownDelay))
			AppendLog(_T("自动关机失败：当前账户无关机权限或被系统策略阻止。\r\n"));
	}

	return 0;
}


HBRUSH CPageTrain::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
	if (pWnd->GetDlgCtrlID() == IDC_EDIT_LOG_TRAIN)
	{
		pDC->SetTextColor(RGB(0x1a, 0x1a, 0x1a));
		pDC->SetBkMode(TRANSPARENT);
		static CBrush brushBlack(RGB(0xff, 0xff, 0xff));
		return brushBlack;
	}

	return hbr;
}


void CPageTrain::OnCbnSelchangeComboExportFmt2()
{
	
}
