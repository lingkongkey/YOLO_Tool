
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
#include <thread>
#include <memory>
#pragma comment(lib, "shlwapi.lib")


extern std::vector<char> HttpPost(const std::wstring& wstrUrl, const char* pUtf8Body, DWORD dwBodyLen, const std::wstring& wstrAuthHeader, DWORD& dwCode);
extern std::string ImageToJpegBase64(LPCTSTR szPath, LONG nMaxLongSide , ULONG nQuality);
extern bool GetImageSize(LPCTSTR szPath, LONG& nWidth, LONG& nHeight);

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
	ON_MESSAGE(WM_USER_AI_RESULT, &CPageAnnotate::OnAiResult)

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

// ===== 轻量 JSON 解析与转换（仅用于解析大模型返回，无第三方依赖） =====
namespace
{

struct JsonValue
{
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool bValue = false;
	double dValue = 0.0;
	std::wstring str;
	std::vector<JsonValue> arr;
	std::vector<std::pair<std::wstring, JsonValue>> obj;

	const JsonValue* Get(const wchar_t* lpszKey) const
	{
		if (type != Object)
			return NULL;
		for (size_t i = 0; i < obj.size(); i++)
		{
			if (obj[i].first == lpszKey)
				return &obj[i].second;
		}
		return NULL;
	}
};

class JsonParser
{
public:
	explicit JsonParser(const std::wstring& strText) : m_strText(strText), m_nPos(0) {}

	bool Parse(JsonValue& out)
	{
		SkipSpace();
		if (!ParseValue(out, 0))
			return false;
		SkipSpace();
		return true;
	}

private:
	void SkipSpace()
	{
		while (m_nPos < m_strText.size())
		{
			wchar_t c = m_strText[m_nPos];
			if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')
				m_nPos++;
			else
				break;
		}
	}

	bool ReadHex4(unsigned int& out)
	{
		if (m_nPos + 4 > m_strText.size())
			return false;
		out = 0;
		for (int i = 0; i < 4; i++)
		{
			wchar_t c = m_strText[m_nPos++];
			unsigned int v;
			if (c >= L'0' && c <= L'9')
				v = (unsigned int)(c - L'0');
			else if (c >= L'a' && c <= L'f')
				v = (unsigned int)(c - L'a' + 10);
			else if (c >= L'A' && c <= L'F')
				v = (unsigned int)(c - L'A' + 10);
			else
				return false;
			out = out * 16 + v;
		}
		return true;
	}

