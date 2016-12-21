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

#include "stdafx.h"
#include "irFFB.h"
#include "public.h"
#include "irsdk_defines.h"
#include "vjoyinterface.h"

/*
#pragma comment(lib, "comctl32.lib")
#include <commctrl.h >
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken = '6595b64144ccf1df' language = '*'\"")
*/

#define MAX_LOADSTRING 100

#define VJOY_DEVICEID 1
#define MAX_FFB_DEVICES 16
#define MAX_TESTS 20
#define MAX_SAMPLES 24
#define DI_MAX 10000
#define MINFORCE_MULTIPLIER 20
#define STOPS_MAXFORCE_RAD 0.175f // 10 deg
#define KEY_PATH L"Software\\irFFB\\Settings"

enum ffbType {
    FFBTYPE_60HZ,
    FFBTYPE_120HZ,
    FFBTYPE_360HZ,
    FFBTYPE_60HZ_INTERP,
    FFBTYPE_360HZ_INTERP,
    FFBTYPE_DIRECT,
    FFBTYPE_DIRECT_FILTER,
    FFBTYPE_360HZ_PREDICT,
    FFBTYPE_UNKNOWN
};

wchar_t *ffbTypeString(int type) {

    switch (type) {
        case FFBTYPE_60HZ:          return L"60 Hz";
        case FFBTYPE_120HZ:         return L"120 Hz";
        case FFBTYPE_360HZ:         return L"360 Hz";
        case FFBTYPE_60HZ_INTERP:   return L"60 Hz interpolated";
        case FFBTYPE_360HZ_INTERP:  return L"360 Hz interpolated";
        case FFBTYPE_DIRECT:        return L"60 Hz direct";
        case FFBTYPE_DIRECT_FILTER: return L"60 Hz direct filtered";
        case FFBTYPE_360HZ_PREDICT: return L"360 Hz predicted";
        default:                    return L"Unknown FFB type";
    }
}

wchar_t *ffbTypeShortString(int type) {

    switch (type) {
        case FFBTYPE_60HZ:          return L"60 Hz";
        case FFBTYPE_120HZ:         return L"120 Hz";
        case FFBTYPE_360HZ:         return L"360 Hz";
        case FFBTYPE_60HZ_INTERP:   return L"60 Hz I";
        case FFBTYPE_360HZ_INTERP:  return L"360 Hz I";
        case FFBTYPE_DIRECT:        return L"60 Hz D";
        case FFBTYPE_DIRECT_FILTER: return L"60 Hz DF";
        case FFBTYPE_360HZ_PREDICT: return L"360 Hz P";
        default:                    return L"Unknown";
    }
}

extern HANDLE hDataValidEvent;

// Globals
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

LPDIRECTINPUT8 pDI = nullptr;
GUID ffdevices[MAX_FFB_DEVICES];
int  ffdeviceIdx = 0;
GUID devGuid = GUID_NULL;
LPDIRECTINPUTDEVICE8 ffdevice = nullptr;
LPDIRECTINPUTEFFECT effect = nullptr;

DIJOYSTATE joyState;
DWORD axes[1] = { DIJOFS_X };
LONG  dir[1]  = { 0 };
DIPERIODIC pforce;
DIEFFECT   dieff;

float cosInterp[6];

// float firc[] = { 0.0227531f, 0.11104f, 0.2843528f, 0.3356835f, 0.1998188f, 0.0463566f };
float firc[] = { -0.0072657f, 0.0838032f, 0.2410764f, 0.3194108f, 0.2757275f, 0.0871442f  };

int ffb, origFFB, testFFB;
int force = 0, minForce = 0, maxForce = 0, delayTicks = 0;

int numButtons, numPov;

HANDLE wheelEvent = CreateEvent(nullptr, false, false, L"WheelEvent");
HANDLE ffbEvent   = CreateEvent(nullptr, false, false, L"FFBEvent");

HWND mainWnd, textWnd, devWnd, ffbWnd, cmpWnd, minWnd, maxWnd, delayWnd, testWnd;

LARGE_INTEGER freq;

int read, write;

int testNum = MAX_TESTS + 1;
int testFFBTypes[MAX_TESTS];
bool guesses[MAX_TESTS];

float *floatvarptr(const char *data, const char *name) {
    int idx = irsdk_varNameToIndex(name);
    if (idx >= 0) 
        return (float *)(data + irsdk_getVarHeaderEntry(idx)->offset);
    else
        return nullptr;
}

int *intvarptr(const char *data, const char *name) {
    int idx = irsdk_varNameToIndex(name);
    if (idx >= 0)
        return (int *)(data + irsdk_getVarHeaderEntry(idx)->offset);
    else
        return nullptr;
}

