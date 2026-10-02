/*
 * A small subset of the Win32 API, enough to compile src/app.c and
 * src/installer.c on Linux: for the type checks in build-native.sh and for
 * the hook simulation (tests/app_sim.c). Declarations only; the values of
 * constants match the Windows SDK where the program depends on them.
 * tests/win32sim/generate_weak.py turns the declarations into do-nothing
 * definitions that the simulation overrides where it plays Windows.
 */
#ifndef W32STUB_WINDOWS_H
#define W32STUB_WINDOWS_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <string.h>

#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef int INT;
typedef long LONG;
typedef unsigned long ULONG;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef long HRESULT;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HINSTANCE;
typedef void *HMODULE;
typedef void *HDC;
typedef void *HFONT;
typedef void *HBRUSH;
typedef void *HPEN;
typedef void *HGDIOBJ;
typedef void *HGLOBAL;
typedef void *HHOOK;
typedef void *HKEY;
typedef void *HKL;
typedef void *HMENU;
typedef void *HRSRC;
typedef void *HICON;
typedef void *HCURSOR;
typedef HICON *PHICON;
typedef DWORD COLORREF;
typedef WORD LANGID;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef HANDLE HGLOBAL_;
typedef BOOL *LPBOOL;
typedef DWORD *LPDWORD;

typedef struct tagPOINT { LONG x, y; } POINT;
typedef struct tagRECT { LONG left, top, right, bottom; } RECT;
typedef struct tagMSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct tagPAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; } PAINTSTRUCT;
typedef struct tagWNDCLASSEXW {
    UINT cbSize; UINT style; LRESULT (*lpfnWndProc)(HWND, UINT, WPARAM, LPARAM);
    int cbClsExtra; int cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor;
    HBRUSH hbrBackground; LPCWSTR lpszMenuName; LPCWSTR lpszClassName; HICON hIconSm;
} WNDCLASSEXW;
typedef struct tagDRAWITEMSTRUCT {
    UINT CtlType; UINT CtlID; UINT itemID; UINT itemAction; UINT itemState;
    HWND hwndItem; HDC hDC; RECT rcItem; ULONG_PTR itemData;
} DRAWITEMSTRUCT;
typedef struct tagKBDLLHOOKSTRUCT { DWORD vkCode; DWORD scanCode; DWORD flags; DWORD time; ULONG_PTR dwExtraInfo; } KBDLLHOOKSTRUCT;
typedef struct tagMSLLHOOKSTRUCT { POINT pt; DWORD mouseData; DWORD flags; DWORD time; ULONG_PTR dwExtraInfo; } MSLLHOOKSTRUCT;
typedef struct tagGUITHREADINFO { DWORD cbSize; DWORD flags; HWND hwndActive; HWND hwndFocus; HWND hwndCapture; HWND hwndMenuOwner; HWND hwndMoveSize; HWND hwndCaret; RECT rcCaret; } GUITHREADINFO;
typedef struct tagLASTINPUTINFO { UINT cbSize; DWORD dwTime; } LASTINPUTINFO;
typedef struct tagKEYBDINPUT { WORD wVk; WORD wScan; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; } KEYBDINPUT;
typedef struct tagMOUSEINPUT { LONG dx, dy; DWORD mouseData, dwFlags, time; ULONG_PTR dwExtraInfo; } MOUSEINPUT;
typedef struct tagINPUT { DWORD type; union { MOUSEINPUT mi; KEYBDINPUT ki; }; } INPUT;
typedef struct _STARTUPINFOW { DWORD cb; LPWSTR lpReserved; LPWSTR lpDesktop; LPWSTR lpTitle; DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; WORD wShowWindow, cbReserved2; BYTE *lpReserved2; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOW;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess; HANDLE hThread; DWORD dwProcessId; DWORD dwThreadId; } PROCESS_INFORMATION;
typedef struct tagPROCESSENTRY32W { DWORD dwSize; DWORD cntUsage; DWORD th32ProcessID; ULONG_PTR th32DefaultHeapID; DWORD th32ModuleID; DWORD cntThreads; DWORD th32ParentProcessID; LONG pcPriClassBase; DWORD dwFlags; wchar_t szExeFile[260]; } PROCESSENTRY32W;
typedef struct _NOTIFYICONDATAW { DWORD cbSize; HWND hWnd; UINT uID; UINT uFlags; UINT uCallbackMessage; HICON hIcon; wchar_t szTip[128]; DWORD dwState; DWORD dwStateMask; wchar_t szInfo[256]; union { UINT uTimeout; UINT uVersion; }; wchar_t szInfoTitle[64]; DWORD dwInfoFlags; } NOTIFYICONDATAW;
typedef struct tagINITCOMMONCONTROLSEX { DWORD dwSize; DWORD dwICC; } INITCOMMONCONTROLSEX;
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES;
typedef struct _GUID { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; } GUID;
typedef GUID IID; typedef GUID CLSID;
typedef const IID *REFIID; typedef const CLSID *REFCLSID;