	bool ParseString(std::wstring& out)
	{
		if (m_nPos >= m_strText.size() || m_strText[m_nPos] != L'"')
			return false;
		m_nPos++;
		out.clear();
		while (m_nPos < m_strText.size())
		{
			wchar_t c = m_strText[m_nPos++];
			if (c == L'"')
				return true;
			if (c != L'\\')
			{
				out += c;
				continue;
			}
			if (m_nPos >= m_strText.size())
				return false;
			wchar_t e = m_strText[m_nPos++];
			switch (e)
			{
			case L'"':  out += L'"';  break;
			case L'\\': out += L'\\'; break;
			case L'/':  out += L'/';  break;
			case L'b':  out += L'\b'; break;
			case L'f':  out += L'\f'; break;
			case L'n':  out += L'\n'; break;
			case L'r':  out += L'\r'; break;
			case L't':  out += L'\t'; break;
			case L'u':
			{
				unsigned int u = 0;
				if (!ReadHex4(u))
					return false;
				if (u >= 0xD800 && u <= 0xDBFF && m_nPos + 6 <= m_strText.size() &&
					m_strText[m_nPos] == L'\\' && m_strText[m_nPos + 1] == L'u')
				{
					size_t nSave = m_nPos;
					m_nPos += 2;
					unsigned int lo = 0;
					if (ReadHex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF)
					{
						out += (wchar_t)(0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
						break;
					}
					m_nPos = nSave;
				}
				if (u < 0xD800 || u > 0xDFFF)
					out += (wchar_t)u;
				break;
			}
			default:
				return false;
			}
		}
		return false;
	}

	bool ParseNumber(JsonValue& out)
	{
		size_t nStart = m_nPos;
		if (m_nPos < m_strText.size() && (m_strText[m_nPos] == L'-' || m_strText[m_nPos] == L'+'))
			m_nPos++;
		bool bDigit = false;
		while (m_nPos < m_strText.size())
		{
			wchar_t c = m_strText[m_nPos];
			if ((c >= L'0' && c <= L'9') || c == L'.' || c == L'e' || c == L'E' || c == L'-' || c == L'+')
			{
				if (c >= L'0' && c <= L'9')
					bDigit = true;
				m_nPos++;
			}
			else
				break;
		}
		if (!bDigit)
			return false;
		out.type = JsonValue::Number;
		out.dValue = wcstod(m_strText.c_str() + nStart, NULL);
		return true;
	}

	bool ParseValue(JsonValue& out, int nDepth)
	{
		if (nDepth > 100)
			return false;
		SkipSpace();
		if (m_nPos >= m_strText.size())
			return false;

		wchar_t c = m_strText[m_nPos];
		if (c == L'"')
		{
			out.type = JsonValue::String;
			return ParseString(out.str);
		}
		if (c == L'{')
		{
			m_nPos++;
			out.type = JsonValue::Object;
			SkipSpace();
			if (m_nPos < m_strText.size() && m_strText[m_nPos] == L'}')
			{
				m_nPos++;
				return true;
			}
			while (m_nPos < m_strText.size())
			{
				SkipSpace();
				std::wstring strKey;
				if (!ParseString(strKey))
					return false;
				SkipSpace();
				if (m_nPos >= m_strText.size() || m_strText[m_nPos] != L':')
					return false;
				m_nPos++;
				JsonValue val;
				if (!ParseValue(val, nDepth + 1))
					return false;
				out.obj.push_back(std::make_pair(strKey, val));
				SkipSpace();
				if (m_nPos >= m_strText.size())
					return false;
				if (m_strText[m_nPos] == L',')
				{
					m_nPos++;
					continue;
				}
				if (m_strText[m_nPos] == L'}')
				{
					m_nPos++;
					return true;
				}
				return false;
			}
			return false;
		}
		if (c == L'[')
		{
			m_nPos++;
			out.type = JsonValue::Array;
			SkipSpace();
			if (m_nPos < m_strText.size() && m_strText[m_nPos] == L']')
			{
				m_nPos++;
				return true;
			}
			while (m_nPos < m_strText.size())
			{
				JsonValue val;
				if (!ParseValue(val, nDepth + 1))
					return false;
				out.arr.push_back(val);
				SkipSpace();
				if (m_nPos >= m_strText.size())
					return false;
				if (m_strText[m_nPos] == L',')
				{
					m_nPos++;
					continue;
				}
				if (m_strText[m_nPos] == L']')
				{
					m_nPos++;
					return true;
				}
				// 兼容模型输出的非标准数组（如 [177 790 203 839]，数字间缺少逗号）
				continue;
			}
			return false;
		}
		if (m_strText.substr(m_nPos, 4) == L"true")
		{
			m_nPos += 4;
			out.type = JsonValue::Bool;
			out.bValue = true;
			return true;
		}
		if (m_strText.substr(m_nPos, 5) == L"false")
		{
			m_nPos += 5;
			out.type = JsonValue::Bool;
			out.bValue = false;
			return true;
		}
		if (m_strText.substr(m_nPos, 4) == L"null")
		{
			m_nPos += 4;
			out.type = JsonValue::Null;
			return true;
		}
		return ParseNumber(out);
	}

	std::wstring m_strText;
	size_t m_nPos;
};

static std::wstring JsonEscape(const std::wstring& src)
{
	std::wstring out;
	out.reserve(src.size() + 16);
	for (size_t i = 0; i < src.size(); i++)
	{
		wchar_t c = src[i];
		switch (c)
		{
		case L'"':  out += L"\\\""; break;
		case L'\\': out += L"\\\\"; break;
		case L'\n': out += L"\\n";  break;
		case L'\r': out += L"\\r";  break;
		case L'\t': out += L"\\t";  break;
		default:
			if (c < 0x20)
			{
				wchar_t szBuf[8] = { 0 };
				swprintf_s(szBuf, ARRAYSIZE(szBuf), L"\\u%04x", (unsigned int)c);
				out += szBuf;
			}
			else
			{
				out += c;
			}
		}
	}
	return out;
}

static bool GetJsonNumber(const JsonValue& obj, const wchar_t* lpszKey, double& dValue)
{
	const JsonValue* p = obj.Get(lpszKey);
	if (p == NULL || p->type != JsonValue::Number)
		return false;
	dValue = p->dValue;
	return true;
}

// 从任意文本中提取数字（兼容 "175 785 203 837" 这类非法 JSON 坐标写法）
static void CollectNumbers(const std::wstring& strText, std::vector<double>& nums)
{
	size_t i = 0;
	while (i < strText.size())
	{
		wchar_t c = strText[i];
		if ((c >= L'0' && c <= L'9') || c == L'-' || c == L'.')
		{
			size_t j = i;
			if (strText[j] == L'-')
				j++;
			bool bDigit = false;
			while (j < strText.size() && ((strText[j] >= L'0' && strText[j] <= L'9') || strText[j] == L'.'))
			{
				if (strText[j] >= L'0' && strText[j] <= L'9')
					bDigit = true;
				j++;
			}
			if (bDigit)
			{
				nums.push_back(wcstod(strText.c_str() + i, NULL));
				i = j;
				continue;
			}
		}
		i++;
	}
}

// 兼容 bbox 为数组、字符串、{x1,y1,x2,y2} 对象等多种输出形式
static bool ExtractBoxNumbers(const JsonValue& v, double vals[4])
{
	std::vector<double> nums;
	if (v.type == JsonValue::String)
	{
		CollectNumbers(v.str, nums);
	}
	else if (v.type == JsonValue::Array)
	{
		for (size_t k = 0; k < v.arr.size(); k++)
		{
			const JsonValue& e = v.arr[k];
			if (e.type == JsonValue::Number)
				nums.push_back(e.dValue);
			else if (e.type == JsonValue::String)
				CollectNumbers(e.str, nums);
		}
	}
	else if (v.type == JsonValue::Object)
	{
		if (GetJsonNumber(v, L"x1", vals[0]) && GetJsonNumber(v, L"y1", vals[1]) &&
			GetJsonNumber(v, L"x2", vals[2]) && GetJsonNumber(v, L"y2", vals[3]))
			return true;
	}

	if (nums.size() < 4)
		return false;
	for (int k = 0; k < 4; k++)
		vals[k] = nums[k];
	return true;
}

// 从 chat/completions 响应中取出 message.content 文本
static bool ExtractAiContent(const std::wstring& strResponse, std::wstring& strContent, std::wstring& strError)
{
	JsonValue root;
	JsonParser parser(strResponse);
	if (!parser.Parse(root) || root.type != JsonValue::Object)
	{
		strError = L"响应不是合法 JSON";
		return false;
	}

	const JsonValue* choices = root.Get(L"choices");
	if (choices == NULL || choices->type != JsonValue::Array || choices->arr.empty())
	{
		strError = L"响应缺少 choices 字段";
		return false;
	}
	const JsonValue* message = choices->arr[0].Get(L"message");
	if (message == NULL)
	{
		strError = L"响应缺少 message 字段";
		return false;
	}
	const JsonValue* content = message->Get(L"content");
	if (content == NULL)
	{
		strError = L"响应缺少 content 字段";
		return false;
	}

	if (content->type == JsonValue::String)
	{
		strContent = content->str;
	}
	else if (content->type == JsonValue::Array)
	{
		for (size_t i = 0; i < content->arr.size(); i++)
		{
			const JsonValue* text = content->arr[i].Get(L"text");
			if (text != NULL && text->type == JsonValue::String)
				strContent += text->str;
		}
	}

	if (strContent.empty())
	{
		strError = L"模型返回内容为空";
		return false;
	}
	return true;
}

// 解析模型输出的框，坐标可为像素（相对上传图）或 0~1 归一化，统一转为 YOLO 归一化
static bool ParseAiBoxes(const std::wstring& strContent, LONG nWidth, LONG nHeight,
	std::vector<AiBoxData>& boxes, std::wstring& strError)
{
	size_t nBegin = strContent.find(L'[');
	size_t nEnd = strContent.rfind(L']');
	if (nBegin == std::wstring::npos || nEnd == std::wstring::npos || nEnd <= nBegin)
	{
		strError = L"模型未返回 JSON 数组";
		return false;
	}

	JsonValue arr;
	JsonParser parser(strContent.substr(nBegin, nEnd - nBegin + 1));
	if (!parser.Parse(arr) || arr.type != JsonValue::Array)
	{
		strError = L"模型返回的 JSON 数组解析失败";
		return false;
	}

	boxes.clear();
	std::wstring strLastClass;
	for (size_t i = 0; i < arr.arr.size(); i++)
	{
		const JsonValue& item = arr.arr[i];
		if (item.type != JsonValue::Object)
			continue;

		std::wstring strName;
		const wchar_t* lpszNameKeys[] = { L"class", L"label", L"name" };
		for (int k = 0; k < 3 && strName.empty(); k++)
		{
			const JsonValue* v = item.Get(lpszNameKeys[k]);
			if (v != NULL && v->type == JsonValue::String)
				strName = v->str;
		}
		while (!strName.empty() && (strName[0] == L' ' || strName[0] == L'\t'))
			strName.erase(strName.begin());
		while (!strName.empty() && (strName[strName.size() - 1] == L' ' || strName[strName.size() - 1] == L'\t'))
			strName.erase(strName.size() - 1);
		// 模型偶尔会漏写类别，沿用上一个框的类别名
		if (strName.empty())
			strName = strLastClass;
		if (strName.empty())
			strName = L"object";
		strLastClass = strName;

		double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
		bool bGotBox = false;
		const JsonValue* bb = item.Get(L"bbox");
		if (bb == NULL)
			bb = item.Get(L"box");
		if (bb == NULL)
			bb = item.Get(L"position");
		if (bb != NULL)
		{
			double vals[4] = { 0, 0, 0, 0 };
			if (ExtractBoxNumbers(*bb, vals))
			{
				x1 = vals[0]; y1 = vals[1]; x2 = vals[2]; y2 = vals[3];
				bGotBox = true;
			}
		}
		if (!bGotBox && GetJsonNumber(item, L"x1", x1) && GetJsonNumber(item, L"y1", y1) &&
			GetJsonNumber(item, L"x2", x2) && GetJsonNumber(item, L"y2", y2))
		{
			bGotBox = true;
		}
		if (!bGotBox)
			continue;

		// 绝对值明显大于 1 视为像素坐标，反之以归一化坐标处理
		bool bPixel = fabs(x1) > 1.5 || fabs(y1) > 1.5 || fabs(x2) > 1.5 || fabs(y2) > 1.5;
		if (bPixel)
		{
			if (nWidth <= 0 || nHeight <= 0)
				continue;
			x1 /= nWidth; y1 /= nHeight;
			x2 /= nWidth; y2 /= nHeight;
		}

		double left = min(x1, x2), right = max(x1, x2);
		double top = min(y1, y2), bottom = max(y1, y2);
		left = min(max(left, 0.0), 1.0);
		top = min(max(top, 0.0), 1.0);
		right = min(max(right, 0.0), 1.0);
		bottom = min(max(bottom, 0.0), 1.0);
		if (right - left < 0.002 || bottom - top < 0.002)
			continue;

		AiBoxData box;
		box.cx = (left + right) / 2.0;
		box.cy = (top + bottom) / 2.0;
		box.w = right - left;
		box.h = bottom - top;
		box.strName = strName;
		boxes.push_back(box);
	}
	return true;
}

} // namespace


struct AiPreset { LPCTSTR szName; LPCTSTR szBase; LPCTSTR szModel; };
static const AiPreset g_aiPresets[] =
{
	{ _T("豆包(火山引擎)"), _T("https://ark.cn-beijing.volces.com/api/v3"),           _T("doubao-seed-2-1-pro-260628") },
	{ _T("通义千问(阿里)"), _T("https://dashscope.aliyuncs.com/compatible-mode/v1"),  _T("qwen3-vl-plus") },
	{ _T("智谱GLM"),        _T("https://open.bigmodel.cn/api/paas/v4"),               _T("glm-4v-flash") },
	{ _T("OpenAI"),         _T("https://api.openai.com/v1"),                          _T("gpt-4o-mini") },
	{ _T("DeepSeek"),       _T("https://api.deepseek.com/v1"),                        _T("deepseek-flash") },
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
		SetDlgItemText(IDC_EDIT_AI_KEY, cfg.strAiKey);
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
			SetStatus(_T("已撤销"));
		else
			SetStatus(_T("没有可撤销的"));
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
	mv(IDC_EDIT_AI_KEY, centerX + 42, M, 350, 22);
	mv(IDC_COMBO_AI_PROVIDER, centerX + 398, M, 112, 200);
	mv(IDC_LBL_AI_MODEL, centerX + 518 , M + 5, 32, 14);
	mv(IDC_EDIT_AI_MODEL, centerX + 550 , M, 200, 22);
	mv(IDC_BTN_AI_ANNOTATE, centerX + 756 , M, 88, 22);


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
		strDelItem.Format(_T("删除选中的 %d 张图片（含标签）"), nCount);
	else
		strDelItem = _T("删除图片（含标签）");
	menu.AppendMenu(MF_STRING, 1, strDelItem);
	menu.AppendMenu(MF_STRING, 2, _T("打开所在文件夹"));

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
			strMore.Format(_T("  ... 共 %d 张\r\n"), nCount);
			strList += strMore;
		}

