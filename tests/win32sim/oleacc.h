#ifndef STUB_OLEACC_H
#define STUB_OLEACC_H
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((long)(hr)) >= 0)
#endif
typedef unsigned short VARTYPE;
typedef struct tagVARIANT { VARTYPE vt; unsigned short r1, r2, r3; union { LONG lVal; void *p; } u; } VARIANT;
#define V_VT(v) ((v)->vt)
#define V_I4(v) ((v)->u.lVal)
#define VT_I4 3
void VariantInit(VARIANT *);
HRESULT VariantClear(VARIANT *);
typedef struct IAccessible IAccessible;
typedef struct IAccessibleVtbl {
    HRESULT (*get_accState)(IAccessible *, VARIANT, VARIANT *);
    ULONG (*Release)(IAccessible *);
} IAccessibleVtbl;
struct IAccessible { const IAccessibleVtbl *lpVtbl; };
HRESULT AccessibleObjectFromEvent(HWND, DWORD, DWORD, IAccessible **, VARIANT *);
#define STATE_SYSTEM_PROTECTED 0x20000000
#define COINIT_APARTMENTTHREADED 2
HRESULT CoInitializeEx(void *, DWORD);
void CoUninitialize(void);
#endif