typedef LRESULT (*HOOKPROC)(int, WPARAM, LPARAM);
typedef INT_PTR (*DLGPROC)(HWND, UINT, WPARAM, LPARAM);

#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define ERROR_SUCCESS 0L
#define ERROR_ALREADY_EXISTS 183L
#define WAIT_TIMEOUT 258L
#define GENERIC_WRITE 0x40000000L
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define MOVEFILE_DELAY_UNTIL_REBOOT 4
#define CREATE_NO_WINDOW 0x08000000
#define PROCESS_TERMINATE 1
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define SYNCHRONIZE 0x00100000L
#define TH32CS_SNAPPROCESS 2
#define KEY_SET_VALUE 2
#define KEY_WRITE 0x20006
#define REG_SZ 1
#define REG_DWORD 4
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define RT_RCDATA ((LPWSTR)10)
#define MAKEINTRESOURCEW(i) ((LPWSTR)(ULONG_PTR)(WORD)(i))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define PRIMARYLANGID(l) ((WORD)(l) & 0x3ff)
#define LANG_ENGLISH 0x09
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb)>>16))
#define ZeroMemory(p,n) memset((p),0,(n))
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr) ((HRESULT)(hr) < 0)
#define RPC_E_CHANGED_MODE ((HRESULT)0x80010106L)
#define COINIT_APARTMENTTHREADED 2
#define CLSCTX_INPROC_SERVER 1
#define MB_OK 0
#define MB_YESNO 4
#define MB_ICONERROR 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONINFORMATION 0x40
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define CW_USEDEFAULT ((int)0x80000000)
#define WS_OVERLAPPED 0
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_EX_APPWINDOW 0x40000L
#define WS_EX_CLIENTEDGE 0x200L
#define BS_OWNERDRAW 0xBL
#define BS_AUTOCHECKBOX 3L
#define CBS_DROPDOWNLIST 3L
#define ES_AUTOHSCROLL 0x80L
#define ES_PASSWORD 0x20L
#define SS_ENDELLIPSIS 0x4000L
#define CB_ADDSTRING 0x143
#define CB_SETCURSEL 0x14E
#define CB_GETCURSEL 0x147
#define BM_GETCHECK 0xF0
#define BM_SETCHECK 0xF1
#define BST_CHECKED 1
#define BST_UNCHECKED 0
#define EM_GETPASSWORDCHAR 0xD2
#define WM_NULL 0
#define WM_CREATE 1
#define WM_DESTROY 2
#define WM_PAINT 0xF
#define WM_CLOSE 0x10
#define WM_ERASEBKGND 0x14
#define WM_SHOWWINDOW 0x18
#define WM_SETFONT 0x30
#define WM_DRAWITEM 0x2B
#define WM_SETICON 0x80
#define WM_KEYDOWN 0x100
#define WM_KEYUP 0x101
#define WM_SYSKEYDOWN 0x104
#define WM_SYSKEYUP 0x105
#define WM_INITDIALOG 0x110
#define WM_COMMAND 0x111
#define WM_TIMER 0x113
#define WM_HOTKEY 0x312
#define WM_CTLCOLOREDIT 0x133
#define WM_CTLCOLORSTATIC 0x138
#define WM_INPUTLANGCHANGEREQUEST 0x50
#define WM_CONTEXTMENU 0x7B
#define WM_LBUTTONDOWN 0x201
#define WM_LBUTTONDBLCLK 0x203
#define WM_RBUTTONDOWN 0x204
#define WM_RBUTTONUP 0x205
#define WM_MBUTTONDOWN 0x207
#define WM_XBUTTONDOWN 0x20B
#define WM_APP 0x8000
#define ODS_SELECTED 1
#define DT_CENTER 1
#define DT_VCENTER 4
#define DT_SINGLELINE 0x20
#define TRANSPARENT 1
#define PS_SOLID 0
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define COLOR_WINDOW 5
#define LOGPIXELSX 88
#define GWL_STYLE (-16)
#define GW_CHILD 5
#define GW_HWNDNEXT 2
#define MF_STRING 0
#define MF_CHECKED 8
#define MF_SEPARATOR 0x800
#define MF_POPUP 0x10
#define TPM_RIGHTALIGN 8
#define TPM_BOTTOMALIGN 0x20
#define ICON_BIG 1
#define SMTO_BLOCK 1
#define SMTO_ABORTIFHUNG 2
#define WH_KEYBOARD_LL 13
#define WH_MOUSE_LL 14
#define LLKHF_INJECTED 0x10
#define LLKHF_EXTENDED 0x01
#define VK_NUMPAD0 0x60
#define VK_DIVIDE 0x6F
#define LLMHF_INJECTED 1
#define MOD_CONTROL 2
#define MOD_WIN 8
#define INPUT_KEYBOARD 1
#define KEYEVENTF_KEYUP 2
#define KEYEVENTF_UNICODE 4
#define MAPVK_VSC_TO_VK_EX 3
#define NIF_MESSAGE 1
#define NIF_ICON 2
#define NIF_TIP 4
#define NIF_SHOWTIP 0x80
#define NIM_ADD 0
#define NIM_MODIFY 1
#define NIM_DELETE 2
#define NIM_SETVERSION 4
#define NOTIFYICON_VERSION_4 4
#define ICC_STANDARD_CLASSES 0x4000
#define WC_COMBOBOXW L"ComboBox"
#define CSIDL_DESKTOPDIRECTORY 0x10
#define CSIDL_PROGRAMS 2
#define CSIDL_FLAG_CREATE 0x8000
#define SHGFP_TYPE_CURRENT 0
#define VK_BACK 8
#define VK_TAB 9
#define VK_RETURN 0xD
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_2 0xBF
#define VK_OEM_PERIOD 0xBE

HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
#define CreateWindowW(a,b,c,d,e,f,g,h,i,j,k) CreateWindowExW(0,a,b,c,d,e,f,g,h,i,j,k)
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
BOOL DestroyWindow(HWND);
BOOL ShowWindow(HWND, int);
BOOL SetForegroundWindow(HWND);
HWND GetForegroundWindow(void);
HWND FindWindowW(LPCWSTR, LPCWSTR);
HWND FindWindowExW(HWND, HWND, LPCWSTR, LPCWSTR);
typedef BOOL (CALLBACK *WNDENUMPROC)(HWND, LPARAM);
BOOL EnumThreadWindows(DWORD, WNDENUMPROC, LPARAM);
BOOL EnumChildWindows(HWND, WNDENUMPROC, LPARAM);
HWND GetWindow(HWND, UINT);
HWND GetDlgItem(HWND, int);
BOOL EnableWindow(HWND, BOOL);
HWND SetFocus(HWND);
BOOL SetWindowTextW(HWND, LPCWSTR);
int GetWindowTextW(HWND, LPWSTR, int);
BOOL SetDlgItemTextW(HWND, int, LPCWSTR);
int GetClassNameW(HWND, LPWSTR, int);
LONG_PTR GetWindowLongPtrW(HWND, int);
DWORD GetWindowThreadProcessId(HWND, LPDWORD);
BOOL GetClientRect(HWND, RECT *);
BOOL InvalidateRect(HWND, const RECT *, BOOL);
BOOL AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM);
LRESULT SendDlgItemMessageW(HWND, int, UINT, WPARAM, LPARAM);
LRESULT SendMessageTimeoutW(HWND, UINT, WPARAM, LPARAM, UINT, UINT, DWORD_PTR *);
BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM);
void PostQuitMessage(int);
BOOL GetMessageW(MSG *, HWND, UINT, UINT);
BOOL TranslateMessage(const MSG *);
LRESULT DispatchMessageW(const MSG *);
UINT RegisterWindowMessageW(LPCWSTR);
unsigned short RegisterClassExW(const WNDCLASSEXW *);
INT_PTR DialogBoxParamW(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
BOOL EndDialog(HWND, INT_PTR);
UINT_PTR SetTimer(HWND, UINT_PTR, UINT, void *);
BOOL KillTimer(HWND, UINT_PTR);
BOOL RegisterHotKey(HWND, int, UINT, UINT);
BOOL UnregisterHotKey(HWND, int);
HHOOK SetWindowsHookExW(int, HOOKPROC, HINSTANCE, DWORD);
BOOL UnhookWindowsHookEx(HHOOK);
LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM);
UINT SendInput(UINT, INPUT *, int);
short GetAsyncKeyState(int);
short GetKeyState(int);
HKL GetKeyboardLayout(DWORD);
int GetKeyboardLayoutList(int, HKL *);
UINT MapVirtualKeyExW(UINT, UINT, HKL);
int ToUnicodeEx(UINT, UINT, const BYTE *, LPWSTR, int, UINT, HKL);
BOOL GetGUIThreadInfo(DWORD, GUITHREADINFO *);
BOOL GetLastInputInfo(LASTINPUTINFO *);
BOOL GetCursorPos(POINT *);
HMENU CreatePopupMenu(void);
BOOL AppendMenuW(HMENU, UINT, UINT_PTR, LPCWSTR);
BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT *);
BOOL DestroyMenu(HMENU);
HICON LoadIconW(HINSTANCE, LPCWSTR);
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
BOOL SetProcessDPIAware(void);
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
int GetDeviceCaps(HDC, int);
HDC BeginPaint(HWND, PAINTSTRUCT *);
BOOL EndPaint(HWND, const PAINTSTRUCT *);
int FillRect(HDC, const RECT *, HBRUSH);
HBRUSH CreateSolidBrush(COLORREF);
HPEN CreatePen(int, int, COLORREF);
HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL DeleteObject(HGDIOBJ);
BOOL RoundRect(HDC, int, int, int, int, int, int);
BOOL TextOutW(HDC, int, int, LPCWSTR, int);
int DrawTextW(HDC, LPCWSTR, int, RECT *, UINT);
int SetBkMode(HDC, int);
COLORREF SetBkColor(HDC, COLORREF);
COLORREF SetTextColor(HDC, COLORREF);
int MulDiv(int, int, int);
int lstrlenW(LPCWSTR);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
DWORD GetLastError(void);
void SetLastError(DWORD);
DWORD GetCurrentProcessId(void);
HANDLE OpenProcess(DWORD, BOOL, DWORD);
BOOL CloseHandle(HANDLE);
BOOL TerminateProcess(HANDLE, UINT);
DWORD WaitForSingleObject(HANDLE, DWORD);
void Sleep(DWORD);
BOOL QueryFullProcessImageNameW(HANDLE, DWORD, LPWSTR, DWORD *);
HANDLE CreateMutexW(SECURITY_ATTRIBUTES *, BOOL, LPCWSTR);
HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, SECURITY_ATTRIBUTES *, DWORD, DWORD, HANDLE);
BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, void *);
BOOL FlushFileBuffers(HANDLE);
BOOL DeleteFileW(LPCWSTR);
BOOL MoveFileExW(LPCWSTR, LPCWSTR, DWORD);
BOOL CreateDirectoryW(LPCWSTR, SECURITY_ATTRIBUTES *);
BOOL RemoveDirectoryW(LPCWSTR);
DWORD GetFileAttributesW(LPCWSTR);
DWORD GetEnvironmentVariableW(LPCWSTR, LPWSTR, DWORD);
DWORD GetTempPathW(DWORD, LPWSTR);
UINT GetSystemDirectoryW(LPWSTR, UINT);
DWORD GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
LPWSTR GetCommandLineW(void);
BOOL CreateProcessW(LPCWSTR, LPWSTR, SECURITY_ATTRIBUTES *, SECURITY_ATTRIBUTES *, BOOL, DWORD, LPVOID, LPCWSTR, STARTUPINFOW *, PROCESS_INFORMATION *);
HANDLE CreateToolhelp32Snapshot(DWORD, DWORD);
BOOL Process32FirstW(HANDLE, PROCESSENTRY32W *);
BOOL Process32NextW(HANDLE, PROCESSENTRY32W *);
HRSRC FindResourceW(HMODULE, LPCWSTR, LPCWSTR);
HGLOBAL LoadResource(HMODULE, HRSRC);
LPVOID LockResource(HGLOBAL);
DWORD SizeofResource(HMODULE, HRSRC);
UINT GetPrivateProfileIntW(LPCWSTR, LPCWSTR, INT, LPCWSTR);
DWORD GetPrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR);
BOOL WritePrivateProfileStringW(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR);
LONG RegCreateKeyExW(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, DWORD, SECURITY_ATTRIBUTES *, HKEY *, LPDWORD);
LONG RegOpenKeyExW(HKEY, LPCWSTR, DWORD, DWORD, HKEY *);
LONG RegSetValueExW(HKEY, LPCWSTR, DWORD, DWORD, const BYTE *, DWORD);
LONG RegDeleteValueW(HKEY, LPCWSTR);
LONG RegDeleteTreeW(HKEY, LPCWSTR);
LONG RegCloseKey(HKEY);
LONG InterlockedIncrement(volatile LONG *);
int _wcsicmp(const wchar_t *, const wchar_t *);
int _wcsnicmp(const wchar_t *, const wchar_t *, size_t);
BOOL Shell_NotifyIconW(DWORD, NOTIFYICONDATAW *);
HINSTANCE ShellExecuteW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int);
BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX *);
HRESULT SHGetFolderPathW(HWND, int, HANDLE, DWORD, LPWSTR);
HRESULT CoInitializeEx(LPVOID, DWORD);
void CoUninitialize(void);
HRESULT CoCreateInstance(REFCLSID, void *, DWORD, REFIID, LPVOID *);
extern const CLSID CLSID_ShellLink;
extern const IID IID_IShellLinkW;
extern const IID IID_IPersistFile;
typedef struct IShellLinkW IShellLinkW;
typedef struct IPersistFile IPersistFile;
HRESULT IShellLinkW_SetPath(IShellLinkW *, LPCWSTR);
HRESULT IShellLinkW_SetArguments(IShellLinkW *, LPCWSTR);
HRESULT IShellLinkW_SetWorkingDirectory(IShellLinkW *, LPCWSTR);
HRESULT IShellLinkW_SetDescription(IShellLinkW *, LPCWSTR);
HRESULT IShellLinkW_SetIconLocation(IShellLinkW *, LPCWSTR, int);
HRESULT IShellLinkW_QueryInterface(IShellLinkW *, REFIID, void **);
ULONG IShellLinkW_Release(IShellLinkW *);
HRESULT IPersistFile_Save(IPersistFile *, LPCWSTR, BOOL);
ULONG IPersistFile_Release(IPersistFile *);
#define IDC_ARROW MAKEINTRESOURCEW(32512)
BOOL IsWindowVisible(HWND);
BOOL IsIconic(HWND);
#define MF_GRAYED 1
#define GENERIC_READ 0x80000000L
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_APPEND_DATA 4
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define CP_UTF8 65001
DWORD GetFileSize(HANDLE, LPDWORD);
BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, void *);
HANDLE GetProcessHeap(void);
LPVOID HeapAlloc(HANDLE, DWORD, size_t);
BOOL HeapFree(HANDLE, DWORD, LPVOID);
int MultiByteToWideChar(UINT, DWORD, const char *, int, LPWSTR, int);
int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, char *, int, const char *, BOOL *);
typedef struct tagSIZE { LONG cx, cy; } SIZE;
BOOL GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE *);
BOOL Ellipse(HDC, int, int, int, int);