bool *boolvarptr(const char *data, const char *name) {
    int idx = irsdk_varNameToIndex(name);
    if (idx >= 0)
        return (bool *)(data + irsdk_getVarHeaderEntry(idx)->offset);
    else
        return nullptr;
}

// Thread that reads the wheel via DirectInput and writes to vJoy
DWORD WINAPI readWheelThread(LPVOID lParam) {

    HRESULT res;
    JOYSTICK_POSITION vjData;

    ResetVJD(VJOY_DEVICEID);

    while (true) {

        res = WaitForSingleObject(wheelEvent, INFINITE);
        
        if (!ffdevice)
            continue; 

        if (ffdevice->GetDeviceState(sizeof(joyState), &joyState) == DIERR_NOTACQUIRED) {
            reacquireDIDevice();
            ffdevice->GetDeviceState(sizeof(joyState), &joyState);
        }

        vjData.wAxisX = joyState.lX;
        vjData.wAxisY = joyState.lY;
        vjData.wAxisZ = joyState.lZ;

        for (int i = 0; i < numButtons; i++) {
            if (joyState.rgbButtons[i])
                vjData.lButtons |= 1 << i;
            else
                vjData.lButtons &= ~(1 << i);
        }
        // This could be wrong, untested..
        for (int i = 0; i < numPov; i++) {

            switch (i) {
                case 0:
                    vjData.bHats = joyState.rgdwPOV[i];
                    break;
                case 1:
                    vjData.bHatsEx1 = joyState.rgdwPOV[i];
                    break;
                case 2:
                    vjData.bHatsEx2 = joyState.rgdwPOV[i];
                    break;
                case 3:
                    vjData.bHatsEx3 = joyState.rgdwPOV[i];
                    break;
            }

        }

        UpdateVJD(VJOY_DEVICEID, (PVOID)&vjData);

    }

}

