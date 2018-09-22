/*
Copyright (c) 2016 NLP

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "resource.h"
#include "stdafx.h"
#include "irsdk_defines.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken = '6595b64144ccf1df' language = '*'\"")

#define MAX_FFB_DEVICES 16
#define DI_MAX 10000
#define IR_MAX 9996
#define MINFORCE_MULTIPLIER 100
#define MIN_MAXFORCE 5
#define MAX_MAXFORCE 300
#define BUMPSFORCE_MULTIPLIER 1.6f
#define LOADFORCE_MULTIPLIER 0.08f
#define LONGLOAD_STDPOWER 4
#define LONGLOAD_MAXPOWER 8
#define STOPS_MAXFORCE_RAD 0.2618f // 15 deg
#define DAMPING_MULTIPLIER 800.0f
#define DAMPING_MULTIPLIER_STOPS 150000.0f
#define USTEER_MIN_OFFSET 0.175f
#define USTEER_MULTIPLIER 0.0075f
#define DIRECT_INTERP_SAMPLES 6
#define SETTINGS_KEY L"Software\\irFFB\\Settings"
#define RUN_ON_STARTUP_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define INI_PATH L"\\irFFB.ini"
#define INI_SCAN_FORMAT  "%[^:]:%d:%d:%d:%f:%f:%d:%d:%f:%f:%f:%f"
#define INI_PRINT_FORMAT "%s:%d:%d:%d:%0.1f:%0.1f:%d:%d:%0.1f:%0.1f:%0.1f:%0.1f\r"
#define MAX_CAR_NAME 32
#define MAX_LATENCY_TIMES 32
#define LATENCY_MIN_DX 60
#define HID_CLASS_GUID { 0x745a17a0, 0x74d3, 0x11d0, 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda };
#define WM_TRAY_ICON WM_USER+1
#define WM_EDIT_VALUE WM_USER+2
#define EDIT_INT 0
#define EDIT_FLOAT 1
#define ID_TRAY_EXIT 40000

#define SVCNAME L"irFFBsvc"
#define CMDLINE_HGSVC    L"service"
#define CMDLINE_HGINST   L"hgInst"
#define CMDLINE_HGREPAIR L"hgRepair"

#define G25PID  0xc299
#define DFGTPID 0xc29a
#define G27PID  0xc29b

#define G25PATH  L"vid_046d&pid_c299"
#define DFGTPATH L"vid_046d&pid_c29a"
#define G27PATH  L"vid_046d&pid_c29b"

#define LOGI_WHEEL_HID_CMD "\x00\xf8\x81\x84\x03\x00\x00\x00\x00"
#define LOGI_WHEEL_HID_CMD_LEN 9

enum ffbType {
    FFBTYPE_360HZ,
    FFBTYPE_360HZ_INTERP,
    FFBTYPE_DIRECT_FILTER,
    FFBTYPE_DIRECT_FILTER_720,
    FFBTYPE_UNKNOWN
};

typedef struct sWins {
    HWND trackbar;
    HWND label;
    HWND value;
} sWins_t;

struct LogiRpmData {
    float rpm;
    float rpmFirstLed;
    float rpmRedLine;
};

struct LogiLedData {
    DWORD size;
    DWORD version;
    LogiRpmData rpmData;
};

struct understeerCoefs {
    char *car;
    float yawRateMult;
    float latAccelDiv;
};

DWORD WINAPI readWheelThread(LPVOID);
DWORD WINAPI directFFBThread(LPVOID);

ATOM MyRegisterClass(HINSTANCE);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

HWND combo(HWND, wchar_t *, int, int); 
sWins_t *slider(HWND, wchar_t *, int, int, wchar_t *, wchar_t *, bool);
HWND checkbox(HWND, wchar_t *, int, int); 

bool initVJD();
void text(wchar_t *, ...);
void text(wchar_t *, char *);
void debug(wchar_t *);
template <typename T> void debug(wchar_t *, T t, ...);
void setCarStatus(char *);
void setConnectedStatus(bool);
void setOnTrackStatus(bool);
void enumDirectInput();
void initDirectInput();
void releaseDirectInput();
void reacquireDIDevice();
inline void sleepSpinUntil(PLARGE_INTEGER, UINT, UINT);
inline int scaleTorque(float);
inline void setFFB(int);
void initAll();
void releaseAll();

BOOL CALLBACK EnumFFDevicesCallback(LPCDIDEVICEINSTANCE, VOID *);

// The compiler seems to like branches
inline float minf(float a, float b) {

    __m128 ma = _mm_set_ss(a);
    __m128 mb = _mm_set_ss(b);
    return _mm_cvtss_f32(_mm_min_ss(ma, mb));

}

inline float maxf(float a, float b) {

    __m128 ma = _mm_set_ss(a);
    __m128 mb = _mm_set_ss(b);
    return _mm_cvtss_f32(_mm_max_ss(ma, mb));

}

inline float csignf(float a, float b) {

    float mask = -0.0f;

    __m128 ma = _mm_set_ss(a);
    __m128 mb = _mm_set_ss(b);
    __m128 mm = _mm_set_ss(mask);
    ma = _mm_andnot_ps(mm, ma);
    mb = _mm_and_ps(mb, mm);
    return _mm_cvtss_f32(_mm_or_ps(ma, mb));

}