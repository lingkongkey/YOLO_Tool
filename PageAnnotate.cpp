
#include "MyIniPuls.h"
#include "pch.h"
#include "framework.h"
#include "YOLO_Tool.h"
#include "PageAnnotate.h"
#include "GreenEnv.h"
#include "UiUtil.h"
#include "AppConfig.h"
#include "afxdialogex.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CPageAnnotate::CPageAnnotate(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_PAGE_ANNOTATE, pParent)
	, m_nCurImage(-1)
{
}

BEGIN_MESSAGE_MAP(CPageAnnotate, CDialogEx)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BTN_SAVE_CLASSES, &CPageAnnotate::OnBnClickedBtnSaveClasses)
	ON_BN_CLICKED(IDC_BTN_ANNOTATE, &CPageAnnotate::OnBnClickedBtnAnnotate)
	ON_BN_CLICKED(IDC_BTN_AI_ANNOTATE, &CPageAnnotate::OnBnClickedBtnAiAnnotate)
	ON_CBN_SELCHANGE(IDC_COMBO_AI_PROVIDER, &CPageAnnotate::OnCbnSelchangeComboAiProvider)
	ON_BN_CLICKED(IDC_BTN_LABELIMG, &CPageAnnotate::OnBnClickedBtnLabelImg)
	ON_BN_CLICKED(IDC_BTN_PREV, &CPageAnnotate::OnBnClickedBtnPrev)
	ON_BN_CLICKED(IDC_BTN_NEXT, &CPageAnnotate::OnBnClickedBtnNext)
	ON_BN_CLICKED(IDC_BTN_REFRESH, &CPageAnnotate::OnBnClickedBtnRefresh)
	ON_BN_CLICKED(IDC_BTN_ZOOM_IN, &CPageAnnotate::OnBnClickedBtnZoomIn)
	ON_BN_CLICKED(IDC_BTN_ZOOM_OUT, &CPageAnnotate::OnBnClickedBtnZoomOut)
	ON_BN_CLICKED(IDC_BTN_ZOOM_FIT, &CPageAnnotate::OnBnClickedBtnZoomFit)
	ON_BN_CLICKED(IDC_BTN_SAVE_LABELS, &CPageAnnotate::OnBnClickedBtnSaveLabels)
	ON_BN_CLICKED(IDC_BTN_DEL_BOX, &CPageAnnotate::OnBnClickedBtnDelBox)
	ON_LBN_SELCHANGE(IDC_LIST_IMAGES, &CPageAnnotate::OnLbnSelchangeListImages)
	ON_CBN_SELCHANGE(IDC_COMBO_CLASSES, &CPageAnnotate::OnCbnSelchangeComboClasses)
	ON_MESSAGE(WM_ANNO_CHANGED, &CPageAnnotate::OnAnnoChanged)
	ON_MESSAGE(WM_USER_RECVLOG, &CPageAnnotate::OnRecvLog)
	ON_MESSAGE(WM_USER_PROC_EXIT, &CPageAnnotate::OnProcExit)

	ON_WM_HELPINFO()
END_MESSAGE_MAP()

static bool ReadUtf8File(const CString& strPath, CString& strText)
{
	CFile file;
	if (!file.Open(strPath, CFile::modeRead | CFile::typeBinary))
		return false;

	ULONGLONG nLen = file.GetLength();
	if (nLen > 1024 * 1024)
		nLen = 1024 * 1024;

	CStringA strA;
	file.Read(strA.GetBuffer((int)nLen), (UINT)nLen);
	strA.ReleaseBuffer((int)nLen);
	file.Close();

	LPCSTR psz = strA;
	if (strA.GetLength() >= 3 && (BYTE)psz[0] == 0xEF && (BYTE)psz[1] == 0xBB && (BYTE)psz[2] == 0xBF)
		psz += 3;

	int nWide = ::MultiByteToWideChar(CP_UTF8, 0, psz, -1, NULL, 0);
	if (nWide <= 0)
	{
		strText = CString(psz);
		return true;
	}
	::MultiByteToWideChar(CP_UTF8, 0, psz, -1, strText.GetBuffer(nWide), nWide);
	strText.ReleaseBuffer(nWide - 1);
	return true;
}

static bool WriteUtf8File(const CString& strPath, const CString& strText)
{
	CStringA strA;
	int nLen = ::WideCharToMultiByte(CP_UTF8, 0, strText, -1, NULL, 0, NULL, NULL);
	::WideCharToMultiByte(CP_UTF8, 0, strText, -1, strA.GetBuffer(nLen), nLen, NULL, NULL);
	strA.ReleaseBuffer(nLen - 1);

	CFile file;
	if (!file.Open(strPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		return false;
	file.Write(strA, (UINT)strA.GetLength());
	file.Close();
	return true;
}


struct AiPreset { LPCTSTR szName; LPCTSTR szBase; LPCTSTR szModel; };
static const AiPreset g_aiPresets[] =
{
	{ _T("豆包(火山引擎)"), _T("https://ark.cn-beijing.volces.com/api/v3"),           _T("Doubao - Seed - Evolving") },
	{ _T("通义千问(阿里)"), _T("https://dashscope.aliyuncs.com/compatible-mode/v1"),  _T("qwen-vl-max") },
	{ _T("智谱GLM"),        _T("https://open.bigmodel.cn/api/paas/v4"),               _T("glm-4v-flash") },
	{ _T("OpenAI"),         _T("https://api.openai.com/v1"),                          _T("gpt-4o-mini") },
};
static const int g_aiPresetCount = (int)(sizeof(g_aiPresets) / sizeof(g_aiPresets[0]));

BOOL CPageAnnotate::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CAppConfig& cfg = CAppConfig::Instance();

	FillWeightsCombo();

	CComboBox* pProvider = (CComboBox*)GetDlgItem(IDC_COMBO_AI_PROVIDER);
	if (pProvider != NULL)
	{
		for (int i = 0; i < g_aiPresetCount; i++)
			pProvider->AddString(g_aiPresets[i].szName);

		int nSel = 0;
		if (!cfg.strAiProvider.IsEmpty())
		{
			for (int i = 0; i < g_aiPresetCount; i++)
			{
				if (cfg.strAiProvider == g_aiPresets[i].szName)
				{
					nSel = i;
					break;
				}
			}
		}
		pProvider->SetCurSel(nSel);

		CString strModel = cfg.strAiModel;
		if (strModel.IsEmpty())
			strModel = g_aiPresets[nSel].szModel;
		SetDlgItemText(IDC_EDIT_AI_MODEL, strModel);
	}

	{
		CComboBox* pW = (CComboBox*)GetDlgItem(IDC_COMBO_WEIGHTS_ANN);
		if (pW != NULL)
		{

			for (INT_PTR i = 0; i < pW->GetCount(); i++)
			{
				CString strItem;
				pW->GetLBText(i, strItem);
				if (strItem.Find(cfg.strTrainWeights) >= 0 || cfg.strTrainWeights.Find(strItem) >= 0)
				{
					pW->SetCurSel((int)i);
					break;
				}
			}
		}
	}

	CString strDataset = GetDatasetDir();
	CString strClasses;
	if (!strDataset.IsEmpty() && ReadUtf8File(strDataset + _T("\\classes.txt"), strClasses))
	{
		strClasses.Replace(_T("\r\n"), _T("\n"));
		strClasses.Replace(_T("\n"), _T("\r\n"));
	}
	else
	{
		strClasses = _T("normal\r\nabnormal");
	}
	SetDlgItemText(IDC_EDIT_CLASSES, strClasses);

	m_canvas.Create(this, IDC_ANNO_CANVAS);

	FillClassCombo();
	FillImageList();
	Layout();
	UpdateAnnoStatus();
	return TRUE;
}


void CPageAnnotate::OnOK()
{
}

void CPageAnnotate::OnCancel()
{
}

void CPageAnnotate::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	Layout();
}