// Write FFB samples for the direct modes
DWORD WINAPI directFFBThread(LPVOID lParam) {

    float s;
    float prod[] = { 0, 0, 0, 0, 0, 0 };
    float output[6];
    LARGE_INTEGER start;

    while (true) {

        // Signalled when force has been updated
        WaitForSingleObject(ffbEvent, INFINITE);

        if (ffb == FFBTYPE_DIRECT) {
            setFFB(force);
            continue;
        }

#define DIRECT_INTERP_SAMPLES 6

        QueryPerformanceCounter(&start);

        s = (float)force;

        prod[0] = s * firc[0];
        output[0] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];
        prod[1] = s * firc[1];
        output[1] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];
        prod[2] = s * firc[2];
        output[2] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];
        prod[3] = s * firc[3];
        output[3] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];
        prod[4] = s * firc[4];
        output[4] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];
        prod[5] = s * firc[5];
        output[5] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];

        for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {

            setFFB((int)output[i]);
            sleepSpinUntil(&start, 2000, 2760 * i);

        }

    }

    return 0;

}

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow
) {

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    HANDLE handles[1];
    char *data = nullptr;
    bool irConnected = false;
    MSG msg;

    float *swTorque = nullptr, *swTorqueST = nullptr, *steer = nullptr, *steerMax = nullptr;
    float *speed = nullptr;
    int *gear = nullptr;
    bool *isOnTrack = nullptr;

    bool wasOnTrack = false, hasStopped = false, readingGuess = false, inNeutral = true;
    int numHandles = 0, dataLen = 0;
    int swTSTnumSamples = 0, swTSTmaxIdx = 0, swTSTusPerSample = 0;
    float halfSteerMax = 0, lastSpeed, lastTorque;
    int maxGear, neutralCounter;

    int sampleBuffer[MAX_SAMPLES];
    memset(sampleBuffer, 0, sizeof(int) * MAX_SAMPLES);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IRFFB));

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IRFFB, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    cosInterp[0] = 0;
    for (int i = 1; i < 6; i++) {
        cosInterp[i] = (1.0f - cosf((float)M_PI * (float)i / 6)) / 2;
    }

    readSettings();
    
    // Setup DI FFB effect
    pforce.dwMagnitude = 0;
    pforce.dwPeriod = INFINITE;
    pforce.dwPhase = 0;
    pforce.lOffset = 0;

    ZeroMemory(&dieff, sizeof(dieff));
    dieff.dwSize = sizeof(DIEFFECT);
    dieff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    dieff.dwDuration = INFINITE;
    dieff.dwSamplePeriod = 0;
    dieff.dwGain = DI_FFNOMINALMAX;
    dieff.dwTriggerButton = DIEB_NOTRIGGER;
    dieff.dwTriggerRepeatInterval = 0;
    dieff.cAxes = 1;
    dieff.rgdwAxes = axes;
    dieff.rglDirection = dir;
    dieff.lpEnvelope = NULL;
    dieff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    dieff.lpvTypeSpecificParams = &pforce;
    dieff.dwStartDelay = 0;

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    LARGE_INTEGER start;
    QueryPerformanceFrequency(&freq);

    initVJD();
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    CreateThread(NULL, 0, readWheelThread, NULL, 0, NULL);
    CreateThread(NULL, 0, directFFBThread, NULL, 0, NULL);

    srand((unsigned)time(NULL));
    
    while (TRUE) {

        DWORD res;
        const irsdk_header *hdr = NULL;

        if (
            irsdk_startup() && (hdr = irsdk_getHeader()) &&
            hdr->status & irsdk_stConnected && hdr->bufLen != dataLen && hdr->bufLen != 0
        ) {

            handles[0] = hDataValidEvent;
            numHandles = 1;

            if (data != NULL)
                free(data);
                    
            dataLen = irsdk_getHeader()->bufLen;
            data = (char *)malloc(dataLen);
            text(L"New session");

            swTorque = floatvarptr(data, "SteeringWheelTorque");
            swTorqueST = floatvarptr(data, "SteeringWheelTorque_ST");
            steer = floatvarptr(data, "SteeringWheelAngle");
            steerMax = floatvarptr(data, "SteeringWheelAngleMax");
            speed = floatvarptr(data, "Speed");
            gear = intvarptr(data, "Gear");
            isOnTrack = boolvarptr(data, "IsOnTrack");
            
            int swTorqueSTidx = irsdk_varNameToIndex("SteeringWheelTorque_ST");
            swTSTnumSamples = irsdk_getVarHeaderEntry(swTorqueSTidx)->count;
            swTSTmaxIdx = swTSTnumSamples - 1;

            lastSpeed = lastTorque = 0.0f;
            neutralCounter = 0;
            wasOnTrack = readingGuess = hasStopped = false;
            inNeutral = irConnected = true;

        }

        res = MsgWaitForMultipleObjects(numHandles, handles, FALSE, 1000, QS_ALLINPUT);

        if (numHandles > 0 && res == numHandles - 1 && irsdk_getNewData(data)) {

            if (wasOnTrack && !*isOnTrack) {
                wasOnTrack = false;
                text(L"Has left track");
                lastTorque = 0;
                force = 0;
                setFFB(force);
            }

            else if (!wasOnTrack && *isOnTrack) {
                wasOnTrack = true;
                text(L"Is now on track");
                reacquireDIDevice();
                write = 0;
                read = -delayTicks;
                if (testNum == 0)
                    ffb = rand() & 1 ? testFFB : origFFB;
            }

            // Blind testing
            if (testNum < MAX_TESTS) {

                if (*speed < 0.1 && lastSpeed > 0.1) {
                    hasStopped = true;
                }
                else if (*speed > 0.1 && lastSpeed < 0.1 && hasStopped) {

                    if (readingGuess)
                        text(L"Set off without making a guess, no change");

                    hasStopped = false;
                    readingGuess = false;
                    maxGear = 0;

                }

                if (*gear == 0 && neutralCounter++ >= 10) {
                    inNeutral = true;
                }
                else {
                    neutralCounter = 0;
                    inNeutral = false;
                }

                if (hasStopped && !readingGuess && inNeutral) {
                    readingGuess = true;
                    text(L"Waiting for guess..");
                }

                if (readingGuess && *gear > maxGear)
                    maxGear = *gear;

                if (hasStopped && readingGuess && maxGear > 0 && maxGear < 3 && inNeutral) {

                    readingGuess = hasStopped = false;
                    testFFBTypes[testNum] = ffb;

                    if (ffb == testFFB && maxGear == 2)
                        guesses[testNum] = true;
                    else if (ffb == origFFB && maxGear == 1)
                        guesses[testNum] = true;
                    else
                        guesses[testNum] = false;

                    text(
                        L"Guess %d of %s recorded, setting FFB type",
                        testNum + 1,
                        maxGear == 2 ? ffbTypeShortString(testFFB) : ffbTypeShortString(origFFB)
                    );

                    maxGear = 0;
                    testNum++;

                    ffb = rand() & 1 ? testFFB : origFFB;

                }
            }

            lastSpeed = *speed;

            if (!*isOnTrack || ffb == FFBTYPE_DIRECT || ffb == FFBTYPE_DIRECT_FILTER)
                continue;

            halfSteerMax = *steerMax / 2;

            // Bump stops
            if (abs(halfSteerMax) < 8 && abs(*steer) > halfSteerMax) {

                float factor, invFactor;

                if (*steer > 0) {
                    factor = (-(*steer - halfSteerMax)) / STOPS_MAXFORCE_RAD;
                    if (factor < -1)
                        factor = -1;
                    invFactor = 1 + factor;
                }
                else {
                    factor = (-(*steer + halfSteerMax)) / STOPS_MAXFORCE_RAD;
                    if (factor > 1)
                        factor = 1;
                    invFactor = 1 - factor;
                }

                setFFB((int)(factor * DI_MAX + scaleTorque(*swTorque) * invFactor));
                continue;

            }

            // Telemetry FFB
            switch (ffb) {

                case FFBTYPE_60HZ: {

                    if (delayTicks > 0) {
                        sampleBuffer[write] = scaleTorque(*swTorque);
                        if (++write > delayTicks)
                            write = 0;
                        if (read >= 0)
                            setFFB(sampleBuffer[read]);
                        if (++read > delayTicks)
                            read = 0;
                    }
                    else
                        setFFB(scaleTorque(*swTorque));

                }
                break;
                
                case FFBTYPE_120HZ: {

                    QueryPerformanceCounter(&start);
                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx >> 1]));

                    sleepSpinUntil(&start, 8000, 8320);
                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx]));

                }
                break;

                case FFBTYPE_360HZ: {

                    QueryPerformanceCounter(&start);

                    for (int i = 0; i < swTSTmaxIdx; i++) {

                        setFFB(scaleTorque(swTorqueST[i]));
                        sleepSpinUntil(&start, 2000, 2760 * (i + 1));
                        
                    }
                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx]));

                }
                break;

                case FFBTYPE_60HZ_INTERP: {

                    QueryPerformanceCounter(&start);

                    for (int i = 1; i < swTSTnumSamples; i++) {

                        setFFB(scaleTorque(lastTorque * (1 - cosInterp[i]) + *swTorque * cosInterp[i]));
                        sleepSpinUntil(&start, 2000, 2760 * i);

                    }
                    setFFB(scaleTorque(*swTorque));
                    lastTorque = *swTorque;

                }
                break;

                case FFBTYPE_360HZ_INTERP: {

                    QueryPerformanceCounter(&start);

                    float diff = swTorqueST[0] - lastTorque;
                    setFFB(scaleTorque(lastTorque + diff / 2));
                    sleepSpinUntil(&start, 0, 1380);
                    
                    for (int i = 0; i < swTSTmaxIdx << 1; i++) {

                        if (i & 1) {
                            int idx = i >> 1;
                            diff = swTorqueST[idx + 1] - swTorqueST[idx];
                            setFFB(scaleTorque(swTorqueST[idx] + diff / 2));
                        }
                        else
                            setFFB(scaleTorque(swTorqueST[i >> 1]));

                        sleepSpinUntil(&start, 0, 1380 * (i + 2));

                    }

                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx]));
                    lastTorque = swTorqueST[swTSTmaxIdx];

                }
                break;

                case FFBTYPE_360HZ_PREDICT: {

                    QueryPerformanceCounter(&start);

                    float diff = swTorqueST[swTSTmaxIdx] - swTorqueST[swTSTmaxIdx - 1];
    
                    for (int i = 0; i < swTSTmaxIdx; i++) {

                        setFFB(scaleTorque(swTorqueST[swTSTmaxIdx] + diff * i));
                        sleepSpinUntil(&start, 2000, 2760 * (i + 1));

                    }

                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx] + diff * swTSTmaxIdx));

                }
                break;

            }

        }

        // Did we lose iRacing?
        if (numHandles > 0 && !(hdr->status & irsdk_stConnected)) {
            numHandles = 0;
            dataLen = 0;
            if (data != NULL) {
                free(data);
                data = NULL;
            }
        }

        // Window messages
        if (res == numHandles) {

            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT)
                    DestroyWindow(mainWnd);
                if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }

        }

    }

    return (int)msg.wParam;

}