		CString strMsg;
		if (nCount > 1)
			strMsg.Format(_T("确定删除选中的 %d 张图片吗？\r\n对应标签也会一并删除。\r\n\r\n%s"), nCount, (LPCTSTR)strList);
		else
			strMsg.Format(_T("确定删除这张图片吗？\r\n对应标签也会一并删除。\r\n\r\n%s"), (LPCTSTR)strList);
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
			strLog.Format(_T("已批量删除 %d 张图片、%d 个标签文件\r\n"), nDelImg, nDelLbl);
		else
			strLog.Format(_T("已删除 %s\r\n"), (LPCTSTR)arrSel[0]);
		if (nFail > 0)
		{
			CString strFail;
			strFail.Format(_T("共 %d 个文件删除失败（可能被占用）\r\n"), nFail);
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
		if (AfxMessageBox(_T("当前图像的标注尚未保存，是否保存？"), MB_YESNO | MB_ICONQUESTION) == IDYES)
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
		AppendLog(_T("标签保存失败: ") + strLabel + _T("\r\n"));
		return false;
	}
	AppendLog(_T("已保存标注: ") + strLabel + _T("\r\n"));
	UpdateAnnoStatus();
	return true;
}

bool CPageAnnotate::SaveAndNext()
{
	if (!m_canvas.HasImage())
	{
		SetStatus(_T("未加载图像，Ctrl+S 无操作"));
		return false;
	}

	if (m_canvas.GetBoxCount() <= 0)
	{
		AppendLog(_T("Ctrl+S 未保存：当前图片没有标注框，停在原图\r\n"));
		SetStatus(_T("当前图片没有标注框，请先画框再按 Ctrl+S"));
		return false;
	}

	if (!SaveCurrentLabels())
		return false;

	int nNext = m_nCurImage + 1;
	if (nNext < (int)m_arrImages.GetSize())
	{
		LoadImageAt(nNext);
		AppendLog(_T("Ctrl+S 已保存并跳到下一张\r\n"));
	}
	else
	{
		SetStatus(_T("已是最后一张，标注已保存"));
		AppendLog(_T("Ctrl+S 已保存（已是最后一张）\r\n"));
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
		str.Format(_T("文件: %s   缩放: %d%%   框: %d   |   0-9 切类别，空白处拖拽画框，框内按住拖动移动，拖角点改大小，Ctrl+Z 撤销，Delete 删除框，Ctrl+S / 回车 保存并跳到下一张"),
			szName, m_canvas.GetZoomPercent(), m_canvas.GetBoxCount());
	}
	else
	{
		str = _T("未加载图像，请在左侧列表中选择一张");
	}

	int nMulti = GetSelectedCount();
	if (nMulti >= 2)
	{
		CString strMulti;
		strMulti.Format(_T("   |   已选 %d 张（Ctrl/Shift 多选，右键批量删除）"), nMulti);
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
		strTip.Format(_T("当前类别 %s"), (LPCTSTR)strName);
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
		AfxMessageBox(_T("未找到项目数据集"), MB_ICONWARNING);
		return false;
	}

	CStringArray arrNames;
	if (!ParseClassNames(arrNames))
	{
		AfxMessageBox(_T("请至少填写一个类别（每行一个）。"), MB_ICONWARNING);
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
		strMsg.Format(_T("已保存 %d 个类别到 classes.txt 和 data.yaml\r\n"), (int)arrNames.GetSize());
		AppendLog(strMsg);
		FillClassCombo();
		SetStatus(_T("类别已保存"));
	}
}