BOOL CPageAnnotate::PreTranslateMessage(MSG* pMsg)
{

	if (UiForwardEditPaste(pMsg))
		return TRUE;

	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN &&
		!UiIsMultilineEdit(pMsg->hwnd) &&
		!UiIsEditControl(pMsg->hwnd) &&
		(pMsg->lParam & 0x40000000) == 0)
	{
		SaveAndNext();
		return TRUE;
	}

	if (UiSwallowDialogKey(pMsg))
		return TRUE;

	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == _T('S') &&
		(::GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
		(pMsg->lParam & 0x40000000) == 0)
	{
		SaveAndNext();
		return TRUE;
	}


	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == _T('Z') &&
		(::GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
		(pMsg->lParam & 0x40000000) == 0 &&
		!UiIsEditControl(pMsg->hwnd))
	{
		if (m_canvas.Undo())
			SetStatus(_T("Undo: no step"));
		else
			SetStatus(_T("娌℃湁鍙挙閿€鐨"));
		return TRUE;
	}

	if (pMsg->message == WM_KEYDOWN &&
		pMsg->wParam >= _T('0') && pMsg->wParam <= _T('9') &&
		(::GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
		(::GetKeyState(VK_MENU) & 0x8000) == 0 &&
		(::GetKeyState(VK_SHIFT) & 0x8000) == 0 &&
		!UiIsEditControl(pMsg->hwnd))
	{
		int nClass = (int)(pMsg->wParam - _T('0'));
		CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_CLASSES);
		if (pCombo != NULL && pCombo->GetCount() > 0)
		{

			if (nClass >= 0 && nClass < pCombo->GetCount())
			{
				pCombo->SetCurSel(nClass);
				m_canvas.SetClassIndex(nClass);
				CString strName;
				pCombo->GetLBText(nClass, strName);
				CString strTip;
				SetStatus(strTip);
			}
			else
			{
				int nMax = min(9, (int)pCombo->GetCount() - 1);
				CString strTip;
				SetStatus(strTip);
			}
			return TRUE;
		}
	}

	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == _T('A') &&
		(::GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
		(pMsg->lParam & 0x40000000) == 0)
	{
		CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
		if (pList != NULL && ::GetFocus() == pList->GetSafeHwnd())
		{
			int nCount = pList->GetCount();
			for (int i = 0; i < nCount; i++)
				pList->SetSel(i, TRUE);
			UpdateAnnoStatus();
			return TRUE;
		}
	}


	if (pMsg->message == WM_RBUTTONUP)
	{
		CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
		if (pList != NULL && pMsg->hwnd == pList->GetSafeHwnd())
		{
			CPoint pt((short)LOWORD(pMsg->lParam), (short)HIWORD(pMsg->lParam));
			BOOL bOutside = FALSE;
			int nIndex = pList->ItemFromPoint(pt, bOutside);
			if (!bOutside && nIndex >= 0 && nIndex < pList->GetCount())
			{
				int nSelCount = pList->GetSelCount();
				BOOL bInSelection = (nSelCount != LB_ERR && nSelCount > 1 && pList->GetSel(nIndex) > 0);
				if (!bInSelection)
				{
					pList->SetCurSel(nIndex);
					if (nIndex != m_nCurImage)
						LoadImageAt(nIndex);
				}
				else if (nIndex != m_nCurImage)
				{
					LoadImageAt(nIndex, true);
				}
			}

			CPoint ptScreen = pt;
			pList->ClientToScreen(&ptScreen);
			ShowListMenu(ptScreen.x, ptScreen.y);
			return TRUE;
		}
	}


	if (pMsg->message == WM_CONTEXTMENU)
	{
		CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
		if (pList != NULL && pMsg->hwnd == pList->GetSafeHwnd())
		{
			int x = 0, y = 0;
			if (pMsg->lParam == -1)
			{
				CRect rc;
				pList->GetWindowRect(&rc);
				x = rc.right;
				y = rc.top;
			}
			else
			{
				x = (short)LOWORD(pMsg->lParam);
				y = (short)HIWORD(pMsg->lParam);
			}

			POINT pt = { x, y };
			pList->ScreenToClient(&pt);
			BOOL bOutside = FALSE;
			int nIndex = pList->ItemFromPoint(CPoint(pt), bOutside);
			if (nIndex >= 0 && nIndex < pList->GetCount())
			{
				int nSelCount = pList->GetSelCount();
				BOOL bInSelection = (nSelCount != LB_ERR && nSelCount > 1 && pList->GetSel(nIndex) > 0);
				if (!bInSelection)
				{
					pList->SetCurSel(nIndex);
					if (nIndex != m_nCurImage)
						LoadImageAt(nIndex);
				}
				else if (nIndex != m_nCurImage)
				{
					LoadImageAt(nIndex, true);
				}
			}
			ShowListMenu(x, y);
			return TRUE;
		}
	}

	if (pMsg->message == WM_MOUSEWHEEL && ::IsWindow(m_canvas.GetSafeHwnd()))
	{
		CRect rc;
		m_canvas.GetWindowRect(&rc);
		CPoint pt;
		::GetCursorPos(&pt);
		if (rc.PtInRect(pt))
		{
			m_canvas.SendMessage(WM_MOUSEWHEEL, pMsg->wParam, pMsg->lParam);
			return TRUE;
		}
	}
	return CDialogEx::PreTranslateMessage(pMsg);
}