void startTest() {

    text(L"Blind testing started");
    SendMessage(testWnd, WM_SETTEXT, 0, (LPARAM)L"End Test");

    testNum = 0;
    memset(testFFBTypes, 0, sizeof(int) * MAX_TESTS);
    memset(guesses, 0, sizeof(bool) * MAX_TESTS);
    EnableWindow(ffbWnd, false);
    EnableWindow(cmpWnd, false);
        
}

void endTest() {

    text(L"Blind testing stopped");
    SendMessage(testWnd, WM_SETTEXT, 0, (LPARAM)L"Blind Test");

    if (testNum) {

        int correct = 0;

        for (int i = 0; i < testNum; i++) {
            text(
                L"Test %d: FFB was %s, guess was %s",
                i + 1,
                ffbTypeShortString(testFFBTypes[i]),
                guesses[i] ? L"correct" : L"incorrect"
            );
            if (guesses[i])
                correct++;
        }

        text(L"Summary: %.02f%% correct", (float)correct * 100 / testNum);

    }

    ffb = origFFB;
    EnableWindow(ffbWnd, true);
    EnableWindow(cmpWnd, true);
    testNum = MAX_TESTS + 1;

}

ATOM MyRegisterClass(HINSTANCE hInstance) {

    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IRFFB));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_IRFFB);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);

}

