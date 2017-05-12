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

#define MAX_FFB_DEVICES 16
#define DI_MAX 10000
#define MINFORCE_MULTIPLIER 100
#define SUSTEXFORCE_MULTIPLIER 160
#define SUSLOADFORCE_MULTIPLIER 8
#define STOPS_MAXFORCE_RAD 0.175f // 10 deg
#define DIRECT_INTERP_SAMPLES 6
#define KEY_PATH L"Software\\irFFB\\Settings"

enum ffbType {
    FFBTYPE_360HZ,
    FFBTYPE_360HZ_INTERP,
    FFBTYPE_DIRECT_FILTER,
    FFBTYPE_UNKNOWN
};

wchar_t *ffbTypeString(int type) {

    switch (type) {
        case FFBTYPE_360HZ:         return L"360 Hz";
        case FFBTYPE_360HZ_INTERP:  return L"360 Hz interpolated";
        case FFBTYPE_DIRECT_FILTER: return L"60 Hz direct filtered";
        default:                    return L"Unknown FFB type";
    }
}

wchar_t *ffbTypeShortString(int type) {

    switch (type) {
        case FFBTYPE_360HZ:         return L"360 Hz";
        case FFBTYPE_360HZ_INTERP:  return L"360 Hz I";
        case FFBTYPE_DIRECT_FILTER: return L"60 Hz DF";
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

float firc[] = { 0.0135009f, 0.0903209f, 0.2332305f, 0.3240389f, 0.2607993f, 0.0777116f };

int ffb;
int force = 0, minForce = 0, maxForce = 0;
float susTexFactor = 0, susLoadFactor = 0;
float scaleFactor = 0;
volatile float suspForce = 0;
__declspec(align(16)) volatile float suspForceST[DIRECT_INTERP_SAMPLES];
bool stopped = true, use360ForDirect = true;

int numButtons, numPov;

HANDLE wheelEvent = CreateEvent(nullptr, false, false, L"WheelEvent");
HANDLE ffbEvent   = CreateEvent(nullptr, false, false, L"FFBEvent");

HWND mainWnd, textWnd, devWnd, ffbWnd;
HWND minWnd, maxWnd, susTexWnd, susLoadWnd, use360Wnd;

LARGE_INTEGER freq;

int vjDev = 1;

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

    ResetVJD(vjDev);

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

        UpdateVJD(vjDev, (PVOID)&vjData);

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

        QueryPerformanceCounter(&start);

        s = (float)force;

        if (!use360ForDirect)
            s += scaleTorque(suspForce);

        for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {

            prod[i] = s * firc[i];
            output[i] = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5];

            if (use360ForDirect)
                output[i] += scaleTorque(suspForceST[i]);

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
    float *speed = nullptr, *throttle = nullptr;
    float *LFshockDeflST = nullptr, *RFshockDeflST = nullptr;
    float LFshockDeflLast, RFshockDeflLast, LFshockNom, RFshockNom, FshockNom;
    bool *isOnTrack = nullptr;
    int *trackSurface = nullptr;

    bool wasOnTrack = false;
    int numHandles = 0, dataLen = 0;
    int swTSTnumSamples = 0, swTSTmaxIdx = 0;
    float halfSteerMax = 0, lastTorque = 0, lastSuspForce = 0;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IRFFB));

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IRFFB, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

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

            // Inform iRacing of the maxForce setting
            irsdk_broadcastMsg(irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)maxForce);

            swTorque = floatvarptr(data, "SteeringWheelTorque");
            swTorqueST = floatvarptr(data, "SteeringWheelTorque_ST");
            steer = floatvarptr(data, "SteeringWheelAngle");
            steerMax = floatvarptr(data, "SteeringWheelAngleMax");
            speed = floatvarptr(data, "Speed");
            throttle = floatvarptr(data, "Throttle");
            isOnTrack = boolvarptr(data, "IsOnTrack");
            trackSurface = intvarptr(data, "PlayerTrackSurface");

            RFshockDeflST = floatvarptr(data, "RFshockDefl_ST");
            LFshockDeflST = floatvarptr(data, "LFshockDefl_ST");

            int swTorqueSTidx = irsdk_varNameToIndex("SteeringWheelTorque_ST");
            swTSTnumSamples = irsdk_getVarHeaderEntry(swTorqueSTidx)->count;
            swTSTmaxIdx = swTSTnumSamples - 1;

            lastTorque = 0.0f;
            wasOnTrack = false;
            irConnected = true;

        }

        res = MsgWaitForMultipleObjects(numHandles, handles, FALSE, 1000, QS_ALLINPUT);

        if (numHandles > 0 && res == numHandles - 1 && irsdk_getNewData(data)) {

            if (wasOnTrack && !*isOnTrack) {
                wasOnTrack = false;
                text(L"Has left track");
                lastTorque = suspForce = 0;
                for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++)
                    suspForceST[i] = 0;
                force = 0;
                setFFB(0);
            }

            else if (!wasOnTrack && *isOnTrack) {
                wasOnTrack = true;
                text(L"Is now on track");
                reacquireDIDevice();
                RFshockDeflLast = LFshockDeflLast = -10000;
                FshockNom = LFshockNom = RFshockNom = 0;
            }

            if (*speed > 1) {

                if (
                    LFshockDeflST != nullptr && RFshockDeflST != nullptr &&
                    (susTexFactor != 0 || susLoadFactor != 0)
                ) {

                    if (LFshockDeflLast != -10000) {

                        if (ffb != FFBTYPE_DIRECT_FILTER || use360ForDirect) {

                            __asm {
                                mov eax, LFshockDeflST
                                mov ecx, RFshockDeflST
                                movups xmm0, xmmword ptr [eax]
                                movups xmm1, xmmword ptr [ecx]
                                // xmm2 = LFdefl[0,1,2,3]
                                movaps xmm2, xmm0
                                // xmm3 = RFdefl[0,1,2,3]
                                movaps xmm3, xmm1
                                // xmm0 = LFdefl[-,1,2,3]
                                pslldq xmm0, 4
                                movss xmm4, LFshockDeflLast
                                // xmm1 = LFdefl[-,1,2,3]
                                pslldq xmm1, 4
                                movss xmm5, RFshockDeflLast
                                // xmm0 = LSdefl[LFlast,0,1,2]
                                movss xmm0, xmm4
                                // xmm6 = LFdefl[0,1,2,3]
                                movaps xmm6, xmm2
                                // xmm2 = LFdefl[0] - LFlast, LFdefl[1] - LFdefl[0], ...
                                subps xmm2, xmm0
                                // xmm1 = RFdefl[RFlast,0,1,2]
                                movss xmm1, xmm5
                                // xmm7 = RFdefl[0,1,2,3]
                                movaps xmm7, xmm3
                                // xmm3 = RFdefl[0] - RFlast, RFdefl[1] - RFdefl[0], ...
                                subps xmm3, xmm1
                                // xmm4 = LFdefl[3,4,5]
                                movups xmm4, xmmword ptr [eax + 12]
                                // xmm1 = LFdefl[4,5]
                                movlps xmm1, qword ptr [eax + 16]
                                // xmm1 = LFdefl[4] - LFdefl[3], LFdefl[5] - LFdefl[4]
                                subps xmm1, xmm4
                                // xmm5 = RFdefl[3,4,5]
                                movups xmm5, xmmword ptr [ecx + 12]
                                // xmm0 = RFdefl[4,5]
                                movlps xmm0, qword ptr [ecx + 16]
                                // xmm2 = delta[0,1,2,3]
                                subps xmm2, xmm3
                                // xmm0 = RFdefl[4] - RFdefl[3], RFdefl[5] - RFdefl[4]
                                subps xmm0, xmm5
                                // xmm3 = susTexFactor
                                movss xmm3, susTexFactor
                                // xmm1 = delta[4,5]
                                subps xmm1, xmm0
                                unpcklps xmm3, xmm3
                                unpcklps xmm3, xmm3
                                mulps xmm2, xmm3
                                mulps xmm1, xmm3
                                movaps xmmword ptr suspForceST[0], xmm2
                                movlps qword ptr suspForceST[16], xmm1
                               
                            }
                            
                            if (FshockNom != 0 && susLoadFactor != 0)
                                __asm {
                                    // xmm6 = LFdefl[0,1,2,3]
                                    // xmm7 = RFdefl[0,1,2,3]
                                    // xmm4 = LFdefl[3,4,5]
                                    // xmm5 = RFdefl[3,4,5]
                                    movss xmm0, LFshockNom
                                    movss xmm3, RFshockNom
                                    unpcklps xmm0, xmm0
                                    // xmm1 = LFdefl[0,1,2,3]
                                    movaps xmm1, xmm6
                                    unpcklps xmm3, xmm3
                                    unpcklps xmm0, xmm0
                                    // xmm2 = RFdefl[0,1,2,3]
                                    movaps xmm2, xmm7
                                    unpcklps xmm3, xmm3
                                    // xmm6 = LFdefl[0,1,2,3] - LFshockNom
                                    subps xmm6, xmm0
                                    // xmm7 = RFdefl[0,1,2,3] - RFshockNom
                                    subps xmm7, xmm3
                                    // xmm4 = LFdefl[4,5]
                                    psrldq xmm4, 4
                                    // xmm6 = (LFdefl - LFshockNom) - (RFdefl - RFshockNom)[0,1,2,3]
                                    subps xmm6, xmm7
                                    // xmm7 = LFdefl[4,5]
                                    movaps xmm7, xmm4
                                    // xmm5 = RFdefl[4,5]
                                    psrldq xmm5, 4
                                    // xmm4 = LFdefl[4,5] - LFshockNom
                                    subps xmm4, xmm0
                                    // xmm0 = RFdefl[4,5]
                                    movaps xmm0, xmm5
                                    // xmm5 = RFdefl[4,5] - RFshockNom
                                    subps xmm5, xmm3
                                    // xmm1 = LFdefl + RFdefl [0,1,2,3]
                                    addps xmm1, xmm2
                                    // xmm3 = FshockNom
                                    movss xmm3, FshockNom
                                    // xmm4 = (LFdefl - LFshockNom) - (RFdefl - RFshockNom)[4,5]
                                    subps xmm4, xmm5
                                    unpcklps xmm3, xmm3
                                    // xmm0 = LFdefl + RFdefl [4, 5]
                                    addps xmm0, xmm7
                                    unpcklps xmm3, xmm3
                                    movss xmm5, susLoadFactor
                                    // xmm1 = LFdefl + RFdefl / FshockNom [0,1,2,3]
                                    divps xmm1, xmm3
                                    unpcklps xmm5, xmm5
                                    // xmm0 = LFdefl + RFdefl / FshockNom [4,5]
                                    divps xmm0, xmm3
                                    // xmm0 = (LFdefl + RFdefl / FshockNom) ^ 8 [4,5] 
                                    // xmm1 = (LFdefl + RFdefl / FshockNom) ^ 8 [0,1,2,3] 
                                    mulps xmm1, xmm1
                                    mulps xmm0, xmm0
                                    mulps xmm1, xmm1
                                    unpcklps xmm5, xmm5
                                    mulps xmm0, xmm0
                                    mulps xmm1, xmm1
                                    mulps xmm0, xmm0
                                    // xmm1 = ((LFdefl - LFnom) - (RFdefl - RFnom)) * ((LFdefl + RFdefl) / Fnom) ^ 8) [0,1,2,3]
                                    mulps xmm1, xmm6
                                    movaps xmm2, xmmword ptr suspForceST[0]
                                    // xmm0 = ((LFdefl - LFnom) - (RFdefl - RFnom)) * ((LFdefl + RFdefl) / Fnom) ^ 8) [4,5]
                                    mulps xmm0, xmm4
                                    // xmm1 *= susLoadFactor
                                    mulps xmm1, xmm5
                                    movlps xmm3, qword ptr suspForceST[16]
                                    // xmm0 *= susLoadFactor
                                    mulps xmm0, xmm5
                                    // add to suspForceST
                                    addps xmm2, xmm1
                                    addps xmm3, xmm0
                                    // write
                                    movaps xmmword ptr suspForceST[0], xmm2
                                    movlps qword ptr suspForceST[16], xmm3
                                }
        
                        }
                        else {
                            float LFdelta = LFshockDeflST[swTSTmaxIdx] - LFshockDeflLast;
                            float RFdelta = RFshockDeflST[swTSTmaxIdx] - RFshockDeflLast;
                            suspForce = (LFdelta - RFdelta) * (susTexFactor / 4);
                            if (FshockNom != 0 && susLoadFactor != 0)
                                suspForce +=
                                    ((LFshockDeflST[swTSTmaxIdx] - LFshockNom) - (RFshockDeflST[swTSTmaxIdx] - RFshockNom)) *
                                        pow((LFshockDeflST[swTSTmaxIdx] + RFshockDeflST[swTSTmaxIdx]) / FshockNom, 8) *
                                            susLoadFactor;
                        }
            
                    }

                    RFshockDeflLast = RFshockDeflST[swTSTmaxIdx];
                    LFshockDeflLast = LFshockDeflST[swTSTmaxIdx];

                }

                stopped = false;

            }
            else {
                stopped = true;
                if (
                    LFshockDeflST != nullptr && RFshockDeflST != nullptr &&
                    *trackSurface == irsdk_InPitStall && *throttle == 0
                ) {
                    LFshockNom = LFshockDeflST[swTSTmaxIdx];
                    RFshockNom = RFshockDeflST[swTSTmaxIdx];
                    FshockNom = LFshockNom + RFshockNom;
                }
            }

            if (!*isOnTrack || ffb == FFBTYPE_DIRECT_FILTER)
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

                case FFBTYPE_360HZ: {

                    QueryPerformanceCounter(&start);

                    for (int i = 0; i < swTSTmaxIdx; i++) {

                        setFFB(scaleTorque(swTorqueST[i] + suspForceST[i]));
                        sleepSpinUntil(&start, 2000, 2760 * (i + 1));

                    }
                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx] + suspForceST[swTSTmaxIdx]));
                }
                break;

                case FFBTYPE_360HZ_INTERP: {

                    QueryPerformanceCounter(&start);

                    float diff = swTorqueST[0] - lastTorque;
                    float sdiff = suspForceST[0] - lastSuspForce;
                    setFFB(scaleTorque(lastTorque + diff / 2 + lastSuspForce + sdiff / 2));
                    sleepSpinUntil(&start, 0, 1380);

                    for (int i = 0; i < swTSTmaxIdx << 1; i++) {

                        int idx = i >> 1;

                        if (i & 1) {
                            diff = swTorqueST[idx + 1] - swTorqueST[idx];
                            sdiff = suspForceST[idx + 1] - suspForceST[idx];
                            setFFB(scaleTorque(swTorqueST[idx] + diff / 2 + suspForceST[idx] + sdiff / 2));
                        }
                        else
                            setFFB(scaleTorque(swTorqueST[idx] + suspForceST[idx]));

                        sleepSpinUntil(&start, 0, 1380 * (i + 2));

                    }

                    setFFB(scaleTorque(swTorqueST[swTSTmaxIdx] + suspForceST[swTSTmaxIdx]));
                    lastTorque = swTorqueST[swTSTmaxIdx];
                    lastSuspForce = suspForceST[swTSTmaxIdx];

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
        CW_USEDEFAULT, CW_USEDEFAULT, 432, 840,
        NULL, NULL, hInstance, NULL
    );

    if (!mainWnd)
        return FALSE;

    combo(&devWnd, L"FFB device:", 20);

    combo(&ffbWnd, L"FFB type:", 80);

    for (int i = 0; i < FFBTYPE_UNKNOWN; i++)
        SendMessage(ffbWnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ffbTypeString(i)));

    SendMessage(ffbWnd, CB_SETCURSEL, ffb, 0);

    slider(&minWnd, L"Min force:", 144, L"0", L"20");
    SendMessage(minWnd, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
    SendMessage(minWnd, TBM_SETPOS, TRUE, minForce / MINFORCE_MULTIPLIER);
    SendMessage(minWnd, TBM_SETPOSNOTIFY, 0, minForce / MINFORCE_MULTIPLIER);

    slider(&maxWnd, L"Max force:", 216, L"5 Nm", L"65 Nm");
    SendMessage(maxWnd, TBM_SETRANGE, TRUE, MAKELPARAM(5, 65));
    SendMessage(maxWnd, TBM_SETPOS, TRUE, maxForce);
    SendMessage(maxWnd, TBM_SETPOSNOTIFY, 0, maxForce);

    slider(&susTexWnd, L"Suspension bumps:", 288, L"0", L"100");
    SendMessage(susTexWnd, TBM_SETPOS, TRUE, (int)(susTexFactor / SUSTEXFORCE_MULTIPLIER));
    SendMessage(susTexWnd, TBM_SETPOSNOTIFY, 0, (int)(susTexFactor / SUSTEXFORCE_MULTIPLIER));

    slider(&susLoadWnd, L"Suspension load:", 360, L"0", L"100");
    SendMessage(susLoadWnd, TBM_SETPOS, TRUE, (int)(susLoadFactor / SUSLOADFORCE_MULTIPLIER));
    SendMessage(susLoadWnd, TBM_SETPOSNOTIFY, 0, (int)(susLoadFactor / SUSLOADFORCE_MULTIPLIER));

    use360Wnd = CreateWindowExW(
        NULL, L"BUTTON", L" Use 360 Hz telemetry for suspension effects\r\n in direct modes?",
        BS_CHECKBOX | BS_MULTILINE | WS_CHILD | WS_VISIBLE,
        30, 440, 360, 58, mainWnd, nullptr, hInstance, nullptr
    );
    if (use360ForDirect)
        SendMessage(use360Wnd, BM_SETCHECK, BST_CHECKED, NULL);

    if (ffb != FFBTYPE_DIRECT_FILTER)
        EnableWindow(use360Wnd, false);

    textWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_VSCROLL | WS_CHILD | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
        16, 512, 384, 240,
        mainWnd, NULL, hInstance, NULL
    );
    SendMessage(textWnd, EM_SETLIMITTEXT, WPARAM(256000), 0);

    text(L"irFFB");

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

                            ffb = (int)SendMessage(ffbWnd, CB_GETCURSEL, 0, 0);
                            EnableWindow(use360Wnd, ffb == FFBTYPE_DIRECT_FILTER);

                        }

                    }
                    else if (HIWORD(wParam) == BN_CLICKED) {
                        if ((HWND)lParam == use360Wnd) {
                            bool oldValue = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                            use360ForDirect = !oldValue;
                            SendMessage((HWND)lParam, BM_SETCHECK, use360ForDirect, 0);
                        }
                    }
                    return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

        case WM_HSCROLL: {
            if ((HWND)lParam == maxWnd) {
                maxForce = (SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
                scaleFactor = (float)DI_MAX / maxForce;
                irsdk_broadcastMsg(
                    irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)maxForce
                );
            }
            else if ((HWND)lParam == minWnd)
                minForce = (SendMessage((HWND)lParam, TBM_GETPOS, 0, 0)) * MINFORCE_MULTIPLIER;
            else if ((HWND)lParam == susTexWnd)
                susTexFactor = (float)(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0)) * SUSTEXFORCE_MULTIPLIER;
            else if ((HWND)lParam == susLoadWnd)
                susLoadFactor = (float)(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0)) * SUSLOADFORCE_MULTIPLIER;
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

    if (lstrcmp(diDevInst->tszProductName, L"vJoy Device") == 0)
        return true;

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
        text(L"Did you select the correct device and is it powered on and connected?");
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

    t *= scaleFactor;

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

    if (stopped)
        mag /= 4;

    pforce.lOffset = mag;

    effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);

}