void CPageAnnotate::Layout()
{
	if (!::IsWindow(GetSafeHwnd()))
		return;

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

	const int M = 8, GAP = 8, LEFTW = 160, RIGHTW = 200, BARH = 22, TOOLH = 32;
	const int AIH = 26;
	int barY = H - M - BARH;
	int colH = barY - GAP - M;
	int rightX = W - M - RIGHTW;
	int centerX = M + LEFTW + GAP;
	int centerW = rightX - GAP - centerX;
	if (centerW < 240)
		centerW = 240;

	mv(IDC_GRP_IMAGES, M, M, LEFTW, colH);
	mv(IDC_LIST_IMAGES, M + 8, M + 20, LEFTW - 16, colH - 20 - 8 - 30 - 8);
	int bw = (LEFTW - 16 - 12) / 3;
	int by = M + colH - 8 - 26;
	mv(IDC_BTN_PREV, M + 8, by, bw, 26);
	mv(IDC_BTN_NEXT, M + 14 + bw, by, bw, 26);
	mv(IDC_BTN_REFRESH, M + 20 + bw * 2, by, bw, 26);

	mv(IDC_LBL_AI_KEY, centerX, M + 5, 40, 14);
	mv(IDC_EDIT_AI_KEY, centerX + 42, M, 280, 22);
	mv(IDC_COMBO_AI_PROVIDER, centerX + 248 + 80, M, 112, 200);
	mv(IDC_LBL_AI_MODEL, centerX + 368 + 80, M + 5, 32, 14);
	mv(IDC_EDIT_AI_MODEL, centerX + 400 + 80, M, 200, 22);
	mv(IDC_BTN_AI_ANNOTATE, centerX + 606 + 80, M, 88, 22);


	mv(IDC_BTN_ZOOM_IN, centerX, M + AIH, 60, 28);
	mv(IDC_BTN_ZOOM_OUT, centerX + 66, M + AIH, 60, 28);
	mv(IDC_BTN_ZOOM_FIT, centerX + 132, M + AIH, 70, 28);
	mv(IDC_BTN_SAVE_LABELS, centerX + 208, M + AIH, 80, 28);
	mv(IDC_BTN_DEL_BOX, centerX + 294, M + AIH, 70, 28);
	mv(IDC_ANNO_CANVAS, centerX, M + AIH + TOOLH, centerW, colH - AIH - TOOLH);


	mv(IDC_GRP_CLASSES, rightX, M, RIGHTW, 260);
	mv(IDC_EDIT_CLASSES, rightX + 10, M + 20, RIGHTW - 20, 178);
	mv(IDC_BTN_SAVE_CLASSES, rightX + 10, M + 200, RIGHTW - 120, 26);
	mv(IDC_LBL_CLASS_CURRENT, rightX + 10, M + 234, 58, 14);
	mv(IDC_COMBO_CLASSES, rightX + 72, M + 230, RIGHTW - 82, 150);

	int py = M + 255 + GAP;
	mv(IDC_GRP_ANNOTATE, rightX, py, RIGHTW, 176);
	mv(IDC_LBL_WEIGHTS_ANN, rightX + 10, py + 24, 44, 14);
	mv(IDC_COMBO_WEIGHTS_ANN, rightX + 10, py + 42, RIGHTW - 20, 160);
	mv(IDC_LBL_CONF, rightX + 10, py + 78, 44, 14);
	mv(IDC_EDIT_CONF, rightX + 62, py + 76, 64, 21);
	mv(IDC_BTN_ANNOTATE, rightX + 10, py + 108, RIGHTW - 20, 26);
	mv(IDC_BTN_LABELIMG, rightX + 10, py + 140, RIGHTW - 20, 26);


	int ly = py + 176 + GAP;
	int lh = (M + colH) - ly;
	if (lh < 60)
		lh = 60;
	mv(IDC_GRP_LOG_ANN, rightX, ly, RIGHTW, lh);
	mv(IDC_EDIT_LOG_ANN, rightX + 8, ly + 20, RIGHTW - 16, lh - 28);


	mv(IDC_STATIC_ANN_STATUS, M, barY, W - M * 2, BARH);
}

void CPageAnnotate::FillImageList()
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
	if (pList == NULL)
		return;

	pList->ResetContent();
	m_arrImages.RemoveAll();
	m_nCurImage = -1;

	CString strDir = GetDatasetDir() + _T("\\images\\train");
	const TCHAR* szExts[] = { _T("*.jpg"), _T("*.jpeg"), _T("*.png"), _T("*.bmp") };
	for (int k = 0; k < 4; k++)
	{
		CFileFind finder;
		BOOL bMore = finder.FindFile(strDir + _T("\\") + szExts[k]);
		while (bMore)
		{
			bMore = finder.FindNextFile();
			if (finder.IsDirectory())
				continue;
			m_arrImages.Add(finder.GetFilePath());
			pList->AddString(finder.GetFileName());
		}
		finder.Close();
	}

	if (pList->GetCount() > 0)
		LoadImageAt(0);
}