void combo(HWND *wnd, wchar_t *name, int y) {

    CreateWindowW(
        L"STATIC", name,
        WS_CHILD | WS_VISIBLE,
        44, y, 300, 20, mainWnd, NULL, hInst, NULL
    );
    *wnd = CreateWindow(
        L"COMBOBOX", nullptr,
        CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_OVERLAPPED | WS_TABSTOP,
        52, y + 26, 300, 240, mainWnd, nullptr, hInst, nullptr
    );

}

void slider(HWND *wnd, wchar_t *name, int y, wchar_t *start, wchar_t *end) {

    CreateWindowW(
        L"STATIC", name,
        WS_CHILD | WS_VISIBLE,
        44, y, 300, 20, mainWnd, NULL, hInst, NULL
    );

    *wnd = CreateWindowEx(
        0, TRACKBAR_CLASS, name,
        WS_CHILD | WS_VISIBLE | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND,
        84, y + 26, 240, 30,
        mainWnd, NULL, hInst, NULL
    );

    HWND buddyLeft = CreateWindowEx(
        0, L"STATIC", start,
        SS_LEFT | WS_CHILD | WS_VISIBLE,
        0, 0, 46, 20, mainWnd, NULL, hInst, NULL
    );
    SendMessage(*wnd, TBM_SETBUDDY, (WPARAM)TRUE, (LPARAM)buddyLeft);

    HWND buddyRight = CreateWindowEx(
        0, L"STATIC", end,
        SS_RIGHT | WS_CHILD | WS_VISIBLE,
        0, 0, 58, 20, mainWnd, NULL, hInst, NULL
    );
    SendMessage(*wnd, TBM_SETBUDDY, (WPARAM)FALSE, (LPARAM)buddyRight);

}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {

    hInst = hInstance;

    mainWnd = CreateWindowW(
        szWindowClass, szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 432, 800,
        NULL, NULL, hInstance, NULL
    );

    if (!mainWnd)
        return FALSE;

    combo(&devWnd, L"FFB device:", 20);

    combo(&ffbWnd, L"FFB type:", 80);
    combo(&cmpWnd, L"Compare against:", 144);

    for (int i = 0; i < FFBTYPE_UNKNOWN; i++) {
        SendMessage(ffbWnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ffbTypeString(i)));
        SendMessage(cmpWnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ffbTypeString(i)));
    }

    SendMessage(ffbWnd, CB_SETCURSEL, ffb, 0);
    origFFB  = ffb;

    SendMessage(cmpWnd, CB_SETCURSEL, testFFB, 0);

    slider(&minWnd, L"Min force:", 216, L"0", L"20");
    SendMessage(minWnd, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
    SendMessage(minWnd, TBM_SETPOS, TRUE, minForce / 20);
    SendMessage(minWnd, TBM_SETPOSNOTIFY, 0, minForce / 20);
    
    slider(&maxWnd, L"Max force:", 288, L"5 Nm", L"65 Nm");
    SendMessage(maxWnd, TBM_SETRANGE, TRUE, MAKELPARAM(5, 65));
    SendMessage(maxWnd, TBM_SETPOS, TRUE, maxForce);
    SendMessage(maxWnd, TBM_SETPOSNOTIFY, 0, maxForce);

    slider(&delayWnd, L"Extra latency:", 360, L"0 ticks", L"20 ticks");
    SendMessage(delayWnd, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
    delayTicks = 0;

    textWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_VSCROLL | WS_CHILD | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
        16, 442, 384, 240,
        mainWnd, NULL, hInstance, NULL
    );
    SendMessage(textWnd, EM_SETLIMITTEXT, WPARAM(256000), 0);

    testWnd = CreateWindowEx(
        0, L"BUTTON", L"Blind Test",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        300, 700, 100, 24, mainWnd, NULL, hInst, NULL
    );

    text(L"iRacing Telemetry FFB Test");

    ShowWindow(mainWnd, nCmdShow);
    UpdateWindow(mainWnd);
    enumDirectInput();

    return TRUE;

}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    switch (message) {
        
        case WM_COMMAND: {

            int wmId = LOWORD(wParam);
            switch (wmId) {

                case IDM_ABOUT:
                    DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                    break;
                case IDM_EXIT:
                    DestroyWindow(hWnd);
                    break;
                default:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {

                        if ((HWND)lParam == devWnd) {

                            int idx = (int)SendMessage(devWnd, CB_GETCURSEL, 0, 0);
                            if (idx < ffdeviceIdx) {
                                devGuid = ffdevices[idx];
                                initDirectInput();
                            }

                        }
                        else if ((HWND)lParam == ffbWnd) {

                            ffb = origFFB = (int)SendMessage(ffbWnd, CB_GETCURSEL, 0, 0);
                            EnableWindow(delayWnd, ffb == FFBTYPE_60HZ ? true : false);

                            if (ffb == FFBTYPE_DIRECT || ffb == FFBTYPE_DIRECT_FILTER)
                                FfbStart(VJOY_DEVICEID);
                            else
                                FfbStop(VJOY_DEVICEID);

                        }
                        else if ((HWND)lParam == cmpWnd)
                            testFFB = (int)SendMessage(cmpWnd, CB_GETCURSEL, 0, 0);

                    }
                    else if (HIWORD(wParam) == BN_CLICKED) {

                        if ((HWND)lParam == testWnd) {
                            if (testNum == MAX_TESTS + 1)
                                startTest();
                            else
                                endTest();
                        }

                    }

                    return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

        case WM_HSCROLL: {

            if ((HWND)lParam == maxWnd)
                maxForce = (SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
            else if ((HWND)lParam == minWnd)
                minForce = (SendMessage((HWND)lParam, TBM_GETPOS, 0, 0)) * MINFORCE_MULTIPLIER;
            else if ((HWND)lParam == delayWnd) {
                delayTicks = (SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
                write = 0;
                read = -delayTicks;
            }

        }
        break;

        case WM_CTLCOLORSTATIC: {
            SetBkColor((HDC)wParam, RGB(0xff, 0xff, 0xff));
            return (LRESULT)CreateSolidBrush(RGB(0xff, 0xff, 0xff));
        }
        break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
        }
        break;

        case WM_POWERBROADCAST: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case PBT_APMSUSPEND:
                    releaseAll();
                break;
                case PBT_APMRESUMESUSPEND:
                    initAll();
                break;
            }
        }
        break;

        case WM_DESTROY: {
            releaseAll();
            writeSettings();
            exit(0);
        }
        break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

    UNREFERENCED_PARAMETER(lParam);
    
    switch (message) {

        case WM_INITDIALOG:
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
            }
        break;
    }

    return (INT_PTR)FALSE;

}