LONG InterlockedDecrement(volatile LONG *);

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef struct _SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;
typedef enum { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;
void GetLocalTime(SYSTEMTIME *);
BOOL GetFileTime(HANDLE, FILETIME *, FILETIME *, FILETIME *);
LONG CompareFileTime(const FILETIME *, const FILETIME *);
BOOL GetFileAttributesExW(LPCWSTR, GET_FILEEX_INFO_LEVELS, void *);
#define FILE_SHARE_WRITE 2
DWORD GetClipboardSequenceNumber(void);
BOOL OpenClipboard(HWND);
BOOL CloseClipboard(void);
BOOL EmptyClipboard(void);
HANDLE GetClipboardData(UINT);
HANDLE SetClipboardData(UINT, HANDLE);
#define CF_UNICODETEXT 13
typedef HANDLE HGLOBAL;
#define GMEM_MOVEABLE 2
HGLOBAL GlobalAlloc(UINT, size_t);
void *GlobalLock(HGLOBAL);
BOOL GlobalUnlock(HGLOBAL);
HGLOBAL GlobalFree(HGLOBAL);
typedef size_t SIZE_T;
#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_METAFILEPICT 3
#define CF_OEMTEXT 7
#define CF_PALETTE 9
#define CF_ENHMETAFILE 14
#define CF_LOCALE 16
#define CF_OWNERDISPLAY 0x80
#define CF_DSPBITMAP 0x82
#define CF_DSPMETAFILEPICT 0x83
#define CF_DSPENHMETAFILE 0x8E
#define CF_PRIVATEFIRST 0x200
#define CF_GDIOBJLAST 0x3FF
UINT EnumClipboardFormats(UINT);
SIZE_T GlobalSize(HANDLE);
void Sleep(DWORD);
#define WM_QUERYENDSESSION 0x11
#define WM_ENDSESSION 0x16
#define SPI_GETWORKAREA 0x30
BOOL SystemParametersInfoW(UINT, UINT, void *, UINT);
#define MB_DEFBUTTON2 0x100
typedef struct HWINEVENTHOOK__ *HWINEVENTHOOK;
typedef void (CALLBACK *WINEVENTPROC)(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
HWINEVENTHOOK SetWinEventHook(DWORD, DWORD, HMODULE, WINEVENTPROC, DWORD, DWORD, DWORD);
BOOL UnhookWinEvent(HWINEVENTHOOK);
#define EVENT_OBJECT_FOCUS 0x8005
#define WINEVENT_OUTOFCONTEXT 0
#define WINEVENT_SKIPOWNPROCESS 2
#define VK_DELETE 0x2E

/* ---- UI additions (3.2) ---- */
#ifndef SS_LEFT
#define SS_LEFT 0x0
#endif
#ifndef SS_NOPREFIX
#define SS_NOPREFIX 0x80
#endif
#ifndef WS_TABSTOP
#define WS_TABSTOP 0x00010000L
#endif
#ifndef WS_VSCROLL
#define WS_VSCROLL 0x00200000L
#endif
#ifndef WS_CLIPCHILDREN
#define WS_CLIPCHILDREN 0x02000000L
#endif
#ifndef WS_GROUP
#define WS_GROUP 0x00020000L
#endif
#ifndef SW_SHOW
#define SW_SHOW 5
#endif
typedef void *HBITMAP;
typedef void *HMONITOR;
typedef struct { BOOL fIcon; DWORD xHotspot; DWORD yHotspot; HBITMAP hbmMask; HBITMAP hbmColor; } ICONINFO;
typedef struct { LONG bmType; LONG bmWidth; LONG bmHeight; LONG bmWidthBytes; WORD bmPlanes; WORD bmBitsPixel; LPVOID bmBits; } BITMAP;
typedef struct { DWORD biSize; LONG biWidth; LONG biHeight; WORD biPlanes; WORD biBitCount; DWORD biCompression; DWORD biSizeImage; LONG biXPelsPerMeter; LONG biYPelsPerMeter; DWORD biClrUsed; DWORD biClrImportant; } BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
#define BI_RGB 0
#define DIB_RGB_COLORS 0
BOOL GetIconInfo(HICON, ICONINFO *);
int GetObjectW(HANDLE, int, LPVOID);
int GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, BITMAPINFO *, UINT);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
HICON CreateIconIndirect(ICONINFO *);
int GetSystemMetrics(int);
#define SM_CXSMICON 49
#define SM_CYSMICON 50
HANDLE LoadImageW(HINSTANCE, LPCWSTR, UINT, int, int, UINT);
#define IMAGE_ICON 1
#define LR_DEFAULTCOLOR 0
#define LR_SHARED 0x8000
HWND GetFocus(void);
#define WM_MOUSEMOVE 0x0200
#define WM_MOUSELEAVE 0x02A3
typedef struct { DWORD cbSize; DWORD dwFlags; HWND hwndTrack; DWORD dwHoverTime; } TRACKMOUSEEVENT;
#define TME_LEAVE 2
BOOL TrackMouseEvent(TRACKMOUSEEVENT *);
typedef LRESULT (CALLBACK *SUBCLASSPROC)(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT DefSubclassProc(HWND, UINT, WPARAM, LPARAM);
BOOL SetWindowSubclass(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);
BOOL RemoveWindowSubclass(HWND, SUBCLASSPROC, UINT_PTR);
#define BS_TYPEMASK 0xF
int GetDlgCtrlID(HWND);
HMODULE GetModuleHandleW(LPCWSTR);
typedef int (*FARPROC)(void);
FARPROC GetProcAddress(HMODULE, LPCSTR);
typedef struct { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; } MONITORINFO;
BOOL GetMonitorInfoW(HMONITOR, MONITORINFO *);
HMONITOR MonitorFromWindow(HWND, DWORD);
#define MONITOR_DEFAULTTONEAREST 2
BOOL IsDialogMessageW(HWND, MSG *);
BOOL IsChild(HWND, HWND);
#define WM_DPICHANGED 0x02E0
#define ODS_NOACCEL 0x0100
#define ODS_NOFOCUSRECT 0x0200
#define DT_HIDEPREFIX 0x00100000
#define NIF_INFO 0x10
#define NIIF_INFO 1
#define NIIF_RESPECT_QUIET_TIME 0x80
#define NIN_SELECT 0x400
#define NIN_KEYSELECT 0x401
#define WM_CTLCOLORBTN 0x0135
#define WM_UPDATEUISTATE 0x0128
#define WM_CHANGEUISTATE 0x0127

BOOL SetRect(RECT *, int, int, int, int);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
#define SWP_NOSIZE 1
#define SWP_NOMOVE 2
#define SWP_NOZORDER 4
#define SWP_NOACTIVATE 0x10
BOOL GetWindowRect(HWND, RECT *);
HDC CreateCompatibleDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC, int, int);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
#define SRCCOPY 0x00CC0020
BOOL DeleteDC(HDC);
#define ODS_DISABLED 4
#define ODS_FOCUS 0x10
BOOL InflateRect(RECT *, int, int);
BOOL DrawFocusRect(HDC, const RECT *);
#define CBN_SELCHANGE 1
#define BN_CLICKED 0
#define EN_KILLFOCUS 0x200
BOOL DestroyIcon(HICON);

#ifndef KEY_QUERY_VALUE
#define KEY_QUERY_VALUE 1
#endif
LONG RegQueryValueExW(HKEY, LPCWSTR, DWORD *, DWORD *, BYTE *, DWORD *);

BOOL PeekMessageW(MSG *, HWND, UINT, UINT, UINT);
#define PM_NOREMOVE 0
#define PM_REMOVE 1
#define PM_QS_SENDMESSAGE 0x00400000
UINT GetDoubleClickTime(void);
#ifndef VK_F2
#define VK_F2 0x71
#endif
#ifndef VK_PRIOR
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#endif
UINT RealGetWindowClassW(HWND, LPWSTR, UINT);

#define UIS_SET 1
#define UIS_CLEAR 2
#define UISF_HIDEFOCUS 1
#define UISF_HIDEACCEL 2
#ifndef MAKEWPARAM
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)(((WORD)(l)) | ((DWORD)(WORD)(h)) << 16))
#endif
typedef struct { UINT cbSize; DWORD dwFlags; LPWSTR lpszDefaultScheme; } HIGHCONTRASTW;
#define SPI_GETHIGHCONTRAST 0x0042
#define HCF_HIGHCONTRASTON 1
DWORD GetSysColor(int);
HBRUSH GetSysColorBrush(int);
#ifndef COLOR_WINDOWTEXT
#define COLOR_WINDOWTEXT 8
#endif
#define COLOR_BTNFACE 15
#define COLOR_BTNTEXT 18
#define COLOR_HIGHLIGHT 13
#define COLOR_HIGHLIGHTTEXT 14
#define COLOR_GRAYTEXT 17
#define WM_SETTINGCHANGE 0x001A
#define WM_SYSCOLORCHANGE 0x0015
#define WM_THEMECHANGED 0x031A
#define WM_ACTIVATE 0x0006
#define WA_INACTIVE 0
BOOL RedrawWindow(HWND, const RECT *, void *, UINT);
#define RDW_INVALIDATE 1
#define RDW_ERASE 4
#define RDW_ALLCHILDREN 0x80
#define MONITOR_DEFAULTTONULL 0
#define NIF_REALTIME 0x40
#ifndef NIM_DELETE
#define NIM_DELETE 2
#endif
#define TPM_RIGHTBUTTON 2
#ifndef IDOK
#define IDOK 1
#endif
#ifndef BM_CLICK
#define BM_CLICK 0xF5
#endif
#define GET_X_LPARAM(lp) ((int)(short)((DWORD)(lp) & 0xFFFF))
#define GET_Y_LPARAM(lp) ((int)(short)(((DWORD)(lp) >> 16) & 0xFFFF))
BOOL AllowSetForegroundWindow(DWORD);