void CPageAnnotate::ShowListMenu(int x, int y)
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
	if (pList == NULL)
		return;

	int nCur = pList->GetCurSel();
	if (nCur < 0 || nCur >= m_arrImages.GetSize())
		return;

	CStringArray arrSel;
	int nItems = pList->GetCount();
	if (nItems > (int)m_arrImages.GetSize())
		nItems = (int)m_arrImages.GetSize();
	for (int i = 0; i < nItems; i++)
	{
		if (pList->GetSel(i) > 0)
			arrSel.Add(m_arrImages[i]);
	}
	if (arrSel.GetSize() == 0)
		arrSel.Add(m_arrImages[nCur]);
	int nCount = (int)arrSel.GetSize();

	CMenu menu;
	menu.CreatePopupMenu();
	CString strDelItem;
	if (nCount > 1)
		strDelItem.Format(_T("鍒犻櫎閫変腑鐨?%d 寮犲浘鐗囷紙鍚爣绛撅級"), nCount);
	else
		strDelItem = _T("鍒犻櫎鍥剧墖锛堝惈鏍囩锛");
	menu.AppendMenu(MF_STRING, 1, strDelItem);
	menu.AppendMenu(MF_STRING, 2, _T("鎵撳紑鎵€鍦ㄦ枃浠跺す"));

	int nCmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
		x, y, this);
	if (nCmd == 1)
	{

		CString strList;
		for (int i = 0; i < nCount && i < 5; i++)
		{
			TCHAR szName[MAX_PATH] = { 0 };
			_tcsncpy_s(szName, arrSel[i], _TRUNCATE);
			::PathStripPath(szName);
			strList += _T("  ") + CString(szName) + _T("\r\n");
		}
		if (nCount > 5)
		{
			CString strMore;
			strMore.Format(_T("  ... 鍏?%d 寮燶r\n"), nCount);
			strList += strMore;
		}

		CString strMsg;
		if (nCount > 1)
			strMsg.Format(_T("纭畾鍒犻櫎閫変腑鐨?%d 寮犲浘鐗囧悧锛焅r\n瀵瑰簲鏍囩涔熶細涓€骞跺垹闄ゃ€俓r\n\r\n%s"), nCount, (LPCTSTR)strList);
		else
			strMsg.Format(_T("纭畾鍒犻櫎杩欏紶鍥剧墖鍚楋紵\r\n瀵瑰簲鏍囩涔熶細涓€骞跺垹闄ゃ€俓r\n\r\n%s"), (LPCTSTR)strList);
		if (AfxMessageBox(strMsg, MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;


		if (m_nCurImage >= 0 && m_nCurImage < m_arrImages.GetSize())
		{
			CString strCurImg = m_arrImages[m_nCurImage];
			for (int i = 0; i < nCount; i++)
			{
				if (arrSel[i].CompareNoCase(strCurImg) == 0)
				{
					m_canvas.Clear();
					m_nCurImage = -1;
					break;
				}
			}
		}


		int nDelImg = 0, nDelLbl = 0, nFail = 0;
		for (int i = 0; i < nCount; i++)
		{
			CString strImg = arrSel[i];
			CString strLabel = GetLabelPathFor(strImg);

			::SetFileAttributes(strImg, FILE_ATTRIBUTE_NORMAL);
			if (!::DeleteFile(strImg))
			{
				nFail++;
				continue;
			}
			nDelImg++;

			if (::PathFileExists(strLabel))
			{
				::SetFileAttributes(strLabel, FILE_ATTRIBUTE_NORMAL);
				if (::DeleteFile(strLabel))
					nDelLbl++;
			}
		}

		CString strLog;
		if (nCount > 1)
			strLog.Format(_T("宸叉壒閲忓垹闄?%d 寮犲浘鐗囥€?d 涓爣绛炬枃浠禱r\n"), nDelImg, nDelLbl);
		else
			strLog.Format(_T("宸插垹闄? %s\r\n"), (LPCTSTR)arrSel[0]);
		if (nFail > 0)
		{
			CString strFail;
			strFail.Format(_T("鏈?%d 涓枃浠跺垹闄ゅけ璐ワ紙鍙兘琚崰鐢級\r\n"), nFail);
			strLog += strFail;
			AfxMessageBox(strLog, MB_ICONWARNING);
		}
		AppendLog(strLog);

		FillImageList();
		UpdateAnnoStatus();
	}
	else if (nCmd == 2)
	{
		CString strArg;
		strArg.Format(_T("/select,\"%s\""), (LPCTSTR)m_arrImages[nCur]);
		::ShellExecute(NULL, _T("open"), _T("explorer.exe"), strArg, NULL, SW_SHOWNORMAL);
	}
}

CString CPageAnnotate::GetLabelPathFor(LPCTSTR lpszImage)
{
	CString strDataset = GetDatasetDir();
	CString strLabelDir = strDataset + _T("\\labels\\train");
	::SHCreateDirectoryEx(NULL, strLabelDir, NULL);

	TCHAR szName[MAX_PATH] = { 0 };
	_tcsncpy_s(szName, lpszImage, _TRUNCATE);
	::PathStripPath(szName);
	::PathRemoveExtension(szName);
	return strLabelDir + _T("\\") + szName + _T(".txt");
}

bool CPageAnnotate::LoadImageAt(int nIndex, bool bKeepListSelection)
{
	if (nIndex < 0 || nIndex >= m_arrImages.GetSize())
		return false;

	if (m_canvas.HasImage() && m_canvas.IsModified())
	{
		if (AfxMessageBox(_T("褰撳墠鍥惧儚鐨勬爣娉ㄥ皻鏈繚瀛橈紝鏄惁淇濆瓨锛", MB_YESNO | MB_ICONQUESTION)) == IDYES)
		{
			if (!SaveCurrentLabels())
				return false;
		}
	}

	m_nCurImage = nIndex;
	CString strImg = m_arrImages[nIndex];
	m_canvas.LoadImage(strImg, GetLabelPathFor(strImg));

	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
	if (pList != NULL)
	{
		if (bKeepListSelection)
			pList->SetCaretIndex(nIndex);
		else
			pList->SetCurSel(nIndex);

		UpdateAnnoStatus();
		return true;
	}
}

int CPageAnnotate::GetSelectedCount()
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
	if (pList == NULL)
		return 0;
	int n = pList->GetSelCount();
	return (n == LB_ERR) ? 0 : n;
}


bool CPageAnnotate::HasMultiSelection()
{
	return GetSelectedCount() >= 2;
}

bool CPageAnnotate::SaveCurrentLabels()
{
	if (!m_canvas.HasImage())
		return false;

	CString strLabel = GetLabelPathFor(m_canvas.GetImagePath());
	if (!m_canvas.SaveLabels(strLabel))
	{
		AppendLog(_T("鏍囩淇濆瓨澶辫触: ") + strLabel + _T("\r\n"));
		return false;
	}
	AppendLog(_T("宸蹭繚瀛樻爣娉? ") + strLabel + _T("\r\n"));
	UpdateAnnoStatus();
	return true;
}

bool CPageAnnotate::SaveAndNext()
{
	if (!m_canvas.HasImage())
	{
		SetStatus(_T("鏈姞杞藉浘鍍忥紝Ctrl+S 鏃犳搷浣"));
		return false;
	}

	if (m_canvas.GetBoxCount() <= 0)
	{
		AppendLog(_T("Ctrl+S 鏈繚瀛橈細褰撳墠鍥剧墖娌℃湁鏍囨敞妗嗭紝鍋滃湪鍘熷浘\r\n"));
		SetStatus(_T("褰撳墠鍥剧墖娌℃湁鏍囨敞妗嗭紝璇峰厛鐢绘鍐嶆寜 Ctrl+S"));
		return false;
	}

	if (!SaveCurrentLabels())
		return false;

	int nNext = m_nCurImage + 1;
	if (nNext < (int)m_arrImages.GetSize())
	{
		LoadImageAt(nNext);
		AppendLog(_T("Ctrl+S 宸蹭繚瀛樺苟璺冲埌涓嬩竴寮燶r\n"));
	}
	else
	{
		SetStatus(_T("Already last image, labels saved"));
		AppendLog(_T("Ctrl+S 宸蹭繚瀛橈紙宸叉槸鏈€鍚庝竴寮狅級\r\n"));
	}
	return true;
}

void CPageAnnotate::UpdateAnnoStatus()
{
	CString str;
	if (m_canvas.HasImage())
	{
		TCHAR szName[MAX_PATH] = { 0 };
		_tcsncpy_s(szName, m_canvas.GetImagePath(), _TRUNCATE);
		::PathStripPath(szName);
		str.Format(_T("鏂囦欢: %s   缂╂斁: %d%%   妗? %d   |   0-9 鍒囩被鍒紝绌虹櫧澶勬嫋鎷界敾妗嗭紝妗嗗唴鎸変綇鎷栧姩绉诲姩锛屾嫋瑙掔偣鏀瑰ぇ灏忥紝Ctrl+Z 鎾ら攢锛孌elete 鍒犻櫎妗嗭紝Ctrl+S / 鍥炶溅 淇濆瓨骞惰烦涓嬩竴寮"),
			szName, m_canvas.GetZoomPercent(), m_canvas.GetBoxCount());
	}
	else
	{
		str = _T("鏈姞杞藉浘鍍忥紝璇峰湪宸︿晶鍒楄〃涓€夋嫨涓€寮");
	}

	int nMulti = GetSelectedCount();
	if (nMulti >= 2)
	{
		CString strMulti;
		strMulti.Format(_T("   |   %d images selected (Ctrl/Shift multi-select, right-click batch delete)"), nMulti);
		str += strMulti;
	}
	SetStatus(str);
}