void text(wchar_t *fmt, ...) {

    va_list argp;
    wchar_t msg[512];
    va_start(argp, fmt);

    StringCbVPrintf(msg, sizeof(msg) - 2 * sizeof(wchar_t), fmt, argp);
    va_end(argp);
    StringCbCat(msg, sizeof(msg), L"\r\n");

    SendMessage(textWnd, EM_SETSEL, 0, -1);
    SendMessage(textWnd, EM_SETSEL, -1, 1);
    SendMessage(textWnd, EM_REPLACESEL, 0, (LPARAM)msg);
    SendMessage(textWnd, EM_SCROLLCARET, 0, 0);

}

BOOL CALLBACK EnumFFDevicesCallback(LPCDIDEVICEINSTANCE diDevInst, VOID *wnd) {

    if (ffdeviceIdx == MAX_FFB_DEVICES)
        return false;

    ffdevices[ffdeviceIdx] = diDevInst->guidInstance;
    SendMessage((HWND)wnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(diDevInst->tszProductName));

    if (devGuid == diDevInst->guidInstance) {
        SendMessage((HWND)wnd, CB_SETCURSEL, ffdeviceIdx, 0);
        initDirectInput();
    }

    ffdeviceIdx++;

    return true;

}

BOOL CALLBACK EnumObjectCallback(const LPCDIDEVICEOBJECTINSTANCE inst, VOID *dw) {

    (*(int *)dw)++;
    return DIENUM_CONTINUE;

}

