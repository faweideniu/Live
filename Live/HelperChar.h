#pragma once
#ifdef __cplusplus
extern "C"
{
#endif

WCHAR* c2w(char *);
char* w2c(WCHAR*);

void memblast(void* dest, void* src, DWORD count);

#ifdef __cplusplus
}
#endif