void CPageAnnotate::OnBnClickedBtnPrev()
{
	LoadImageAt(m_nCurImage - 1);
}

void CPageAnnotate::OnBnClickedBtnNext()
{
	LoadImageAt(m_nCurImage + 1);
}

void CPageAnnotate::OnBnClickedBtnRefresh()
{
	FillImageList();
}

void CPageAnnotate::OnBnClickedBtnZoomIn()
{
	m_canvas.ZoomIn();
	UpdateAnnoStatus();
}

void CPageAnnotate::OnBnClickedBtnZoomOut()
{
	m_canvas.ZoomOut();
	UpdateAnnoStatus();
}

void CPageAnnotate::OnBnClickedBtnZoomFit()
{
	m_canvas.FitToWindow();
	m_canvas.Invalidate();
	UpdateAnnoStatus();
}

void CPageAnnotate::OnBnClickedBtnSaveLabels()
{
	SaveCurrentLabels();
}

void CPageAnnotate::OnBnClickedBtnDelBox()
{
	m_canvas.DeleteSelected();
}

void CPageAnnotate::OnLbnSelchangeListImages()
{
	CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST_IMAGES);
	if (pList == NULL)
		return;
	int nSel = pList->GetCurSel();
	if (nSel < 0)
		return;

	if (HasMultiSelection())
	{
		if (nSel != m_nCurImage)
			LoadImageAt(nSel, true);
		else
			UpdateAnnoStatus();
	}
	else if (nSel != m_nCurImage)
	{
		LoadImageAt(nSel);
	}
}

void CPageAnnotate::OnCbnSelchangeComboClasses()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_CLASSES);
	if (pCombo != NULL && pCombo->GetCurSel() >= 0)
	{
		int nSel = pCombo->GetCurSel();
		m_canvas.SetClassIndex(nSel);
		CString strName;
		pCombo->GetLBText(nSel, strName);
		CString strTip;
		strTip.Format(_T("褰撳墠绫诲埆 %s"), (LPCTSTR)strName);
		SetStatus(strTip);
	}
}

LRESULT CPageAnnotate::OnAnnoChanged(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	UpdateAnnoStatus();
	return 0;
}


void CPageAnnotate::FillWeightsCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_WEIGHTS_ANN);
	if (pCombo == NULL)
		return;

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
			{
				if (pCombo->FindStringExact(-1, finder.GetFileName()) < 0)
					pCombo->AddString(finder.GetFileName());
			}
		}
		finder.Close();
	}

	if (pCombo->GetCount() == 0)
	{
		pCombo->AddString(_T("yolo11n.pt"));
		pCombo->AddString(_T("yolo11s.pt"));
		pCombo->AddString(_T("yolov8n.pt"));
		pCombo->AddString(_T("yolov8s.pt"));
	}

	int nSel = pCombo->FindStringExact(-1, _T("yolo11n.pt"));
	pCombo->SetCurSel(nSel >= 0 ? nSel : 0);
}

void CPageAnnotate::FillClassCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_CLASSES);
	if (pCombo == NULL)
		return;

	CStringArray arrNames;
	ParseClassNames(arrNames);

	pCombo->ResetContent();
	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
	{

		CString strItem;
		if (i < 10)
			strItem.Format(_T("%d %s"), (int)i, (LPCTSTR)arrNames[i]);
		else
			strItem = _T("    ") + arrNames[i];
		pCombo->AddString(strItem);
	}
	if (pCombo->GetCount() > 0)
	{
		pCombo->SetCurSel(0);
		m_canvas.SetClassIndex(0);
	}
	m_canvas.SetClassNames(arrNames);
}

bool CPageAnnotate::ParseClassNames(CStringArray& arrNames)
{
	CString strText;
	GetDlgItemText(IDC_EDIT_CLASSES, strText);

	arrNames.RemoveAll();
	int nPos = 0;
	CString strLine = strText.Tokenize(_T("\r\n"), nPos);
	while (!strLine.IsEmpty())
	{
		strLine.Trim();
		if (!strLine.IsEmpty())
			arrNames.Add(strLine);
		strLine = strText.Tokenize(_T("\r\n"), nPos);
	}
	return arrNames.GetSize() > 0;
}

bool CPageAnnotate::SaveClasses()
{
	CString strDataset = GetDatasetDir();
	if (strDataset.IsEmpty() || !::PathIsDirectory(strDataset))
	{
		AfxMessageBox(_T("鏈壘鍒伴」鐩暟鎹泦"), MB_ICONWARNING);
		return false;
	}

	CStringArray arrNames;
	if (!ParseClassNames(arrNames))
	{
		AfxMessageBox(_T("璇疯嚦灏戝～鍐欎竴涓被鍒紙姣忚涓€涓級銆"), MB_ICONWARNING);
		return false;
	}


	CString strClasses;
	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
		strClasses += arrNames[i] + _T("\n");
	WriteUtf8File(strDataset + _T("\\classes.txt"), strClasses);


	CString strYamlPath = strDataset + _T("\\data.yaml");
	CString strYaml;
	ReadUtf8File(strYamlPath, strYaml);

	CString strPath = _T("datasets/my_dataset");
	CString strTrain = _T("images/train");
	CString strVal = _T("images/val");


	if (!GetProjectRoot().IsEmpty())
	{
		strPath = strDataset;
		strPath.Replace(_T("\\"), _T("/"));
	}

	int nPos = 0;
	CString strLine = strYaml.Tokenize(_T("\r\n"), nPos);
	while (!strLine.IsEmpty())
	{
		CString s = strLine;
		s.Trim();
		int nComment = s.Find(_T('#'));
		if (nComment >= 0)
			s = s.Left(nComment);
		s.Trim();

		CString* pTarget = NULL;
		if (s.Left(5) == _T("path:"))
			pTarget = &strPath;
		else if (s.Left(6) == _T("train:"))
			pTarget = &strTrain;
		else if (s.Left(4) == _T("val:"))
			pTarget = &strVal;

		if (pTarget != NULL)
		{
			int nColon = s.Find(_T(':'));
			CString v = s.Mid(nColon + 1);
			v.Trim();
			if (!v.IsEmpty())
				*pTarget = v;
		}
		strLine = strYaml.Tokenize(_T("\r\n"), nPos);
	}

	CString strNew;
	strNew.Format(_T("path: %s\ntrain: %s\nval: %s\n\nnc: %d\nnames:\n"),
		(LPCTSTR)strPath, (LPCTSTR)strTrain, (LPCTSTR)strVal, (int)arrNames.GetSize());
	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
	{
		CString strEntry;
		strEntry.Format(_T("  %d: %s\n"), (int)i, (LPCTSTR)arrNames[i]);
		strNew += strEntry;
	}

	if (!WriteUtf8File(strYamlPath, strNew))
	{
		AfxMessageBox(_T("Failed to write data.yaml."), MB_ICONWARNING);
		return false;
	}
	return true;
}

