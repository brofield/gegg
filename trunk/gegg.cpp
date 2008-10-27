#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT    0x0500  // Windows 2000
#define WINVER          0x0500  // Windows 2000
#define _WIN32_IE       0x0500  // Internet Explorer 5.0

#include <windows.h>
#include <mmsystem.h>
#include <tchar.h>
#include <stdio.h> 

#include "resource.h"

#pragma comment(lib, "winmm.lib")

#define TIMERID                 1
#define ARRAYLEN(x)             (sizeof(x)/sizeof((x)[0]))

#define REGKEY_GEGG             _T("SOFTWARE\\Jellycan\\Gegg")
#define REGVAL_DURATION         _T("Duration")
#define REGVAL_ALARM_MSGBOX     _T("AlarmMsgBox")
#define REGVAL_ALARM_FRONT      _T("AlarmBringToFront")
#define REGVAL_ALARM_SOUND      _T("AlarmSound")

#define APPTITLE                _T("gegg")
#define APPTITLE_FORMAT         _T("%u:%02u - gegg")

#define MSG_STARTBUTTON         _T("Start")
#define MSG_STOPBUTTON          _T("Stop")
#define MSG_ALARM               _T("Time is up!")
#define MSG_INVALIDTIME         _T("Invalid duration")


// globals
HINSTANCE   g_hInstance = NULL;
HWND        g_hwnd = NULL;
DWORD       g_dwTimerStart = 0;
DWORD       g_dwTimerDuration = 0;


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

    if (bTitleBar) {
        TCHAR szBuf[50];
        _sntprintf_s(szBuf, ARRAYLEN(szBuf), APPTITLE_FORMAT, dwMins, dwSecs);
        SetWindowText(g_hwnd, szBuf);
    }
    else {
        SetWindowText(g_hwnd, APPTITLE);
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

    dwValue = IsChecked(IDC_ALARM_MSGBOX) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_MSGBOX, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_ALARM_FRONT) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_FRONT, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

    dwValue = IsChecked(IDC_ALARM_SOUND) ? 1 : 0;
    RegSetValueEx(hkey, REGVAL_ALARM_SOUND, 0, REG_DWORD, (BYTE*) &dwValue, sizeof(dwValue));

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
        SendDlgItemMessage(g_hwnd, IDC_ALARM_MSGBOX, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_ALARM_FRONT, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        SendDlgItemMessage(g_hwnd, IDC_ALARM_FRONT, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    dwLen = sizeof(dwValue);
    rc = RegQueryValueEx(hkey, REGVAL_ALARM_SOUND, 0, &dwType, (LPBYTE) &dwValue, &dwLen);
    if (rc == ERROR_SUCCESS && dwType == REG_DWORD && dwLen == sizeof(dwValue)) {
        SendDlgItemMessage(g_hwnd, IDC_ALARM_SOUND, BM_SETCHECK, dwValue ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    RegCloseKey(hkey);
}

void EnableControls(BOOL bEnable)
{
    int rgControl[] = {
        IDC_MINS, IDC_SECS, IDC_ALARM_FRONT, IDC_ALARM_MSGBOX, IDC_ALARM_SOUND
    };
    for (int n = 0; n < ARRAYLEN(rgControl); ++n) {
        EnableWindow(GetDlgItem(g_hwnd, rgControl[n]), bEnable);
    }
}

BOOL OnInitDialog(HWND hwnd, WPARAM wParam) 
{
    // attach icon to main dialog
    HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_GEGG));
    SendMessage(hwnd, WM_SETICON, WPARAM(ICON_SMALL), LPARAM(hIcon));
    SendMessage(hwnd, WM_SETICON, WPARAM(ICON_BIG),   LPARAM(hIcon));

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

    if (GetDlgCtrlID((HWND) wParam) != IDC_MINS) { 
        SetFocus(GetDlgItem(hwnd, IDC_MINS)); 
        return FALSE; 
    } 

    return TRUE; 
}

void SoundAlarm()
{
    if (IsChecked(IDC_ALARM_SOUND)) {
        PlaySound(NULL, NULL, SND_NODEFAULT);
        PlaySound((LPCTSTR) SND_ALIAS_SYSTEMEXCLAMATION, 
            NULL, SND_ALIAS_ID | SND_NODEFAULT | SND_ASYNC);
    }

    if (IsChecked(IDC_ALARM_FRONT)) {
        ShowWindow(g_hwnd, SW_SHOW);
        SetActiveWindow(g_hwnd);
        SetForegroundWindow(g_hwnd);
    }

    if (IsChecked(IDC_ALARM_MSGBOX)) {
        MessageBox(g_hwnd, MSG_ALARM, APPTITLE, MB_ICONEXCLAMATION | MB_OK);
    }
}

void OnStopTiming() {
    KillTimer(g_hwnd, TIMERID);
    SetWindowText(GetDlgItem(g_hwnd, IDC_START), MSG_STARTBUTTON);
    EnableControls(TRUE);
    SetDurationText(g_dwTimerDuration, false);
    g_dwTimerStart = 0;
    g_dwTimerDuration = 0;
    PostMessage(g_hwnd, WM_NEXTDLGCTL, (WPARAM) GetDlgItem(g_hwnd, IDC_MINS), TRUE);
}

BOOL OnTimer(WPARAM wParam, LPARAM lParam) 
{
    if (wParam != TIMERID) {
        return FALSE;
    }

    DWORD dwDuration = GetTickCount() - g_dwTimerStart;
    if (dwDuration >= g_dwTimerDuration) {
        OnStopTiming();
        SoundAlarm();
    }
    else {
        DWORD dwRemaining = g_dwTimerDuration - dwDuration;
        dwDuration = min(dwRemaining, 1000);
        SetTimer(g_hwnd, TIMERID, dwDuration, NULL);
        SetDurationText(dwRemaining, true);
    }

    return TRUE;
}

void OnStartTiming() {
    g_dwTimerDuration = GetTimerDuration();
    if (!g_dwTimerDuration) {
        MessageBox(g_hwnd, MSG_INVALIDTIME, APPTITLE, MB_ICONERROR | MB_OK);
        return;
    }
    g_dwTimerStart = GetTickCount();

    SavePreferences();

    SetWindowText(GetDlgItem(g_hwnd, IDC_START), MSG_STOPBUTTON);
    EnableControls(FALSE);

    OnTimer(TIMERID, 0); // will start the timer
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
        if (g_dwTimerStart && g_dwTimerDuration) {
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

BOOL CALLBACK GeggDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) 
{ 
    switch (message) { 
    case WM_INITDIALOG:
        return OnInitDialog(hwnd, wParam);

    case WM_COMMAND:
        return OnCommand(wParam, lParam);

    case WM_TIMER:
        return OnTimer(wParam, lParam);

    case WM_CLOSE:
        SavePreferences();
        DestroyWindow(hwnd);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;

    default: 
        return FALSE; 
    } 
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int iCmdShow)
{
    g_hInstance = hInstance;

    g_hwnd = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_GEGG), NULL, GeggDialogProc);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOW);

    SetDlgItemText(g_hwnd, IDC_MINS, _T("3"));
    SetDlgItemText(g_hwnd, IDC_SECS, _T("0"));

    SendDlgItemMessage(g_hwnd, IDC_ALARM_FRONT,  BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessage(g_hwnd, IDC_ALARM_MSGBOX, BM_SETCHECK, BST_CHECKED, 0);
    SendDlgItemMessage(g_hwnd, IDC_ALARM_SOUND,  BM_SETCHECK, BST_CHECKED, 0);

    LoadPreferences();

    SendDlgItemMessage(g_hwnd, IDC_MINS, EM_SETSEL, 0, -1);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) { 
        if (!IsDialogMessage(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}

