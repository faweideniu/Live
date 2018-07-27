#pragma once

#include "stdafx.h"




#ifdef	__cplusplus
extern "C"
{
#endif

void setThresholdRatio(CString* pThreshold, CString* pRatio);
DWORD WINAPI mainC(LPVOID lpParam);
#ifdef __cplusplus
};
#endif