void CPageAnnotate::OnBnClickedBtnSaveClasses()
{
	if (SaveClasses())
	{
		CStringArray arrNames;
		ParseClassNames(arrNames);
		CString strMsg;
		strMsg.Format(_T("宸蹭繚瀛?%d 涓被鍒埌 classes.txt 涓?data.yaml\r\n"), (int)arrNames.GetSize());
		AppendLog(strMsg);
		FillClassCombo();
		SetStatus(_T("Classes saved"));
	}
}


void CPageAnnotate::OnBnClickedBtnAnnotate()
{

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		AfxMessageBox(_T("鏈壘鍒?YOLO_Green 缁胯壊鍖呮垨 python.exe銆"), MB_ICONWARNING);
		return;
	}

	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_WEIGHTS_ANN);
	CString strWeights;
	int nSel = pCombo->GetCurSel();
	if (nSel >= 0)
		pCombo->GetLBText(nSel, strWeights);
	if (strWeights.IsEmpty())
	{
		AfxMessageBox(_T("Please select a pretrained weight file."), MB_ICONWARNING);
		return;
	}

	CString strConf;
	GetDlgItemText(IDC_EDIT_CONF, strConf);
	strConf.Trim();
	if (strConf.IsEmpty())
		strConf = _T("0.5");


	CString strWeightsPath = strWeights;
	if (strWeights.Find(_T('\\')) < 0 && strWeights.Find(_T('/')) < 0)
	{
		CString strProjW = GetProjectModelsDir() + _T("\\") + strWeights;
		if (::PathFileExists(strProjW))
			strWeightsPath = strProjW;
		else
			strWeightsPath = strGreen + _T("\\models\\") + strWeights;
	}

	CString strImgs = GetProjectImagesDir();
	if (strImgs.IsEmpty() || !::PathIsDirectory(strImgs))
	{
		AfxMessageBox(_T("鏈壘鍒拌缁冨浘鐗囩洰褰曪紝璇峰厛鍦ㄦ埅鍥鹃〉閫夋嫨椤圭洰鏍圭洰褰曘"), MB_ICONWARNING);
		return;
	}

	CString strScript = strGreen + _T("\\scripts\\auto_annotate.py");
	if (!::PathFileExists(strScript))
	{
		AfxMessageBox(_T("鏈壘鍒?scripts\\auto_annotate.py锛岃妫€鏌ョ豢鑹插寘鏄惁瀹屾暣銆"), MB_ICONWARNING);
		return;
	}

	CString strCmd;
	strCmd.Format(_T("\"%s\" -X utf8 -u \"%s\" --imgs \"%s\" --weights \"%s\" --conf %s"),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strImgs, (LPCTSTR)strWeightsPath, (LPCTSTR)strConf);

	AppendLog(_T("==== 寮€濮嬮鏍囨敞 ====\r\n") + strCmd + _T("\r\n"));

	if (!m_capture.StartProcess(strCmd, GetSafeHwnd()))
	{
		AppendLog(_T("鍒涘缓棰勬爣娉ㄨ繘绋嬪け璐ャ€俓r\n"));
		SetStatus(_T("Pre-label start failed"));
		return;
	}
	SetRunning(true);
	SetStatus(_T("棰勬爣娉ㄤ腑..."));
}

void CPageAnnotate::OnCbnSelchangeComboAiProvider()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_AI_PROVIDER);
	if (pCombo == NULL)
		return;
	int nSel = pCombo->GetCurSel();
	if (nSel >= 0 && nSel < g_aiPresetCount)
		SetDlgItemText(IDC_EDIT_AI_MODEL, g_aiPresets[nSel].szModel);
}

void CPageAnnotate::OnBnClickedBtnAiAnnotate()
{
	if (m_bAiRunning)
	{
		m_bAiStop = true;
		GetDlgItem(IDC_BTN_AI_ANNOTATE)->EnableWindow(FALSE);
		SetStatus(_T("AI 鏍囨敞灏嗗湪褰撳墠鍥剧墖澶勭悊瀹屽悗鍋滄..."));
		return;
	}

	CString strKey;
	GetDlgItemText(IDC_EDIT_AI_KEY, strKey);
	strKey.Trim();
	if (strKey.IsEmpty())
	{
		AfxMessageBox(_T("璇峰厛鍦ㄤ笂闈㈢殑 AI Key 杈撳叆妗嗕腑濉叆澶фā鍨?API Key銆"), MB_ICONINFORMATION);
		return;
	}

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		AfxMessageBox(_T("鏈壘鍒?YOLO_Green 缁胯壊鍖呮垨 python.exe銆"), MB_ICONWARNING);
		return;
	}
	CString strScript = strGreen + _T("\\scripts\\ai_annotate.py");
	if (!::PathFileExists(strScript))
	{
		AfxMessageBox(_T("鏈壘鍒?scripts\\ai_annotate.py锛岃妫€鏌ョ豢鑹插寘鏄惁瀹屾暣銆"), MB_ICONWARNING);
		return;
	}

	m_arrAiTodo.RemoveAll();
	for (INT_PTR i = 0; i < m_arrImages.GetSize(); i++)
	{
		if (!::PathFileExists(GetLabelPathFor(m_arrImages[i])))
			m_arrAiTodo.Add(m_arrImages[i]);
	}
	if (m_arrAiTodo.GetSize() == 0)
	{
		AfxMessageBox(_T("All images already have labels. Nothing for AI to annotate."), MB_ICONINFORMATION);
		return;
	}

	m_strAiKey = strKey;

	CComboBox* pProvider = (CComboBox*)GetDlgItem(IDC_COMBO_AI_PROVIDER);
	int nProvider = (pProvider != NULL) ? pProvider->GetCurSel() : 0;
	if (nProvider < 0 || nProvider >= g_aiPresetCount)
		nProvider = 0;
	m_strAiBase = g_aiPresets[nProvider].szBase;
	GetDlgItemText(IDC_EDIT_AI_MODEL, m_strAiModel);
	m_strAiModel.Trim();
	if (m_strAiModel.IsEmpty())
		m_strAiModel = g_aiPresets[nProvider].szModel;

	m_nAiIndex = 0;
	m_bAiStop = false;
	m_bAiClassesDirty = false;
	m_bAiRunning = true;
	SetAiRunningUI(true);

	CString strLog;
	strLog.Format(_T("==== 寮€濮?AI 鏍囨敞锛氬叡 %d 寮犳湭鏍囨敞鍥剧墖锛岄€愬紶澶勭悊 ====\r\n"),
		(int)m_arrAiTodo.GetSize());
	AppendLog(strLog);
	StartAiAnnotateImage();
}