void CALLBACK vjFFBCallback(PVOID ffbPacket, PVOID data) {

    FFBPType type;
    FFB_EFF_CONSTANT constEffect;
    int16_t mag;

    if (ffb != FFBTYPE_DIRECT_FILTER)
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
    int maxVjDev;
    VjdStat vjdStatus;

    if (!vJoyEnabled()) {
        text(L"vJoy Not Enabled!");
        return false;
    }
    else if (!DriverMatch(&verDll, &verDrv)) {
        text(L"vJoy driver version %04x != DLL version %04x!", verDrv, verDll);
        return false;
    }
    else
        text(L"vJoy driver version %04x init OK", verDrv);

    vjDev = 1;

    if (!GetvJoyMaxDevices(&maxVjDev)) {
        text(L"Failed to determine max number of vJoy devices");
        return false;
    }

    while (vjDev <= maxVjDev) {

        vjdStatus = GetVJDStatus(vjDev);

        if (vjdStatus == VJD_STAT_BUSY || vjdStatus == VJD_STAT_MISS)
            goto NEXT;
        if (!GetVJDAxisExist(vjDev, HID_USAGE_X))
            goto NEXT;
        if (!IsDeviceFfb(vjDev))
            goto NEXT;
        if (
            !IsDeviceFfbEffect(vjDev, HID_USAGE_CONST) ||
            !IsDeviceFfbEffect(vjDev, HID_USAGE_SINE)  ||
            !IsDeviceFfbEffect(vjDev, HID_USAGE_DMPR)  ||
            !IsDeviceFfbEffect(vjDev, HID_USAGE_FRIC)  ||
            !IsDeviceFfbEffect(vjDev, HID_USAGE_SPRNG)
        ) {
            text(L"vjDev %d: Not all required FFB effects are enabled", vjDev);
            text(L"Enable all FFB effects to use this device");
            goto NEXT;
        }
        break;

NEXT:
        vjDev++;

    }

    if (vjDev > maxVjDev) {
        text(L"Failed to find suitable vJoy device!");
        text(L"Create a device with an X axis and all FFB effects enabled");
        return false;
    }

    if (vjdStatus == VJD_STAT_OWN) {
        RelinquishVJD(vjDev);
        vjdStatus = GetVJDStatus(vjDev);
    }
    if (vjdStatus == VJD_STAT_FREE) {
        if (!AcquireVJD(vjDev)) {
            text(L"Failed to acquire vJoy device %d!", vjDev);
            return false;
        }
    }
    else {
        text(L"ERROR: vJoy device %d status is %d", vjDev, vjdStatus);
        return false;
    }

    text(L"Acquired vJoy device %d", vjDev);
    FfbRegisterGenCB(vjFFBCallback, NULL);
    ResetVJD(vjDev);

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
            ffb = FFBTYPE_DIRECT_FILTER;
        if (RegGetValue(regKey, nullptr, L"maxForce", RRF_RT_REG_DWORD, nullptr, &maxForce, &sz))
            maxForce = 45;
        scaleFactor = (float)DI_MAX / maxForce;
        if (RegGetValue(regKey, nullptr, L"minForce", RRF_RT_REG_DWORD, nullptr, &minForce, &sz))
            minForce = 0;
        if (RegGetValue(regKey, nullptr, L"susTexFactor", RRF_RT_REG_DWORD, nullptr, &susTexFactor, &sz))
            susTexFactor = 0;
        if (RegGetValue(regKey, nullptr, L"susLoadFactor", RRF_RT_REG_DWORD, nullptr, &susLoadFactor, &sz))
            susLoadFactor = 0;
        if (RegGetValue(regKey, nullptr, L"use360ForDirect", RRF_RT_REG_DWORD, nullptr, &use360ForDirect, &sz))
            use360ForDirect = true;

    }
    else {
        ffb = FFBTYPE_DIRECT_FILTER;
        minForce = 0;
        maxForce = 45;
        scaleFactor = DI_MAX / 45;
        susTexFactor = 0;
        susLoadFactor = 0;
        use360ForDirect = true;
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
        RegSetValueEx(regKey, L"ffb",        0, REG_DWORD, (BYTE *)&ffb,        sz);
        RegSetValueEx(regKey, L"susTexFactor", 0, REG_DWORD, (BYTE *)&susTexFactor, sz);
        RegSetValueEx(regKey, L"susLoadFactor", 0, REG_DWORD, (BYTE *)&susLoadFactor, sz);
        RegSetValueEx(regKey, L"maxForce",   0, REG_DWORD, (BYTE *)&maxForce,   sz);
        RegSetValueEx(regKey, L"minForce",   0, REG_DWORD, (BYTE *)&minForce,   sz);
        RegSetValueEx(regKey, L"use360ForDirect", 0, REG_DWORD, (BYTE *)&use360ForDirect, sz);

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

    RelinquishVJD(vjDev);

    irsdk_shutdown();

}