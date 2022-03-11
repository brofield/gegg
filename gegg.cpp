/*
Copyright (c) 2008 Brodie Thiesfield

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#define WIN32_LEAN_AND_MEAN

#define _WIN32_WINNT    0x0600  // enable Vista features, but we should work with everything
#define WINVER          0x0600  // enable Vista features, but we should work with everything
#define _WIN32_IE       0x0500  // Internet Explorer 5.0

#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <tchar.h>
#include <stdio.h> 
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#include "resource.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

#define TIMERID_COUNTDOWN       1
#define TIMERID_FLASH           2
#define TIMERID_SOUND           3

#define TIMERDELAY_FLASH        500
#define TIMERDELAY_SOUND        10*1000

#define WM_GEGG_TRAYMSG         (WM_USER + 1)

#define ARRAYLEN(x)             (sizeof(x)/sizeof((x)[0]))

#define REGKEY_GEGG             _T("SOFTWARE\\Jellycan\\Gegg")
#define REGVAL_DURATION         _T("Duration")
#define REGVAL_ALARM_MSGBOX     _T("AlarmMsgBox")
#define REGVAL_ALARM_FLASH      _T("AlarmFlash")
#define REGVAL_ALARM_SOUND      _T("AlarmSound")
#define REGVAL_USE_TRAY         _T("MinimizeToTray")
#define REGVAL_DIGITAL          _T("DisplayDigital")

#define APPTITLE                _T("The Good Egg")
#define APPLINK                 _T("http://code.jellycan.com/")
#define ABOUTTITLE              _T("About The Good Egg")

#define MSG_STARTBUTTON         _T("Start")
#define MSG_STOPBUTTON          _T("Stop")
#define MSG_ALARM               _T("Time to go.")
#define MSG_INVALIDTIME         _T("Invalid duration")

#define FLASH_COLOR             RGB(255,0,0)


// globals
HINSTANCE   g_hInstance = NULL;
HWND        g_hwnd = NULL;
DWORD       g_dwTimerStart = 0;     // 0 if not timing
DWORD       g_dwTimerDuration = 0;
BOOL        g_bShowRed = FALSE;
HBRUSH      g_brush = NULL;
BOOL        g_bIsAlarmSounding = TRUE;
int         g_nSoundCount = 0;
BOOL        g_bMinimizedToTray = FALSE;
RECT        g_rcLastPos;
BOOL        g_bEnableAnimation = TRUE;
BOOL        g_bClientAreaAnimation = TRUE;
UINT        WM_TASKBARCREATED = 0;

// controls
const static int g_rgControl[] = {
    IDC_TYPE, IDC_TIME, IDC_MSGBOX, IDC_SOUND, IDC_USETRAY, IDC_DIGITAL
};

// forward dec
BOOL OnTimer(WPARAM wParam, LPARAM lParam);
void StopTiming();
void UpdateTrayIconTooltip(LPCTSTR pszTitle);
void ShowAboutBox();
void RestoreFromTray();
void StopSoundingAlarm();
DWORD GetTimerDuration();
void SetDurationText(DWORD dwDuration, bool bAddToTitleBar);


// ----------------------------------------------------------------------------
// Preferences

bool IsChecked(int id) {
    return SendDlgItemMessage(g_hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SavePreferences() {
    HKEY hkey = NULL;
    LONG rc = RegCreateKeyEx(
        HKEY_CURRENT_USER, REGKEY_GEGG, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (rc != ERROR_SUCCESS) return;

    DWORD dwValue;

    dwValue = g_dwTimerDuration ? g_dwTimerDuration : GetTimerDuration();
    RegSetValueEx(hkey, REGVAL_DURATION, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_MSGBOX) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_MSGBOX, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_SOUND) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_SOUND, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_USETRAY) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_USE_TRAY, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));
    
    dwValue = IsChecked(IDC_DIGITAL) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_DIGITAL, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    RegCloseKey(hkey);
}

void LoadPreferences() {
    HKEY hkey = NULL;
    LONG rc = RegOpenKeyEx(
        HKEY_CURRENT_USER, REGKEY_GEGG, 0, KEY_ALL_ACCESS, &hkey);
    if (rc != ERROR_SUCCESS) return;

    DWORD dwValue, dwLen, dwType;

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_DURATION, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        g_dwTimerDuration = dwValue;
        SetDurationText(dwValue, false);
    }

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_ALARM_MSGBOX, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        SendDlgItemMessage(g_hwnd, IDC_MSGBOX, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_ALARM_SOUND, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        SendDlgItemMessage(g_hwnd, IDC_SOUND, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_USE_TRAY, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        SendDlgItemMessage(g_hwnd, IDC_USETRAY, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_DIGITAL, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        SendDlgItemMessage(g_hwnd, IDC_DIGITAL, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    RegCloseKey(hkey);
}

// ----------------------------------------------------------------------------
// Timing and Alarms

DWORD GetTimerDuration() {
    TCHAR szTime[50] = { 0 };
    if (!GetDlgItemText(g_hwnd, IDC_TIME, szTime, ARRAYLEN(szTime))) {
        return 0;
    }
    int nType = SendDlgItemMessage(g_hwnd, IDC_TYPE, CB_GETCURSEL, 0, 0);

    // trim space
    TCHAR * pTime = szTime;
    int nHours = -1, nMins = -1, nSecs = -1;
    while (*pTime) {
        while (*pTime && !_istdigit(*pTime)) ++pTime;
        if (!*pTime) break;

        int nCurr = _tcstoul(pTime, &pTime, 10);
        while (*pTime && _istspace(*pTime)) ++pTime;

        switch (*pTime) {
        case _T('h'):
        case _T('H'):
            nHours = nCurr;
            break;

        case _T('m'):
        case _T('M'):
            nMins = nCurr;
            break;

        case _T('s'):
        case _T('S'):
            nSecs = nCurr;
            break;

        case _T(':'):
        case _T('.'):
        case 0:
            if (nSecs == -1) {
                nSecs = nCurr;
            }
            else if (nMins == -1) {
                nMins = nSecs;
                nSecs = nCurr;
            }
            else if (nHours == -1) {
                nHours = nMins;
                nMins = nSecs;
                nSecs = nCurr;
            }
            else {
                return 0;
            }
            break;
        }
    }

    // delay
    if (nType == 0) { 
        // zero out the unset items
        if (nHours == -1) nHours = 0;
        if (nMins == -1)  nMins = 0;
        if (nSecs == -1)  nSecs = 0;

        return max((nHours * 3600 + nMins * 60 + nSecs) * 1000, 1);
    }

    // until

    time_t tNow = time(NULL);
    struct tm t;
    localtime_s(&t, &tNow);

    // allow hms, hm, h, hh:mm or hh:mm:ss.
    if (nHours == -1) { // hh:mm
        if (nMins == -1 || nSecs == -1) return 0;
        nHours = nMins;
        nMins  = nSecs;
        nSecs  = 0;
    }
    else { // hms, hm, h, hh:mm:ss
        if (nMins == -1)  nMins = 0;
        if (nSecs == -1)  nSecs = 0;
    }

    t.tm_hour = nHours;
    t.tm_min  = nMins;
    t.tm_sec  = nSecs;
    time_t tUntil = mktime(&t);
    while (tUntil < tNow) {
        tUntil += (12 * 60 * 60); // add 12 hours until we get to a time in the future
    }

    SendDlgItemMessage(g_hwnd, IDC_TYPE, CB_SETCURSEL, 0, 0);
    return (DWORD) max((tUntil - tNow) * 1000, 1);
}

void SetDurationText(DWORD dwDuration, bool bAddToTitleBar) 
{
    bool bUsingDigital = IsChecked(IDC_DIGITAL);

    TCHAR szTemp[50], szBuf[250] = { 0 };

    DWORD dwHours = (dwDuration / 1000) / 3600;
    DWORD dwMins = ((dwDuration / 1000) % 3600) / 60;
    DWORD dwSecs  = (dwDuration / 1000) % 60;

    if (bUsingDigital) {
        if (dwHours) {
            _sntprintf_s(szBuf, ARRAYLEN(szBuf), _T("%d:%02d:%02d"), dwHours, dwMins, dwSecs);
        }
        else {
            _sntprintf_s(szBuf, ARRAYLEN(szBuf), _T("%d:%02d"), dwMins, dwSecs);
        }
    }
    else {
        if (dwHours) {
            _sntprintf_s(szTemp, ARRAYLEN(szTemp), _T("%dh"), dwHours);
            _tcscat_s(szBuf, szTemp);
        }
        if (dwMins) {
            if (szBuf[0]) _tcscat_s(szBuf, _T(" "));
            _sntprintf_s(szTemp, ARRAYLEN(szTemp), _T("%dm"), dwMins);
            _tcscat_s(szBuf, szTemp);
        }
        if (dwSecs || !szBuf[0]) {
            if (szBuf[0]) _tcscat_s(szBuf, _T(" "));
            _sntprintf_s(szTemp, ARRAYLEN(szTemp), _T("%ds"), dwSecs);
            _tcscat_s(szBuf, szTemp);
        }
    }

    SetDlgItemText(g_hwnd, IDC_TIME, szBuf);

    if (bAddToTitleBar) {
        _tcscat_s(szBuf, _T(" - "));
        _tcscat_s(szBuf, APPTITLE);
        SetWindowText(g_hwnd, szBuf);
    }
    else {
        SetWindowText(g_hwnd, APPTITLE);
    }
    
    if (g_bMinimizedToTray) {
        UpdateTrayIconTooltip(szBuf);
    }
}

void EnableControls(BOOL bEnable)
{
    HWND hwndCtrl;
    for (int n = 0; n < ARRAYLEN(g_rgControl); ++n) {
        hwndCtrl = GetDlgItem(g_hwnd, g_rgControl[n]);
        EnableWindow(hwndCtrl, bEnable);
    }
}

void CenterWindow(HWND hwnd)
{
    // Get the owner window and dialog box rectangles
    HWND hwndOwner;
    if ((hwndOwner = GetParent(hwnd)) == NULL) {
        hwndOwner = GetDesktopWindow(); 
    }

    RECT rcOwner, rcDlg, rc;
    GetWindowRect(hwndOwner, &rcOwner); 
    GetWindowRect(hwnd, &rcDlg); 
    CopyRect(&rc, &rcOwner); 

    // Offset the owner and dialog box rectangles so that right and bottom 
    // values represent the width and height, and then offset the owner again 
    // to discard space taken up by the dialog box. 
    OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top); 
    OffsetRect(&rc, -rc.left, -rc.top); 
    OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom); 

    // The new position is the sum of half the remaining space and the owner's 
    // original position. 
    SetWindowPos(hwnd, 
        HWND_TOP, 
        rcOwner.left + (rc.right / 2), 
        rcOwner.top + (rc.bottom / 2), 
        0, 0,          // Ignores size arguments. 
        SWP_NOSIZE); 
}

void EnableControlNotifications(HWND hwnd)
{
    DWORD dwStyle;
    HWND hwndCtrl;
    for (int n = 0; n < ARRAYLEN(g_rgControl); ++n) {
        // subclass all dialog items so that we can set their background
        hwndCtrl = GetDlgItem(hwnd, g_rgControl[n]);
        dwStyle = GetWindowLong(hwndCtrl, GWL_STYLE);
        SetWindowLong(hwndCtrl, GWL_STYLE, dwStyle | SS_NOTIFY);
    }
}

void StartTiming() {
    g_dwTimerDuration = GetTimerDuration();
    if (!g_dwTimerDuration) {
        MessageBox(g_hwnd, MSG_INVALIDTIME, APPTITLE, MB_ICONERROR | MB_OK);
        return;
    }
    g_dwTimerStart = GetTickCount();

    SavePreferences();

    SetDlgItemText(g_hwnd, IDC_START, MSG_STOPBUTTON);
    EnableControls(FALSE);

    OnTimer(TIMERID_COUNTDOWN, 0); // will start the timer
}

void StopTiming() {
    RestoreFromTray();
    KillTimer(g_hwnd, TIMERID_COUNTDOWN);
    SetDlgItemText(g_hwnd, IDC_START, MSG_STARTBUTTON);
    EnableControls(TRUE);
    SetDurationText(g_dwTimerDuration, false);
    g_dwTimerStart = 0;
    g_dwTimerDuration = 0;
    PostMessage(g_hwnd, WM_NEXTDLGCTL, (WPARAM) GetDlgItem(g_hwnd, IDC_TIME), TRUE);
}

void StartSoundingAlarm()
{
    RestoreFromTray();

    KillTimer(g_hwnd, TIMERID_COUNTDOWN);
    EnableControls(TRUE);
    SetDurationText(0, true);

    SetDlgItemText(g_hwnd, IDC_START, MSG_STOPBUTTON);
    g_bIsAlarmSounding = TRUE;

    // start dialog flashing
    OnTimer(TIMERID_FLASH, 0);

    if (IsChecked(IDC_SOUND)) {
        g_nSoundCount = 0;
        OnTimer(TIMERID_SOUND, 0); 
    }

    if (IsChecked(IDC_MSGBOX)) {
        ShowWindow(g_hwnd, SW_RESTORE);
        MessageBox(g_hwnd, MSG_ALARM, APPTITLE, MB_ICONEXCLAMATION | MB_OK);
        StopSoundingAlarm();
    }
}

void StopSoundingAlarm()
{
    if (!g_bIsAlarmSounding) {
        return;
    }

    g_bIsAlarmSounding = FALSE;

    KillTimer(g_hwnd, TIMERID_FLASH);
    KillTimer(g_hwnd, TIMERID_SOUND);

    // stop the taskbar flash
    FLASHWINFO fwi;
    fwi.cbSize = sizeof(fwi);
    fwi.hwnd = g_hwnd;
    fwi.dwFlags = FLASHW_STOP;
    fwi.dwTimeout = 0;
    fwi.uCount = 0;
    FlashWindowEx(&fwi);

    // get rid of the red
    g_bShowRed = FALSE;

    RECT rcClient;
    GetClientRect(g_hwnd, &rcClient);
    InvalidateRect(g_hwnd, &rcClient, TRUE);

    SetDlgItemText(g_hwnd, IDC_START, MSG_STARTBUTTON);

    StopTiming();
}

// ----------------------------------------------------------------------------
// Tray Icon

void AddTrayIcon()
{
    NOTIFYICONDATA nid;
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = IDI_GEGG;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_GEGG_TRAYMSG;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_GEGG));
    _tcscpy_s(nid.szTip, APPTITLE);

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void MinimizeToTray()
{
    AddTrayIcon();

    if (g_bEnableAnimation) {
        RECT rcTray;
        HWND hTray = FindWindow("Shell_TrayWnd", NULL);
        if (hTray) hTray = FindWindowEx(hTray, NULL, "TrayNotifyWnd", NULL);
        if (hTray) {
            GetWindowRect(hTray, &rcTray);
        }
        else {
            GetWindowRect(GetDesktopWindow(), &rcTray);
            rcTray.top  = rcTray.bottom - 20;
            rcTray.left = rcTray.right  - 20;
        }

        GetWindowRect(g_hwnd, &g_rcLastPos);

        DrawAnimatedRects(g_hwnd, IDANI_CAPTION, &g_rcLastPos, &rcTray);
    }

    ShowWindow(g_hwnd, SW_HIDE);

    g_bMinimizedToTray = TRUE;
}

void RestoreFromTray()
{
    if (!g_bMinimizedToTray) return;
    g_bMinimizedToTray = FALSE;

    ANIMATIONINFO ai =  { 0 };
    ai.cbSize = sizeof(ai);
    SystemParametersInfo(SPI_GETANIMATION, sizeof(ai), &ai, 0);
    if (ai.iMinAnimate) {
        RECT rcTray;
        HWND hTray = FindWindow("Shell_TrayWnd", NULL);
        if (hTray) hTray = FindWindowEx(hTray, NULL, "TrayNotifyWnd", NULL);
        if (hTray) {
            GetWindowRect(hTray, &rcTray);
        }
        else {
            GetWindowRect(GetDesktopWindow(), &rcTray);
            rcTray.top  = rcTray.bottom - 20;
            rcTray.left = rcTray.right  - 20;
        }

        DrawAnimatedRects(g_hwnd, IDANI_CAPTION, &rcTray, &g_rcLastPos);
    }

    ShowWindow(g_hwnd, SW_SHOW);

    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = IDI_GEGG;

    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void UpdateTrayIconTooltip(LPCTSTR pszTitle)
{
    NOTIFYICONDATA nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = IDI_GEGG;
    nid.uFlags = NIF_TIP;
    _tcscpy_s(nid.szTip, pszTitle);

    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// ----------------------------------------------------------------------------
// Message Handlers

BOOL OnInitDialog(HWND hwnd, WPARAM wParam) 
{
    // attach icon to main dialog
    HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_GEGG));
    SendMessage(hwnd, WM_SETICON, WPARAM(ICON_SMALL), LPARAM(hIcon));
    SendMessage(hwnd, WM_SETICON, WPARAM(ICON_BIG),   LPARAM(hIcon));

    CenterWindow(hwnd);

    EnableControlNotifications(hwnd);

	// IDM_ABOUTBOX can't be a system message
	assert((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	assert(IDM_ABOUTBOX < 0xF000);

    HMENU hMenu = GetSystemMenu(hwnd, FALSE);
	if (hMenu) {
		AppendMenu(hMenu, MF_SEPARATOR, NULL, NULL);
		AppendMenu(hMenu, MF_STRING, IDM_ABOUTBOX, ABOUTTITLE _T(" ..."));
	}

    // set the data items into the combobox
    SendDlgItemMessage(hwnd, IDC_TYPE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hwnd, IDC_TYPE, CB_ADDSTRING, 0, (LPARAM) _T("Delay"));
    SendDlgItemMessage(hwnd, IDC_TYPE, CB_ADDSTRING, 0, (LPARAM) _T("Until"));
    SendDlgItemMessage(hwnd, IDC_TYPE, CB_SETCURSEL, 0, 0);

    // set focus to control
    if (GetDlgCtrlID((HWND) wParam) != IDC_TIME) { 
        SetFocus(GetDlgItem(hwnd, IDC_TIME)); 
        return FALSE; 
    } 

    return TRUE; 
}

BOOL OnTimer(WPARAM wParam, LPARAM lParam) 
{
    if (wParam == TIMERID_COUNTDOWN) {
        DWORD dwDuration = GetTickCount() - g_dwTimerStart;
        if (dwDuration >= g_dwTimerDuration) {
            StartSoundingAlarm();
        }
        else {
            DWORD dwRemaining = g_dwTimerDuration - dwDuration;
            dwDuration = min(dwRemaining, 1000);
            SetTimer(g_hwnd, TIMERID_COUNTDOWN, dwDuration, NULL);
            SetDurationText(dwRemaining + 999, true); // round up
        }

        return TRUE;
    }

    if (wParam == TIMERID_FLASH) {
        g_bShowRed = !g_bShowRed;

        RECT rcClient;
        GetClientRect(g_hwnd, &rcClient);
        InvalidateRect(g_hwnd, &rcClient, TRUE);
        
        FLASHWINFO fwi;
        fwi.cbSize = sizeof(fwi);
        fwi.hwnd = g_hwnd;
        fwi.dwFlags = FLASHW_ALL;
        fwi.dwTimeout = 1000;
        fwi.uCount = 1;
        FlashWindowEx(&fwi);

        if (g_bClientAreaAnimation) {
            SetTimer(g_hwnd, TIMERID_FLASH, TIMERDELAY_FLASH, NULL);
        }

        return TRUE;
    }

    if (wParam == TIMERID_SOUND) {
        ++g_nSoundCount;

        switch (g_nSoundCount) {
        case 1:
            PlaySound(NULL, NULL, SND_NODEFAULT);
            PlaySound((LPCTSTR) IDR_AHEM, g_hInstance, SND_RESOURCE | SND_NODEFAULT | SND_ASYNC);
            SetTimer(g_hwnd, TIMERID_SOUND, TIMERDELAY_SOUND, NULL);
            break;

        case 2:
            PlaySound(NULL, NULL, SND_NODEFAULT);
            PlaySound((LPCTSTR) IDR_AHEM, g_hInstance, SND_RESOURCE | SND_NODEFAULT | SND_ASYNC);
            SetTimer(g_hwnd, TIMERID_SOUND, 500, NULL); // play the next ahem soon after
            break;

        case 3:
            // don't clear the sound
            PlaySound((LPCTSTR) IDR_AHEM, g_hInstance, SND_RESOURCE | SND_NODEFAULT | SND_ASYNC);
            SetTimer(g_hwnd, TIMERID_SOUND, TIMERDELAY_SOUND * 2, NULL);
            break;

        case 4:
            KillTimer(g_hwnd, TIMERID_SOUND);
            PlaySound(NULL, NULL, SND_NODEFAULT);
            PlaySound((LPCTSTR) IDR_HELLO, g_hInstance, SND_RESOURCE | SND_NODEFAULT | SND_ASYNC);
            break;

        default:
            KillTimer(g_hwnd, TIMERID_SOUND);
        }

        return TRUE;
    }

    return FALSE;
}

BOOL OnEnter() 
{
    DWORD id = GetDlgCtrlID(GetFocus());
    if (id == IDC_TYPE || id == IDC_TIME) {
        SendMessage(g_hwnd, WM_NEXTDLGCTL, 0, 0);
        return TRUE;
    }
    
    return FALSE;
}

BOOL OnCommand(WPARAM wParam, LPARAM lParam) 
{
    DWORD dwNotify  = HIWORD(wParam);
    DWORD dwControl = LOWORD(wParam);

    // hit the ENTER key
    if (wParam == IDOK) {
        return OnEnter();
    }

    switch (dwControl) {
    case IDC_TIME:
        if (dwNotify == EN_SETFOCUS) {
            SendDlgItemMessage(g_hwnd, dwControl, EM_SETSEL, 0, -1);
            return TRUE;
        }
        return FALSE;

    case IDC_START:
        if (g_bIsAlarmSounding) {
            StopSoundingAlarm();
        }
        else if (g_dwTimerStart && g_dwTimerDuration) {
            StopTiming();
        }
        else {
            StartTiming();
        }
        return TRUE;

    case IDC_DIGITAL:
        SetDurationText(GetTimerDuration(), g_dwTimerStart != 0);
        return FALSE;

    case IDOK:
        return FALSE;

    default:
        return FALSE;
    }
}

BOOL OnEraseBackground(HDC hDC, bool bIsControl)
{
    if (bIsControl) {
	    SetBkColor(hDC, FLASH_COLOR);
        return (BOOL) (g_bShowRed ? g_brush : FALSE);
    }

    if (g_bShowRed) {
        RECT rcClient;
        GetClientRect(g_hwnd, &rcClient);
        FillRect(hDC, &rcClient, g_brush);
	    SetBkColor(hDC, FLASH_COLOR);
        return TRUE;
    }

    return FALSE;
}

BOOL OnTrayMessage(WPARAM wParam, LPARAM lParam) 
{
    UINT uIconId   = (UINT) wParam; // icon ID sending the message
    UINT uMouseMsg = (UINT) lParam; // mouse event
    if (uIconId == IDI_GEGG && (
        uMouseMsg == WM_LBUTTONUP || uMouseMsg == WM_LBUTTONDBLCLK ||
        uMouseMsg == WM_RBUTTONUP || uMouseMsg == WM_RBUTTONDBLCLK 
        )) 
    {
        RestoreFromTray();
        return TRUE;
    }
    return FALSE;
}

BOOL OnSysCommand(WPARAM wParam, LPARAM lParam)
{
    if (wParam == SC_MINIMIZE) {
        if (IsChecked(IDC_USETRAY)) {
            MinimizeToTray();
            SetWindowLong(g_hwnd, DWL_MSGRESULT, 0);
            return TRUE;
        }
    }

	if ((wParam & 0xFFF0) == IDM_ABOUTBOX) {
        ShowAboutBox();
        return TRUE;
	}

    return FALSE;
}

INT_PTR CALLBACK GeggDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) 
{ 
    switch (message) { 
    case WM_INITDIALOG:
        return OnInitDialog(hwnd, wParam);

    case WM_ERASEBKGND:
        return OnEraseBackground((HDC) wParam, false);

    case WM_CTLCOLORSTATIC:
        return OnEraseBackground((HDC) wParam, true);

    case WM_COMMAND:
        return OnCommand(wParam, lParam);

    case WM_TIMER:
        return OnTimer(wParam, lParam);

    case WM_SETFOCUS:
    case WM_LBUTTONUP:
    case WM_NCLBUTTONUP:
        if (g_bIsAlarmSounding) {
            StopSoundingAlarm();
        }
        return FALSE;

    case WM_SYSCOMMAND:
        return OnSysCommand(wParam, lParam);

    case WM_GEGG_TRAYMSG:
        return OnTrayMessage(wParam, lParam);

    case WM_CLOSE:
        SavePreferences();
        DestroyWindow(hwnd);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;

    default: 
        if (message == WM_TASKBARCREATED) {
            AddTrayIcon();
            return TRUE;
        }
        return FALSE; 
    } 
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int iCmdShow)
{
    g_hInstance = hInstance;

    WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));

    ANIMATIONINFO ai;
    ai.cbSize = sizeof(ai);
    ai.iMinAnimate = 1; // default on
    SystemParametersInfo(SPI_GETANIMATION, sizeof(ai), &ai, 0);
    g_bEnableAnimation = (ai.iMinAnimate != 0);

    g_bClientAreaAnimation = TRUE;
#ifdef SPI_GETCLIENTAREAANIMATION
    SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 
        sizeof(g_bClientAreaAnimation), &g_bClientAreaAnimation, 0);
#endif

    g_brush = CreateSolidBrush(FLASH_COLOR);

    g_hwnd = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_GEGG), NULL, GeggDialogProc);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOW);

    SetDlgItemText(g_hwnd, IDC_TIME, _T("3 mins"));

    LoadPreferences();

    SendDlgItemMessage(g_hwnd, IDC_TIME, EM_SETSEL, 0, -1);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) { 
        if (!IsDialogMessage(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DeleteObject(g_brush);

    return 0;
}

// ----------------------------------------------------------------------------
// About Dialog

#define PROP_ORIGINAL_PROC		TEXT("HyperlinkOrigProc")
#define PROP_ORIGINAL_FONT		TEXT("HyperlinkOrigFont")
#define PROP_UNDERLINE_FONT		TEXT("HyperlinkUnderlineFont")

LRESULT CALLBACK _HyperlinkProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC pfnOrigProc = (WNDPROC) GetProp(hwnd, PROP_ORIGINAL_PROC);

    switch (message)
    {
    case WM_SETCURSOR:
        {
            // since IDC_HAND is not available on all operating systems,
            // we will load the arrow cursor if IDC_HAND is not present
            HCURSOR hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_HAND));
            if (!hCursor) {
                hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW));
            }
            SetCursor(hCursor);
        }
        return TRUE;

    case WM_MOUSEMOVE:
        if (GetCapture() != hwnd) {
            HFONT hFont = (HFONT) GetProp(hwnd, PROP_UNDERLINE_FONT);
            SendMessage(hwnd, WM_SETFONT, (WPARAM) hFont, FALSE);
            InvalidateRect(hwnd, NULL, FALSE);
            SetCapture(hwnd);
        }
        else {
            RECT rect;
            GetWindowRect(hwnd, &rect);

            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ClientToScreen(hwnd, &pt);

            if (!PtInRect(&rect, pt)) {
                HFONT hFont = (HFONT) GetProp(hwnd, PROP_ORIGINAL_FONT);
                SendMessage(hwnd, WM_SETFONT, (WPARAM) hFont, FALSE);
                InvalidateRect(hwnd, NULL, FALSE);
                ReleaseCapture();
            }
        }
        break;

    case WM_DESTROY:
        {
            SetWindowLong(hwnd, GWL_WNDPROC, (LONG) pfnOrigProc);
            RemoveProp(hwnd, PROP_ORIGINAL_PROC);

            HFONT hOrigFont = (HFONT) GetProp(hwnd, PROP_ORIGINAL_FONT);
            SendMessage(hwnd, WM_SETFONT, (WPARAM) hOrigFont, 0);
            RemoveProp(hwnd, PROP_ORIGINAL_FONT);

            HFONT hFont = (HFONT) GetProp(hwnd, PROP_UNDERLINE_FONT);
            DeleteObject(hFont);
            RemoveProp(hwnd, PROP_UNDERLINE_FONT);
        }
        break;
    }

    return CallWindowProc(pfnOrigProc, hwnd, message, wParam, lParam);
}

BOOL ConvertStaticToHyperlink(HWND hwndParent, UINT uCtrlId)
{
    HWND hwndCtl = GetDlgItem(hwndParent, uCtrlId);

    // make sure the control will send notifications
    DWORD dwStyle = GetWindowLong(hwndCtl, GWL_STYLE);
    SetWindowLong(hwndCtl, GWL_STYLE, dwStyle | SS_NOTIFY);

    // subclass the existing control
    WNDPROC pfnOrigProc = (WNDPROC) GetWindowLong(hwndCtl, GWL_WNDPROC);
    SetProp(hwndCtl, PROP_ORIGINAL_PROC, (HANDLE) pfnOrigProc);
    SetWindowLong(hwndCtl, GWL_WNDPROC, (LONG) (WNDPROC) _HyperlinkProc);

    // create an updated font by adding an underline
    HFONT hOrigFont = (HFONT) SendMessage(hwndCtl, WM_GETFONT, 0, 0);
    SetProp(hwndCtl, PROP_ORIGINAL_FONT, (HANDLE) hOrigFont);

    LOGFONT lf;
    GetObject(hOrigFont, sizeof(lf), &lf);
    lf.lfUnderline = TRUE;

    HFONT hFont = CreateFontIndirect(&lf);
    SetProp(hwndCtl, PROP_UNDERLINE_FONT, (HANDLE) hFont);

    return TRUE;
}

INT_PTR CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) 
{
    const char szHelpText[] =
        "To set a countdown timer for a period of time, select \"Delay\" and "
        "then enter the desired period.\n"
        "\n"
        "To set a countdown timer until a specific time of day, select \"Until\" "
        "and then enter the desired time for the timer to expire. The required "
        "delay will be automatically calculated.\n"
        "\n"
        "Periods can be entered using either separated time (hms) or in "
        "digital time (HH:MM:SS). For example:\n"
        "\n"
        "PERIOD\t\t\t"        "Using HMS\t"     "Using HH:MM:SS\n"
        "30 secs\t\t\t"       "\"30s\"\t\t"     "\"30\"\n"
        "3 mins\t\t\t"        "\"3m\"\t\t"      "\"3:00\"\n"
        "1 hour 5 mins\t\t"   "\"1h 5m\"\t\t"   "\"1:05:00\"\n"
        ;

    switch (message) { 
    case WM_INITDIALOG:
        {
            HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_GEGG));
            SendMessage(hwnd, WM_SETICON, WPARAM(ICON_SMALL), LPARAM(hIcon));
            SendMessage(hwnd, WM_SETICON, WPARAM(ICON_BIG),   LPARAM(hIcon));

            SetWindowText(hwnd, APPTITLE);
            SetDlgItemText(hwnd, IDC_APPLINK, APPLINK);
            ConvertStaticToHyperlink(hwnd, IDC_APPLINK);

            SetDlgItemText(hwnd, IDC_HELPTEXT, szHelpText);

            CenterWindow(hwnd);
        }
        return TRUE;

    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDCANCEL) {
            EndDialog(hwnd, 0);
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_APPLINK) {
            ShellExecute(hwnd, _T("open"), APPLINK, NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(hwnd, 0);
        return TRUE;

    default:
        return FALSE;
    }
}

void ShowAboutBox()
{
    DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUT), g_hwnd, AboutDialogProc);
}

