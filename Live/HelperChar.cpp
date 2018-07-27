#include "stdafx.h"
#include "HelperChar.h"

WCHAR* c2w(char * CStr)
{

    // char *CStr = "string to convert";

    size_t len = strlen(CStr) + 1;

    size_t converted = 0;

    WCHAR *WStr;

    WStr = (wchar_t*)malloc(len*sizeof(wchar_t));
    //memmove()
    mbstowcs_s(&converted, WStr, len, CStr, _TRUNCATE);

    return WStr;
}
char* w2c(WCHAR* WStr)
{
    //wchar_t *WStr = &WStri;

    size_t len = wcslen(WStr) + 1;

    size_t converted = 0;

    char *CStr;

    CStr = (char*)malloc(len*sizeof(char));

    wcstombs_s(&converted, CStr, len, WStr, _TRUNCATE);

    return CStr;
}
void memblast(void* dest, void* src, DWORD count)
{
    DWORD	iCount;

    __asm {
        MOV		ECX, count
        SHR		ECX, 2
        SHL		ECX, 2
        MOV		iCount, ECX

        MOV		ESI, src
        MOV		EDI, dest
        MOV		ECX, iCount
        SHR		ECX, 2
        REP		MOVSD

        MOV		ECX, count
        MOV		EAX, iCount
        SUB		ECX, EAX

        JZ		Exit

        MOV		ESI, src
        ADD		ESI, EAX
        MOV		EDI, dest
        ADD		EDI, EAX
        REP		MOVSB
        Exit :
    }
}