void CPageAnnotate::OnBnClickedBtnAnnotate()
{

	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		AfxMessageBox(_T("未找到 YOLO_Green 绿色包或 python.exe。"), MB_ICONWARNING);
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
		AfxMessageBox(_T("未找到训练图片目录，请先在截图页选择项目根目录。"), MB_ICONWARNING);
		return;
	}

	CString strScript = strGreen + _T("\\scripts\\auto_annotate.py");
	if (!::PathFileExists(strScript))
	{
		AfxMessageBox(_T("未找到 scripts\\auto_annotate.py，请检查绿色包是否完整。"), MB_ICONWARNING);
		return;
	}

	CString strCmd;
	strCmd.Format(_T("\"%s\" -X utf8 -u \"%s\" --imgs \"%s\" --weights \"%s\" --conf %s"),
		(LPCTSTR)strPython, (LPCTSTR)strScript, (LPCTSTR)strImgs, (LPCTSTR)strWeightsPath, (LPCTSTR)strConf);

	AppendLog(_T("==== 开始预标注 ====\r\n") + strCmd + _T("\r\n"));

	if (!m_capture.StartProcess(strCmd, GetSafeHwnd()))
	{
		AppendLog(_T("创建预标注进程失败。\r\n"));
		SetStatus(_T("预标注启动失败"));
		return;
	}
	SetRunning(true);
	SetStatus(_T("预标注中..."));
}