void CPageAnnotate::StartAiAnnotateImage()
{
	CString strPython = GetGreenPython();
	CString strGreen = GetGreenRoot();
	CString strImg = m_arrAiTodo[m_nAiIndex];
	m_vecAiBoxes.clear();
	m_bAiGotResult = false;
	m_bAiError = false;

	CString strScript = strGreen + _T("\\scripts\\ai_annotate.py");
	CString strClasses = GetProjectDatasetDir() + _T("\\classes.txt");

	CString strCmd;
	strCmd.Format(_T("\"%s\" -X utf8 -u \"%s\" --key \"%s\" --img \"%s\" --classes-file \"%s\" --base-url \"%s\" --model \"%s\""),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)m_strAiKey,
		(LPCTSTR)strImg, (LPCTSTR)strClasses, (LPCTSTR)m_strAiBase, (LPCTSTR)m_strAiModel);

	CString strShow = strCmd;
	strShow.Replace(_T("--key \"") + m_strAiKey + _T("\""), _T("--key \"***\""));
	AppendLog(strShow + _T("\r\n"));

	if (!m_capture.StartProcess(strCmd, GetSafeHwnd()))
	{
		AppendLog(_T("鍒涘缓 AI 鏍囨敞杩涚▼澶辫触銆俓r\n"));
		m_bAiRunning = false;
		SetAiRunningUI(false);
		SetStatus(_T("AI 鏍囨敞鍚姩澶辫触"));
		return;
	}

	TCHAR szName[MAX_PATH] = { 0 };
	_tcsncpy_s(szName, strImg, _TRUNCATE);
	::PathStripPath(szName);
	CString strStatus;
	strStatus.Format(_T("AI 鏍囨敞涓?(%d/%d): %s"),
		m_nAiIndex + 1, (int)m_arrAiTodo.GetSize(), szName);
	SetStatus(strStatus);
}


void CPageAnnotate::HandleAiProcExit(DWORD dwExit)
{
	CString strImg = m_arrAiTodo[m_nAiIndex];
	TCHAR szName[MAX_PATH] = { 0 };
	_tcsncpy_s(szName, strImg, _TRUNCATE);
	::PathStripPath(szName);
	int nTotal = (int)m_arrAiTodo.GetSize();

	if (dwExit == 0 && m_bAiGotResult && !m_bAiError)
	{

		CString strContent;
		for (size_t i = 0; i < m_vecAiBoxes.size(); i++)
		{
			const AiBox& b = m_vecAiBoxes[i];
			int nCls = MergeAiClass(b.strName);
			CString strLine;
			strLine.Format(_T("%d %.6f %.6f %.6f %.6f\n"), nCls, b.cx, b.cy, b.w, b.h);
			strContent += strLine;
		}

		CString strLabel = GetLabelPathFor(strImg);
		if (!WriteUtf8File(strLabel, strContent))
			AppendLog(_T("鏍囩鍐欏叆澶辫触: ") + strLabel + _T("\r\n"));

		CString strLog;
		strLog.Format(_T("[AI %d/%d] %s -> %d 涓\r\n"),
			m_nAiIndex + 1, nTotal, szName, (int)m_vecAiBoxes.size());
		AppendLog(strLog);
	}
	else
	{
		CString strLog;
		strLog.Format(_T("[AI %d/%d] %s 澶辫触锛堥€€鍑虹爜 %lu锛夛紝宸茶烦杩囷紝涓嬫鍙噸璇昞r\n"),
			m_nAiIndex + 1, nTotal, szName, dwExit);
		AppendLog(strLog);
	}


	if (m_bAiClassesDirty)
	{
		m_bAiClassesDirty = false;
		if (SaveClasses())
			FillClassCombo();
	}

	if (m_canvas.HasImage() && m_canvas.GetImagePath().CompareNoCase(strImg) == 0)
		m_canvas.LoadImage(strImg, GetLabelPathFor(strImg));

	m_nAiIndex++;
	if (m_bAiStop)
	{
		AppendLog(_T("==== AI 鏍囨敞宸插仠姝?====\r\n"));
		m_bAiRunning = false;
		m_bAiStop = false;
		SetAiRunningUI(false);
		SetStatus(_T("AI labeling stopped"));
		UpdateAnnoStatus();
		return;
	}
	if (m_nAiIndex >= nTotal)
	{
		AppendLog(_T("==== AI 鏍囨敞鍏ㄩ儴瀹屾垚 ====\r\n"));
		m_bAiRunning = false;
		SetAiRunningUI(false);
		SetStatus(_T("AI 鏍囨敞瀹屾垚"));
		UpdateAnnoStatus();
		return;
	}
	StartAiAnnotateImage();
}

int CPageAnnotate::MergeAiClass(const CString& strAiName)
{
	CStringArray arrNames;
	ParseClassNames(arrNames);

	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
		if (arrNames[i] == strAiName)
			return (int)i;

	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
	{
		const CString strOld = arrNames[i];
		if (strOld.GetLength() >= 2 &&
			(strAiName.Find(strOld) >= 0 || strOld.Find(strAiName) >= 0))
		{
			arrNames[i] = strAiName;
			CString strNew;
			for (INT_PTR k = 0; k < arrNames.GetSize(); k++)
				strNew += arrNames[k] + _T("\r\n");
			SetDlgItemText(IDC_EDIT_CLASSES, strNew);
			m_bAiClassesDirty = true;
			AppendLog(_T("绫诲埆浠?AI 涓哄噯閲嶅懡鍚? ") + strOld + _T(" -> ") + strAiName + _T("\r\n"));
			return (int)i;
		}
	}

	CString strText;
	GetDlgItemText(IDC_EDIT_CLASSES, strText);
	strText.TrimRight(_T("\r\n "));
	if (!strText.IsEmpty())
		strText += _T("\r\n");
	strText += strAiName;
	SetDlgItemText(IDC_EDIT_CLASSES, strText);
	m_bAiClassesDirty = true;
	AppendLog(_T("AI 鏂板绫诲埆: ") + strAiName + _T("\r\n"));
	return (int)arrNames.GetSize();
}

void CPageAnnotate::SetAiRunningUI(bool bRun)
{
	GetDlgItem(IDC_BTN_ANNOTATE)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_BTN_SAVE_CLASSES)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_COMBO_WEIGHTS_ANN)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_CONF)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_AI_KEY)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_COMBO_AI_PROVIDER)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_AI_MODEL)->EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_BTN_LABELIMG)->EnableWindow(bRun ? FALSE : TRUE);
	m_canvas.EnableWindow(bRun ? FALSE : TRUE);
	GetDlgItem(IDC_BTN_AI_ANNOTATE)->EnableWindow(TRUE);
	SetDlgItemText(IDC_BTN_AI_ANNOTATE, bRun ? _T("鍋滄AI鏍囨敞") : _T("寮€濮婣I鏍囨敞"));
}

