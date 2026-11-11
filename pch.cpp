

#include "pch.h"


BOOL IsPt(std::vector<YOLOPT>& pt,CString str)
{

	for (size_t i = 0; i < pt.size(); i++)
	{
		if (str==pt[i].ptName)
		{
			return TRUE;
		}
	}

	return FALSE;
}