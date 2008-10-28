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

#define APPTITLE                _T("The Good Egg")
#define APPTITLE_FORMAT         _T("%u:%02u - The Good Egg")

#define MSG_STARTBUTTON         _T("Start")
#define MSG_STOPBUTTON          _T("Stop")
#define MSG_ALARM               _T("Time to go.")
#define MSG_INVALIDTIME         _T("Invalid duration")

#define FLASH_COLOR             RGB(255,0,0)


// globals
HINSTANCE   g_hInstance = NULL;
HWND        g_hwnd = NULL;
DWORD       g_dwTimerStart = 0;
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
    IDC_MINS, IDC_SECS, IDC_MSGBOX, IDC_SOUND, IDC_USETRAY
};

// forward dec
BOOL OnTimer(WPARAM wParam, LPARAM lParam);
void OnStopTiming();
void UpdateTrayIconTooltip(LPCTSTR pszTitle);



bool IsChecked(int id) {
    return SendDlgItemMessage(g_hwnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

DWORD GetTimerDuration() {
    BOOL bSuccess = TRUE;
    DWORD dwDuration = GetDlgItemInt(g_hwnd, IDC_MINS, &bSuccess, FALSE) * 60;
    if (bSuccess) { 
        dwDuration += GetDlgItemInt(g_hwnd, IDC_SECS, &bSuccess, FALSE);
    }
    if (!bSuccess) {
        return 0;
    }
    return max(dwDuration * 1000, 1);
}

void SetDurationText(DWORD dwDuration, bool bTitleBar) 
{
    DWORD dwMins = (dwDuration / 1000) / 60;
    DWORD dwSecs = (dwDuration / 1000) % 60;

    SetDlgItemInt(g_hwnd, IDC_MINS, dwMins, FALSE);
    SetDlgItemInt(g_hwnd, IDC_SECS, dwSecs, FALSE);

    TCHAR szBuf[50];
    if (bTitleBar) {
        _sntprintf(szBuf, ARRAYLEN(szBuf), APPTITLE_FORMAT, dwMins, dwSecs);
    }
    else {
        _tcscpy(szBuf, APPTITLE);
    }
    SetWindowText(g_hwnd, szBuf);
    
    if (g_bMinimizedToTray) {
        UpdateTrayIconTooltip(szBuf);
    }
}

void SavePreferences() {
    HKEY hkey = NULL;
    LONG rc = RegCreateKeyEx(
        HKEY_CURRENT_USER, REGKEY_GEGG, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (rc != ERROR_SUCCESS) return;

    DWORD dwValue;

    dwValue = g_dwTimerDuration ? g_dwTimerDuration : GetTimerDuration();
    if (dwValue) {
        RegSetValueEx(hkey, REGVAL_DURATION, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));
    }

    dwValue = IsChecked(IDC_MSGBOX) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_MSGBOX, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_SOUND) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_SOUND, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_USETRAY) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_USE_TRAY, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));
    
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
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue) 
        && dwValue > 0 && dwValue < 24 * 60 * 60 * 1000) 
    {
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

    RegCloseKey(hkey);
}

void EnableControls(BOOL bEnable)
{
    for (int n = 0; n < ARRAYLEN(g_rgControl); ++n) {
        EnableWindow(GetDlgItem(g_hwnd, g_rgControl[n]), bEnable);
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

BOOL OnInitDialog(HWND hwnd, WPARAM wParam) 
{
    // attach icon to main dialog
    HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_GEGG));
    SendMessage(hwnd, WM_SETICON, WPARAM(ICON_SMALL), LPARAM(hIcon));
    SendMessage(hwnd, WM_SETICON, WPARAM(ICON_BIG),   LPARAM(hIcon));

    CenterWindow(hwnd);

    EnableControlNotifications(hwnd);

    // set focus to IDC_MINS
    if (GetDlgCtrlID((HWND) wParam) != IDC_MINS) { 
        SetFocus(GetDlgItem(hwnd, IDC_MINS)); 
        return FALSE; 
    } 

    return TRUE; 
}

void StartFlash()
{
    OnTimer(TIMERID_FLASH, 0);
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

    OnStopTiming();
}

void AddTrayIcon()
{
    NOTIFYICONDATA nid;
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = IDI_GEGG;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_GEGG_TRAYMSG;
    nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_GEGG));
    _tcscpy(nid.szTip, APPTITLE);

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
    _tcscpy(nid.szTip, pszTitle);

    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void OnStopTiming() {
    RestoreFromTray();
    KillTimer(g_hwnd, TIMERID_COUNTDOWN);
    SetDlgItemText(g_hwnd, IDC_START, MSG_STARTBUTTON);
    EnableControls(TRUE);
    SetDurationText(g_dwTimerDuration, false);
    g_dwTimerStart = 0;
    g_dwTimerDuration = 0;
    PostMessage(g_hwnd, WM_NEXTDLGCTL, (WPARAM) GetDlgItem(g_hwnd, IDC_MINS), TRUE);
}

void StartSoundingAlarm()
{
    RestoreFromTray();

    KillTimer(g_hwnd, TIMERID_COUNTDOWN);
    EnableControls(TRUE);
    SetDurationText(0, true);

    SetDlgItemText(g_hwnd, IDC_START, MSG_STOPBUTTON);
    g_bIsAlarmSounding = TRUE;

    StartFlash();

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
        PlaySound(NULL, NULL, SND_NODEFAULT);
        PlaySound((LPCTSTR) IDR_AHEM, g_hInstance, SND_RESOURCE | SND_NODEFAULT | SND_ASYNC);

        // when generated more than once, sound it twice a little overlapped 
        // for a double whammy effect
        if (g_nSoundCount > 0) { 
            Sleep(500);
            PlaySound((LPCTSTR) IDR_AHEM, g_hInstance, SND_RESOURCE | SND_NODEFAULT | SND_ASYNC);
        }

        // increase the amount of time before we play the sound each time and
        // set a maximum on the number of times we play it.
        ++g_nSoundCount;
        if (g_nSoundCount >= 5) {
            KillTimer(g_hwnd, TIMERID_SOUND);
        }
        else {
            SetTimer(g_hwnd, TIMERID_SOUND, TIMERDELAY_SOUND * g_nSoundCount, NULL);
        }
        return TRUE;
    }

    return FALSE;
}

void OnStartTiming() {
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

BOOL OnEnter() 
{
    DWORD id = GetDlgCtrlID(GetFocus());
    if (id == IDC_MINS || id == IDC_SECS) {
        PostMessage(g_hwnd, WM_NEXTDLGCTL, 0, 0);
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
    case IDC_MINS:
    case IDC_SECS:
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
            OnStopTiming();
        }
        else {
            OnStartTiming();
        }
        return TRUE;

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
    if (uMouseMsg == WM_LBUTTONDBLCLK && uIconId == IDI_GEGG) {
        RestoreFromTray();
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
        if (wParam == SC_MINIMIZE && IsChecked(IDC_USETRAY)) {
            MinimizeToTray();
            SetWindowLong(g_hwnd, DWL_MSGRESULT, 0);
            return TRUE;
        }
        return FALSE;

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

    SetDlgItemText(g_hwnd, IDC_MINS, _T("3"));
    SetDlgItemText(g_hwnd, IDC_SECS, _T("0"));

    LoadPreferences();

    SendDlgItemMessage(g_hwnd, IDC_MINS, EM_SETSEL, 0, -1);

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