void CPageAnnotate::OnBnClickedBtnLabelImg()
{
	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		AfxMessageBox(_T("鏈壘鍒?YOLO_Green 缁胯壊鍖呮垨 python.exe銆"), MB_ICONWARNING);
		return;
	}


	CString strImgs = GetProjectImagesDir();
	CString strClasses = GetProjectDatasetDir() + _T("\\classes.txt");

	CString strCmd;
	if (!strImgs.IsEmpty() && ::PathFileExists(strClasses))
		strCmd.Format(_T("\"%s\" -m labelImg \"%s\" \"%s\""), (LPCTSTR)strPython, (LPCTSTR)strImgs, (LPCTSTR)strClasses);
	else if (!strImgs.IsEmpty())
		strCmd.Format(_T("\"%s\" -m labelImg \"%s\""), (LPCTSTR)strPython, (LPCTSTR)strImgs);
	else
		strCmd.Format(_T("\"%s\" -m labelImg"), (LPCTSTR)strPython);

	int nBufLen = strCmd.GetLength() + 1;
	LPTSTR pszCmd = new TCHAR[nBufLen];
	_tcscpy_s(pszCmd, nBufLen, strCmd);

	STARTUPINFO si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	BOOL bOk = ::CreateProcess(NULL, pszCmd, NULL, NULL, FALSE,
		CREATE_NO_WINDOW | DETACHED_PROCESS, NULL,
		(LPCTSTR)strGreen, &si, &pi);
	delete[] pszCmd;

	if (bOk)
	{
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
		AppendLog(_T("宸插惎鍔?labelImg\r\n"));
	}
	else
	{
		CString strErr;
		strErr.Format(_T("labelImg 鍚姩澶辫触 (閿欒 %lu\r\n"), ::GetLastError());
		AppendLog(strErr);
	}
}


LRESULT CPageAnnotate::OnRecvLog(WPARAM /*wParam*/, LPARAM lParam)
{
	CStringW* pMsg = (CStringW*)lParam;
	if (pMsg != NULL)
	{
		CString strLine(*pMsg);
		if (m_bAiRunning && strLine.Left(3) == _T("AI_"))
		{
			if (strLine.Left(8) == _T("AI_SIZE "))
			{
				m_bAiGotResult = true;
			}
			else if (strLine.Left(7) == _T("AI_BOX "))
			{
				LPCTSTR p = (LPCTSTR)strLine + 7;
				double vals[4];
				int nGot = 0;
				for (int k = 0; k < 4; k++)
				{
					while (*p == _T(' '))
						p++;
					TCHAR* pEnd = NULL;
					vals[k] = _tcstod(p, &pEnd);
					if (pEnd == p)
						break;
					p = pEnd;
					nGot++;
				}
				if (nGot == 4)
				{
					while (*p == _T(' '))
						p++;
					CString strName = p;
					strName.Trim();
					if (!strName.IsEmpty())
					{
						AiBox box;
						box.cx = vals[0]; box.cy = vals[1];
						box.w = vals[2];  box.h = vals[3];
						box.strName = strName;
						m_vecAiBoxes.push_back(box);
					}
				}
			}
			else if (strLine.Left(9) == _T("AI_ERROR "))
			{
				m_bAiError = true;
				AppendLog(strLine.Mid(9) + _T("\r\n"));
			}
			else
			{
				strLine += _T("\r\n");
				AppendLog(strLine);
			}
			delete pMsg;
		}
		return 0;
	}
}
LRESULT CPageAnnotate::OnProcExit(WPARAM wParam, LPARAM /*lParam*/)
{
	DWORD dwExit = (DWORD)wParam;

	m_capture.Stop();
	if (m_bAiRunning)
	{
		HandleAiProcExit(dwExit);
		return 0;
	}

	SetStatus(dwExit == 0 ? _T("棰勬爣娉ㄥ畬鎴") : _T(""));

	SetRunning(false);

	CString strMsg;
	strMsg.Format(_T("==== 棰勬爣娉ㄧ粨鏉?(閫€鍑虹爜 %lu ====\r\n"), dwExit);
	AppendLog(strMsg);

	if (dwExit == 0 && m_canvas.HasImage())
		m_canvas.LoadImage(m_canvas.GetImagePath(), GetLabelPathFor(m_canvas.GetImagePath()));

	UpdateAnnoStatus();
	return 0;
}

void CPageAnnotate::OnDestroy()
{

	m_capture.Stop();
	CAppConfig& cfg = CAppConfig::Instance();
	GetDlgItemText(IDC_EDIT_AI_KEY, cfg.strAiKey);
	CComboBox* pProvider = (CComboBox*)GetDlgItem(IDC_COMBO_AI_PROVIDER);
	if (pProvider != NULL)
	{
		int nSel = pProvider->GetCurSel();
		if (nSel >= 0 && nSel < g_aiPresetCount)
			cfg.strAiProvider = g_aiPresets[nSel].szName;
	}
	GetDlgItemText(IDC_EDIT_AI_MODEL, cfg.strAiModel);
	GetDlgItemText(IDC_EDIT_CONF, cfg.strAnnotateConf);

	CComboBox* pW = (CComboBox*)GetDlgItem(IDC_COMBO_WEIGHTS_ANN);
	if (pW != NULL)
	{
		CString strWeight;
		pW->GetWindowText(strWeight);
		cfg.strTrainWeights = strWeight;
	}

	CDialogEx::OnDestroy();
}

void CPageAnnotate::AppendLog(const CString& strText)
{
	CEdit* pEdit = (CEdit*)GetDlgItem(IDC_EDIT_LOG_ANN);
	if (pEdit == NULL)
		return;

	int nLen = pEdit->GetWindowTextLength();
	if (nLen > 60000)
	{
		pEdit->SetSel(0, nLen / 2);
		pEdit->ReplaceSel(_T(""));
		nLen = pEdit->GetWindowTextLength();
	}

	pEdit->SetSel(nLen, nLen);
	pEdit->ReplaceSel(strText);
}

void CPageAnnotate::SetStatus(LPCTSTR lpszText)
{
	SetDlgItemText(IDC_STATIC_ANN_STATUS, lpszText);
}

void CPageAnnotate::SetRunning(bool bRunning)
{
	GetDlgItem(IDC_BTN_ANNOTATE)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_BTN_SAVE_CLASSES)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_COMBO_WEIGHTS_ANN)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_CONF)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_AI_KEY)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_COMBO_AI_PROVIDER)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_EDIT_AI_MODEL)->EnableWindow(bRunning ? FALSE : TRUE);
	GetDlgItem(IDC_BTN_AI_ANNOTATE)->EnableWindow(bRunning ? FALSE : TRUE);
	m_canvas.EnableWindow(bRunning ? FALSE : TRUE);
}


BOOL CPageAnnotate::OnHelpInfo(HELPINFO* pHelpInfo)
{

	return 1;
}