HWND GetAncestor(HWND, UINT);
#define GA_ROOT 2
UINT RegisterClipboardFormatW(LPCWSTR);

BOOL SetMenuDefaultItem(HMENU, UINT, UINT);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
#define EM_SETLIMITTEXT 0x00C5
#define CBN_CLOSEUP 8
#define CB_GETDROPPEDSTATE 0x0157

BOOL CopyFileW(LPCWSTR, LPCWSTR, BOOL);
#define CBN_DROPDOWN 7

typedef void *HLOCAL_;
LPWSTR *CommandLineToArgvW(LPCWSTR, int *);
HLOCAL_ LocalFree(HLOCAL_);

#define ERROR_INVALID_HOOK_HANDLE 1404L
HWND GetClipboardOwner(void);
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2L
#endif


#ifndef MB_ERR_INVALID_CHARS
#define MB_ERR_INVALID_CHARS 8
#endif
#ifndef CP_ACP
#define CP_ACP 0
#endif
BOOL SetFileAttributesW(LPCWSTR, DWORD);
typedef union { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;
BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *);
DWORD SetFilePointer(HANDLE, LONG, LONG *, DWORD);
#define FILE_END 2
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define MB_SETFOREGROUND 0x00010000L
#define MB_TOPMOST 0x00040000L
#ifndef EVENT_OBJECT_STATECHANGE
#define EVENT_OBJECT_STATECHANGE 0x800A
#endif
BOOL QueryPerformanceCounter(LARGE_INTEGER *);
DWORD GetCurrentProcessId(void);
typedef struct _WIN32_FIND_DATAW { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1; wchar_t cFileName[260]; wchar_t cAlternateFileName[14]; } WIN32_FIND_DATAW;
HANDLE FindFirstFileW(LPCWSTR, WIN32_FIND_DATAW *);
BOOL FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
BOOL FindClose(HANDLE);
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#endif
#define CB_RESETCONTENT 0x014B
#define CB_GETITEMDATA 0x0150
#define CB_SETITEMDATA 0x0151
#define SS_CENTER 0x00000001L
#define CB_SETDROPPEDWIDTH 0x0160
#define CBN_KILLFOCUS 4
#endif
