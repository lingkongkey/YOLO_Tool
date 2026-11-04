#pragma once

inline int UiDpi(HWND hwnd)
{
	HDC hdc = ::GetDC(hwnd);
	int dpi = ::GetDeviceCaps(hdc, LOGPIXELSX);
	::ReleaseDC(hwnd, hdc);
	return dpi > 0 ? dpi : 96;
}


inline int UiScale(HWND hwnd, int v)
{
	return ::MulDiv(v, UiDpi(hwnd), 96);
}


inline bool UiIsMultilineEdit(HWND hwnd)
{
	TCHAR szClass[64] = { 0 };
	::GetClassName(hwnd, szClass, _countof(szClass));
	if (_tcsicmp(szClass, _T("Edit")) != 0)
		return false;
	LONG_PTR nStyle = ::GetWindowLongPtr(hwnd, GWL_STYLE);
	return (nStyle & ES_MULTILINE) != 0;
}


inline bool UiIsEditControl(HWND hwnd)
{
	if (hwnd == NULL)
		return false;
	TCHAR szClass[64] = { 0 };
	::GetClassName(hwnd, szClass, _countof(szClass));
	return _tcsicmp(szClass, _T("Edit")) == 0;
}


inline bool UiForwardEditPaste(MSG* pMsg)
{
	if (pMsg == NULL || pMsg->message != WM_KEYDOWN || pMsg->wParam != _T('V'))
		return false;
	if ((::GetKeyState(VK_CONTROL) & 0x8000) == 0)
		return false;
	HWND hFocus = ::GetFocus();
	if (!UiIsEditControl(hFocus))
		return false;
	::SendMessage(hFocus, WM_PASTE, 0, 0);
	return true;
}


inline bool UiSwallowDialogKey(MSG* pMsg)
{
	if (pMsg == NULL || pMsg->message != WM_KEYDOWN)
		return false;

	if (pMsg->wParam == VK_ESCAPE || pMsg->wParam == VK_F1)
		return true;

	if (pMsg->wParam == VK_RETURN)
	{
		
		if (UiIsMultilineEdit(pMsg->hwnd))
			return false;
		return true;
	}

	return false;
}
