

#include "pch.h"

#include "PageTrain.h"
#include "PageAnnotate.h"
#include "PageTest.h"
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

extern CPageTrain* g_CPageTrain;
extern CPageAnnotate* g_CPagethis;
extern CPageTest* g_CPageTest;
void UpPt()
{
	g_CPageTest->FillModelCombo();
	g_CPagethis->FillWeightsCombo();
	g_CPageTrain->FillWeightsCombo();

}