void enumDirectInput() {

    if (
        FAILED(
            DirectInput8Create(
                GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
                (VOID **)&pDI, nullptr
            )
        )
    ) {
        text(L"Failed to initialise DirectInput");
        return;
    }

    pDI->EnumDevices(
        DI8DEVCLASS_GAMECTRL, EnumFFDevicesCallback, devWnd,
        DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK
    );

}

void initDirectInput() {

    DIDEVICEINSTANCE di;

    numButtons = numPov = 0;

    if (ffdevice) {
        ffdevice->Unacquire();
        ffdevice->Release();
        ffdevice = nullptr;
    }

    if (effect) {
        effect->Release();
        effect = nullptr;
    }

    if (pDI) {
        pDI->Release();
        pDI = nullptr;
    }

    if (
        FAILED(
            DirectInput8Create(
                GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
                (VOID **)&pDI, nullptr
            )
        )
    ) {
        text(L"Failed to initialise DirectInput");
        return;
    }

    if (FAILED(pDI->CreateDevice(devGuid, &ffdevice, nullptr))) {
        text(L"Failed to create DI device");
        return;
    }
    if (FAILED(ffdevice->SetDataFormat(&c_dfDIJoystick))) {
        text(L"Failed to set DI device DataFormat!");
        return;
    }
    if (FAILED(ffdevice->SetCooperativeLevel(mainWnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND))) {
        text(L"Failed to set DI device CooperativeLevel!");
        return;
    }

    di.dwSize = sizeof(DIDEVICEINSTANCE);
    if (FAILED(ffdevice->GetDeviceInfo(&di))) {
        text(L"Failed to get info for DI device!");
        return;
    }

    if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID *)&numButtons, DIDFT_BUTTON))) {
        text(L"Failed to enumerate DI device buttons");
        return;
    }
    text(L"Device has %d buttons", numButtons);

    if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID *)&numPov, DIDFT_POV))) {
        text(L"Failed to enumerate DI device povs");
        return;
    }
    text(L"Device has %d POV", numPov);

    if (FAILED(ffdevice->SetEventNotification(wheelEvent))) {
        text(L"Failed to set event notification on DI device");
        return;
    }

    if (FAILED(ffdevice->Acquire())) {
        text(L"Failed to set acquire DI device");
        return;
    }

    if (FAILED(ffdevice->CreateEffect(GUID_Sine, &dieff, &effect, nullptr))) {
        text(L"Failed to create sine periodic effect");
        return;
    }

    if (effect)
        effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);

}

void reacquireDIDevice() {

    if (ffdevice == nullptr)
        return;

    ffdevice->Unacquire();

    if (FAILED(ffdevice->Acquire()))
        text(L"Failed to acquire DI device");

    if (effect)
        effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);

}

inline void sleepSpinUntil(PLARGE_INTEGER base, UINT sleep, UINT offset) {

    LARGE_INTEGER time;

    std::this_thread::sleep_for(std::chrono::microseconds(sleep));
    while (true) {
        QueryPerformanceCounter(&time);
        time.QuadPart -= base->QuadPart;
        time.QuadPart *= 1000000;
        time.QuadPart /= freq.QuadPart;
        if (time.QuadPart > offset)
            return;
    }

}

inline int scaleTorque(float t) {

    t *= 10000;
    t /= maxForce;

    if (minForce) {
        if (t > 0 && t < minForce)
            return minForce;
        else if (t < 0 && t > -minForce)
            return -minForce;
    }

    return (int)t;

}

inline void setFFB(int mag) {

    if (!effect)
        return;

    if (mag < -DI_MAX) mag = -DI_MAX;
    else if (mag > DI_MAX) mag = DI_MAX;

    pforce.lOffset = mag;

    effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);

}

void CALLBACK vjFFBCallback(PVOID ffbPacket, PVOID data) {

    FFBPType type;
    FFB_EFF_CONSTANT constEffect;
    int16_t mag;

    if (ffb != FFBTYPE_DIRECT && ffb != FFBTYPE_DIRECT_FILTER)
        return;

    // Only interested in constant force reports
    if (Ffb_h_Type((FFB_DATA *)ffbPacket, &type) || type != PT_CONSTREP)
        return;

    // Parse the report
    if (Ffb_h_Eff_Constant((FFB_DATA *)ffbPacket, &constEffect))
        return;

    // low word is magnitude
    mag = constEffect.Magnitude & 0xffff;
    // sign extend
    force = mag;
    SetEvent(ffbEvent);

}

