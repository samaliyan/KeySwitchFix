#!/usr/bin/env python3
"""Writes a do-nothing definition for every function tests/win32sim declares.

Each one is weak, so tests/app_sim.c replaces the ones it needs (keyboard
layouts, SendInput, the clock, files). Usage: generate_weak.py header... > out.c
"""
import re
import sys
out=['/* Generated: weak no-op definitions for every function the Win32 stub declares. */',
     '#include <windows.h>','#include <oleacc.h>','#include <commctrl.h>','#include <shellapi.h>','#include <windowsx.h>']
# Real behaviour for the few calls whose do-nothing result would mislead
# the program (every string "equal", errors never set).
SPECIAL = {"_wcsicmp", "_wcsnicmp", "GetLastError", "SetLastError"}
out.append('''
static wchar_t sim_fold(wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c; }
__attribute__((weak)) int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n) {
    for (; n; --n, ++a, ++b) {
        wchar_t x = sim_fold(*a), y = sim_fold(*b);
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    return 0;
}
__attribute__((weak)) int _wcsicmp(const wchar_t *a, const wchar_t *b) { return _wcsnicmp(a, b, (size_t)-1); }
static DWORD sim_last_error;
__attribute__((weak)) DWORD GetLastError(void) { return sim_last_error; }
__attribute__((weak)) void SetLastError(DWORD error) { sim_last_error = error; }
const CLSID CLSID_ShellLink;
const IID IID_IShellLinkW;
const IID IID_IPersistFile;
''')
seen=set(SPECIAL)
for path in sys.argv[1:]:
    for line in open(path):
        line=line.strip()
        if line.startswith(('typedef','#','/*','}')) or '{' in line: continue
        m=re.match(r'^([A-Za-z_][\w \*]*?[\s\*])(\w+)\s*\((.*)\);$',line)
        if not m: continue
        ret,name,args=m.group(1).strip(),m.group(2),m.group(3).strip()
        if name in seen: continue
        seen.add(name)
        params=[]
        depth=0;cur=''
        for ch in args:
            if ch=='(' : depth+=1
            if ch==')': depth-=1
            if ch==',' and depth==0: params.append(cur.strip());cur=''
            else: cur+=ch
        if cur.strip(): params.append(cur.strip())
        if params==['void']: params=[]
        named=[]
        variadic=False
        for i,p in enumerate(params):
            if p=='...': named.append('...');variadic=True;continue
            if '(*)' in p: named.append(p.replace('(*)','(*a%d)'%i))
            else: named.append('%s a%d'%(p,i))
        body = '' if ret=='void' else ' return (%s)0;'%ret
        out.append('__attribute__((weak)) %s %s(%s) {%s%s }'%(ret,name,', '.join(named) if named else 'void',
            ''.join(' (void)a%d;'%i for i,p in enumerate(params) if p!='...'), body))
print('\n'.join(out))