void CPageAnnotate::OnCbnSelchangeComboAiProvider()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO_AI_PROVIDER);
	if (pCombo == NULL)
		return;
	int nSel = pCombo->GetCurSel();
	if (nSel >= 0 && nSel < g_aiPresetCount)
	{
		SetDlgItemText(IDC_EDIT_AI_MODEL, g_aiPresets[nSel].szModel);
		// 切换服务商后清空上一家的 Key，避免用 A 家的 Key 请求 B 家接口
		SetDlgItemText(IDC_EDIT_AI_KEY, _T(""));
	}
}

void CPageAnnotate::OnBnClickedBtnAiAnnotate()
{
	if (m_bAiRunning)
	{
		m_bAiStop = true;
		GetDlgItem(IDC_BTN_AI_ANNOTATE)->EnableWindow(FALSE);
		SetStatus(_T("AI 标注将在当前图片处理完后停止..."));
		return;
	}

	CString strKey;
	GetDlgItemText(IDC_EDIT_AI_KEY, strKey);
	strKey.Trim();
	if (strKey.IsEmpty())
	{
		AfxMessageBox(_T("请先在上面的 AI Key 输入框中填入大模型 API Key。"), MB_ICONINFORMATION);
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
		AfxMessageBox(_T("所有图片都已存在标签文件，没有需要 AI 标注的图片。"), MB_ICONINFORMATION);
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
	m_nAiRunId++;
	m_bAiStop = false;
	m_bAiClassesDirty = false;
	m_bAiRunning = true;
	SetAiRunningUI(true);

	CString strLog;
	strLog.Format(_T("==== 开始 AI 标注：共 %d 张未标注图片，逐张处理 ====\r\n"),
		(int)m_arrAiTodo.GetSize());
	AppendLog(strLog);
	StartAiAnnotateImage();
}

void CPageAnnotate::StartAiAnnotateImage()
{
	int nTotal = (int)m_arrAiTodo.GetSize();
	if (m_nAiIndex < 0 || m_nAiIndex >= nTotal)
	{
		AppendLog(_T("==== AI 标注全部完成 ====\r\n"));
		m_bAiRunning = false;
		SetAiRunningUI(false);
		SetStatus(_T("AI 标注完成"));
		UpdateAnnoStatus();
		return;
	}

	CString strImg = m_arrAiTodo[m_nAiIndex];
	TCHAR szName[MAX_PATH] = { 0 };
	_tcsncpy_s(szName, strImg, _TRUNCATE);
	::PathStripPath(szName);

	CString strStatus;
	strStatus.Format(_T("AI 标注中 (%d/%d): %s，等待模型返回..."),
		m_nAiIndex + 1, nTotal, szName);
	SetStatus(strStatus);

	std::wstring strImgPath = (LPCTSTR)strImg;
	std::wstring strKey = (LPCTSTR)m_strAiKey;
	std::wstring strModel = (LPCTSTR)m_strAiModel;

	// 组装 OpenAI 兼容接口地址（豆包火山方舟同样使用 chat/completions）
	std::wstring strUrl = (LPCTSTR)m_strAiBase;
	while (!strUrl.empty() && (strUrl[strUrl.size() - 1] == L'/' || strUrl[strUrl.size() - 1] == L' '))
		strUrl.erase(strUrl.size() - 1);
	strUrl += L"/chat/completions";

	// 已有类别作为提示，降低模型乱造类别名的概率
	std::wstring strClassesHint;
	CStringArray arrNames;
	ParseClassNames(arrNames);
	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
	{
		if (!strClassesHint.empty())
			strClassesHint += L"、";
		strClassesHint += std::wstring((LPCTSTR)arrNames[i]);
	}

	HWND hWnd = GetSafeHwnd();
	int nIndex = m_nAiIndex;
	int nRunId = m_nAiRunId;

	// 网络请求放到后台线程，避免界面卡死
	std::thread([hWnd, nIndex, nRunId, strImgPath, strKey, strUrl, strModel, strClassesHint]()
	{
		AiAnnotateResult* pRes = new AiAnnotateResult();
		pRes->nIndex = nIndex;
		pRes->strImage = strImgPath;

		LONG nWidth = 0, nHeight = 0;
		if (!GetImageSize(strImgPath.c_str(), nWidth, nHeight))
		{
			pRes->strError = L"图片读取失败";
		}
		else
		{
			// 坐标已统一为 0-1000 归一化，上传尺寸只影响识别精度；DeepSeek 用 768 最省 token
			LONG nMaxSide = 1024;
			if (strUrl.find(L"api.deepseek.com") != std::wstring::npos)
				nMaxSide = 768;
			LONG nSendW = nWidth, nSendH = nHeight;
			if (nWidth > nMaxSide || nHeight > nMaxSide)
			{
				double dScale = (double)nMaxSide / (double)max(nWidth, nHeight);
				nSendW = (LONG)(nWidth * dScale);
				nSendH = (LONG)(nHeight * dScale);
				if (nSendW < 1) nSendW = 1;
				if (nSendH < 1) nSendH = 1;
			}

			std::string strBase64 = ImageToJpegBase64(strImgPath.c_str(), nMaxSide, 85);
			if (strBase64.empty())
			{
				pRes->strError = L"图片编码失败";
			}
			else
			{
				std::wstring strPrompt;
				strPrompt += L"只输出JSON数组，不要解释或代码块。格式：{\"class\":\"类别\",\"bbox\":[x1,y1,x2,y2]}，bbox为0-1000归一化坐标。";
				if (!strClassesHint.empty())
					strPrompt += L"只标类别：" + strClassesHint + L"；忽略UI（小地图、状态栏、按钮等）与背景（天空、山水、树木等）；没有符合目标输出 []。";
				else
					strPrompt += L"只标游戏中的目标对象，忽略UI与背景；没有目标输出 []。";

				std::wstring strBody;
				strBody += L"{\"model\":\"";
				strBody += JsonEscape(strModel);
				strBody += L"\",\"temperature\":0.1,\"max_tokens\":";
				// DeepSeek 默认推理量较大，给足输出上限，避免推理占满额度导致正文为空
				strBody += (strUrl.find(L"api.deepseek.com") != std::wstring::npos) ? L"8192" : L"4096";
				// 豆包/DeepSeek 默认开启思考模式，标注任务关掉以大幅减少推理 token
				if (strUrl.find(L"volces.com") != std::wstring::npos ||
					strUrl.find(L"api.deepseek.com") != std::wstring::npos)
					strBody += L",\"thinking\":{\"type\":\"disabled\"}";
				strBody += L",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"";
				strBody += JsonEscape(strPrompt);
				strBody += L"\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
				strBody.append(strBase64.begin(), strBase64.end());
				strBody += L"\"";
				// 豆包/千问/DeepSeek 支持低精度图片，可大幅降低图片 token
				if (strUrl.find(L"volces.com") != std::wstring::npos ||
					strUrl.find(L"dashscope.aliyuncs.com") != std::wstring::npos ||
					strUrl.find(L"api.deepseek.com") != std::wstring::npos)
					strBody += L",\"detail\":\"low\"";
				strBody += L"}}]}]}";

				std::string strUtf8;
				int nUtf8Len = ::WideCharToMultiByte(CP_UTF8, 0, strBody.c_str(), (int)strBody.size(), NULL, 0, NULL, NULL);
				if (nUtf8Len > 0)
				{
					strUtf8.resize((size_t)nUtf8Len);
					::WideCharToMultiByte(CP_UTF8, 0, strBody.c_str(), (int)strBody.size(), &strUtf8[0], nUtf8Len, NULL, NULL);
				}

				std::wstring strAuth = L"Authorization: Bearer " + strKey + L"\r\n";
				DWORD dwCode = 0;
				std::vector<char> vecResp = HttpPost(strUrl, strUtf8.c_str(), (DWORD)strUtf8.size(), strAuth, dwCode);

				if (vecResp.empty())
				{
					pRes->strError = L"请求失败：网络错误、超时或无法连接服务器";
				}
				else if (dwCode != 200)
				{
					CStringW strBodyW(CA2W(vecResp.data(), CP_UTF8));
					std::wstring strBodyWs = (LPCWSTR)strBodyW;
					if (strBodyWs.size() > 300)
						strBodyWs = strBodyWs.substr(0, 300) + L"...";
					wchar_t szErr[64] = { 0 };
					swprintf_s(szErr, ARRAYSIZE(szErr), L"HTTP %lu: ", dwCode);
					pRes->strError = szErr + strBodyWs;
				}
				else
				{
					CStringW strBodyW(CA2W(vecResp.data(), CP_UTF8));
					std::wstring strBodyWs = (LPCWSTR)strBodyW;
					std::wstring strContent, strErr;
					LONG nCoordW = 1000, nCoordH = 1000;
					if (strUrl.find(L"api.openai.com") != std::wstring::npos)
					{
						nCoordW = nSendW;
						nCoordH = nSendH;
					}
					if (!ExtractAiContent(strBodyWs, strContent, strErr))
						pRes->strError = strErr;
					else if (!ParseAiBoxes(strContent, nCoordW, nCoordH, pRes->boxes, strErr))
					{
						std::wstring strRaw = strContent;
						if (strRaw.size() > 200)
							strRaw = strRaw.substr(0, 200) + L"...";
						pRes->strError = strErr + L"；原始输出: " + strRaw;
					}
					else
						pRes->bOk = true;
				}
			}
		}

		if (!::IsWindow(hWnd) || !::PostMessage(hWnd, WM_USER_AI_RESULT, (WPARAM)nRunId, (LPARAM)pRes))
			delete pRes;
	}).detach();
}

LRESULT CPageAnnotate::OnAiResult(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<AiAnnotateResult> pResult((AiAnnotateResult*)lParam);
	if (pResult == NULL)
		return 0;

	// 已停止或已开始新一轮任务，丢弃过期结果
	if (!m_bAiRunning || (int)wParam != m_nAiRunId)
		return 0;

	HandleAiResult(pResult.get());
	return 0;
}

static bool IsAiClassAllowed(const CStringArray& arrNames, const CString& strName);

void CPageAnnotate::HandleAiResult(AiAnnotateResult* pResult)
{
	if (pResult == NULL)
		return;

	CString strImg(pResult->strImage.c_str());
	TCHAR szName[MAX_PATH] = { 0 };
	_tcsncpy_s(szName, strImg, _TRUNCATE);
	::PathStripPath(szName);
	int nTotal = (int)m_arrAiTodo.GetSize();

	if (pResult->bOk)
	{
		CStringArray arrKnown;
		ParseClassNames(arrKnown);
		bool bRestrict = arrKnown.GetSize() > 0;

		int nWritten = 0;
		int nSkipped = 0;
		CStringArray arrSkipNames;
		CString strContent;
		for (size_t i = 0; i < pResult->boxes.size(); i++)
		{
			const AiBoxData& b = pResult->boxes[i];
			CString strName(b.strName.c_str());

			// 只保留类别表中已有的类别，模型自行编造的类别一律丢弃
			if (bRestrict && !IsAiClassAllowed(arrKnown, strName))
			{
				nSkipped++;
				bool bExists = false;
				for (INT_PTR k = 0; k < arrSkipNames.GetSize(); k++)
				{
					if (arrSkipNames[k].CompareNoCase(strName) == 0)
					{
						bExists = true;
						break;
					}
				}
				if (!bExists)
					arrSkipNames.Add(strName);
				continue;
			}

			int nCls = MergeAiClass(strName);
			CString strLine;
			strLine.Format(_T("%d %.6f %.6f %.6f %.6f\n"), nCls, b.cx, b.cy, b.w, b.h);
			strContent += strLine;
			nWritten++;
		}

		// 只有标注成功才写标签：零框且无过滤说明模型确认无目标；全部被过滤则不写，保留重试
		bool bWriteLabel = (nWritten > 0) || (nSkipped == 0);
		CString strLabel = GetLabelPathFor(strImg);
		if (bWriteLabel && !WriteUtf8File(strLabel, strContent))
			AppendLog(_T("标签写入失败: ") + strLabel + _T("\r\n"));

		CString strSkip;
		if (nSkipped > 0)
		{
			strSkip.Format(_T("（已过滤 %d 个非类别表目标"), nSkipped);
			for (INT_PTR k = 0; k < arrSkipNames.GetSize() && k < 5; k++)
			{
				strSkip += (k == 0) ? _T(": ") : _T("、");
				strSkip += arrSkipNames[k];
			}
			if (arrSkipNames.GetSize() > 5)
				strSkip += _T("等");
			strSkip += _T("）");
		}

		CString strLog;
		if (!bWriteLabel)
			strLog.Format(_T("[AI %d/%d] %s -> 未写入标签%s，可重试"),
				m_nAiIndex + 1, nTotal, szName, (LPCTSTR)strSkip);
		else if (nWritten == 0)
			strLog.Format(_T("[AI %d/%d] %s -> 未检测到目标（已写入空标签）"),
				m_nAiIndex + 1, nTotal, szName);
		else
			strLog.Format(_T("[AI %d/%d] %s -> %d 个框%s"),
				m_nAiIndex + 1, nTotal, szName, nWritten, (LPCTSTR)strSkip);
		strLog += _T("\r\n");
		AppendLog(strLog);
	}
	else
	{
		CString strErr(pResult->strError.c_str());
		if (strErr.IsEmpty())
			strErr = _T("未知错误");
		CString strLog;
		strLog.Format(_T("[AI %d/%d] %s 失败：%s（已跳过，下次可重试）\r\n"),
			m_nAiIndex + 1, nTotal, szName, (LPCTSTR)strErr);
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
		AppendLog(_T("==== AI 标注已停止 ====\r\n"));
		m_bAiRunning = false;
		m_bAiStop = false;
		SetAiRunningUI(false);
		SetStatus(_T("AI 标注已停止"));
		UpdateAnnoStatus();
		return;
	}
	if (m_nAiIndex >= nTotal)
	{
		AppendLog(_T("==== AI 标注全部完成 ====\r\n"));
		m_bAiRunning = false;
		SetAiRunningUI(false);
		SetStatus(_T("AI 标注完成"));
		UpdateAnnoStatus();
		return;
	}
	StartAiAnnotateImage();
}


static bool IsAiClassAllowed(const CStringArray& arrNames, const CString& strName)
{
	for (INT_PTR i = 0; i < arrNames.GetSize(); i++)
	{
		if (arrNames[i].CompareNoCase(strName) == 0)
			return true;
	}
	return false;
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
			AppendLog(_T("类别以 AI 为准重命名: ") + strOld + _T(" -> ") + strAiName + _T("\r\n"));
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
	AppendLog(_T("AI 新增类别: ") + strAiName + _T("\r\n"));
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
	SetDlgItemText(IDC_BTN_AI_ANNOTATE, bRun ? _T("停止AI标注") : _T("开始AI标注"));
}

void CPageAnnotate::OnBnClickedBtnLabelImg()
{
	CString strGreen = GetGreenRoot();
	CString strPython = GetGreenPython();
	if (strGreen.IsEmpty() || strPython.IsEmpty())
	{
		AfxMessageBox(_T("未找到 YOLO_Green 绿色包或 python.exe。"), MB_ICONWARNING);
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
		AppendLog(_T("已启动 labelImg\r\n"));
	}
	else
	{
		CString strErr;
		strErr.Format(_T("labelImg 启动失败 (错误 %lu)\r\n"), ::GetLastError());
		AppendLog(strErr);
	}
}


LRESULT CPageAnnotate::OnRecvLog(WPARAM /*wParam*/, LPARAM lParam)
{
	CStringW* pMsg = (CStringW*)lParam;
	delete pMsg;
	return 0;
}
LRESULT CPageAnnotate::OnProcExit(WPARAM wParam, LPARAM /*lParam*/)
{
	DWORD dwExit = (DWORD)wParam;

	m_capture.Stop();

	SetStatus(dwExit == 0 ? _T("预标注完成") : _T(""));

	SetRunning(false);

	CString strMsg;
	strMsg.Format(_T("==== 预标注结束 (退出码 %lu) ====\r\n"), dwExit);
	AppendLog(strMsg);

	if (dwExit == 0 && m_canvas.HasImage())
		m_canvas.LoadImage(m_canvas.GetImagePath(), GetLabelPathFor(m_canvas.GetImagePath()));

	UpdateAnnoStatus();
	return 0;
}

void CPageAnnotate::SaveConfig()
{
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
}

void CPageAnnotate::OnDestroy()
{
	m_capture.Stop();
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