bool initVJD() {

    WORD verDll, verDrv;

    if (!vJoyEnabled()) {
        text(L"VJoy Not Enabled!");
        return false;
    }
    else if (!DriverMatch(&verDll, &verDrv)) {
        text(L"vJoy driver version %04x != DLL version %04x!", verDrv, verDll);
        return false;
    }
    else
        text(L"vJoy driver version %04x init OK", verDrv);

    VjdStat vjdStatus = GetVJDStatus(VJOY_DEVICEID);
    if (vjdStatus == VJD_STAT_BUSY)
        text(L"VJ device %d is busy!", VJOY_DEVICEID);
    else if (vjdStatus == VJD_STAT_MISS)
        text(L"VJ device %d is disabled!", VJOY_DEVICEID);
    if (vjdStatus == VJD_STAT_OWN) {
        RelinquishVJD(VJOY_DEVICEID);
        vjdStatus = GetVJDStatus(VJOY_DEVICEID);
    }
    if (vjdStatus == VJD_STAT_FREE) {
        if (!AcquireVJD(VJOY_DEVICEID)) {
            text(L"Failed to acquire VJ device %d!", VJOY_DEVICEID);
            return false;
        }
    }
    else {
        text(L"ERROR: VJ device %d status is %d", VJOY_DEVICEID, vjdStatus);
        return false;
    }

    if (!IsDeviceFfb(VJOY_DEVICEID)) {
        text(L"VJ device %d doesn't support FFB", VJOY_DEVICEID);
        return false;
    }

    if (!IsDeviceFfbEffect(VJOY_DEVICEID, HID_USAGE_CONST)) {
        text(L"VJ device %d doesn't support constant force effect", VJOY_DEVICEID);
        return false;
    }

    FfbRegisterGenCB(vjFFBCallback, NULL);
    ResetVJD(VJOY_DEVICEID);

    return true;

}

void readSettings() {

    wchar_t dguid[GUIDSTRING_MAX];
    HKEY regKey;
    DWORD sz = sizeof(int);
    DWORD dgsz = sizeof(dguid);

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PATH, 0, KEY_ALL_ACCESS, &regKey)) {

        if (!RegGetValue(regKey, nullptr, L"device", RRF_RT_REG_SZ, nullptr, dguid, &dgsz))
            if (FAILED(IIDFromString(dguid, &devGuid)))
                devGuid = GUID_NULL;
        if (RegGetValue(regKey, nullptr, L"ffb", RRF_RT_REG_DWORD, nullptr, &ffb, &sz))
            ffb = FFBTYPE_60HZ;
        if (RegGetValue(regKey, nullptr, L"testFFB", RRF_RT_REG_DWORD, nullptr, &testFFB, &sz))
            testFFB = FFBTYPE_360HZ;
        if (RegGetValue(regKey, nullptr, L"maxForce", RRF_RT_REG_DWORD, nullptr, &maxForce, &sz))
            maxForce = 45;
        if (RegGetValue(regKey, nullptr, L"minForce", RRF_RT_REG_DWORD, nullptr, &minForce, &sz))
            minForce = 0;

    }
    else {
        ffb = FFBTYPE_60HZ;
        testFFB = FFBTYPE_360HZ;
        minForce = 0;
        maxForce = 45;
    }

}

void writeSettings() {

    wchar_t *guid;
    HKEY regKey;
    DWORD sz = sizeof(int);

    RegCreateKeyEx(
        HKEY_CURRENT_USER, KEY_PATH, 0, nullptr, 
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PATH, 0, KEY_ALL_ACCESS, &regKey)) {

        if (SUCCEEDED(StringFromCLSID(devGuid, (LPOLESTR *)&guid))) {
            int len = (lstrlenW(guid) + 1) * sizeof(wchar_t);
            RegSetValueEx(regKey, L"device", 0, REG_SZ, (BYTE *)guid, len);
        }
        RegSetValueEx(regKey, L"ffb",      0, REG_DWORD, (BYTE *)&ffb,      sz);
        RegSetValueEx(regKey, L"testFFB",  0, REG_DWORD, (BYTE *)&testFFB,  sz);
        RegSetValueEx(regKey, L"maxForce", 0, REG_DWORD, (BYTE *)&maxForce, sz);
        RegSetValueEx(regKey, L"minForce", 0, REG_DWORD, (BYTE *)&minForce, sz);

    }

}

void initAll() {

    initVJD();
    initDirectInput();

}

void releaseAll() {

    if (effect) {
        setFFB(0);
        effect->Stop();
        effect->Release();
        effect = nullptr;
    }
    if (ffdevice) {
        ffdevice->Unacquire();
        ffdevice->Release();
        ffdevice = nullptr;
    }
    if (pDI) {
        pDI->Release();
        pDI = nullptr;
    }

    RelinquishVJD(VJOY_DEVICEID);

    irsdk_shutdown();

}