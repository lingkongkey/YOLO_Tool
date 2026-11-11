
#ifndef PCH_H
#define PCH_H

#include "framework.h"
int filexist(CString str);
#include <string>
#include <vector>
typedef struct _YOLOPT  //
{
public:
	CString pt;
	CString ptName;
	CString ncnnParam;
	CString ncnnBin;
	long ptdex;
	bool bCanConvertNcnn = true;
}YOLOPT, * PYOLOPT;

void IsTestPt();
void RefreshWeightCombos();
BOOL IsPt(std::vector<YOLOPT>& pt, CString str);
#endif //PCH_H
