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

#include "irFFB.h"
#include "Settings.h"
#include "jetseat.h"
#include "fan.h"
#include "public.h"
#include "yaml_parser.h"
#include "vjoyinterface.h"

/*
#pragma comment(lib, "comctl32.lib")
#include <commctrl.h >
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken = '6595b64144ccf1df' language = '*'\"")
*/

#define MAX_LOADSTRING 100

#define STATUS_CONNECTED_PART 0
#define STATUS_ONTRACK_PART 1
#define STATUS_CAR_PART 2

extern HANDLE hDataValidEvent;

// Globals
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

LPDIRECTINPUT8 pDI = nullptr;
LPDIRECTINPUTDEVICE8 ffdevice = nullptr;
LPDIRECTINPUTEFFECT effect = nullptr;

DIJOYSTATE joyState;
DWORD axes[1] = { DIJOFS_X };
LONG  dir[1]  = { 0 };
DIPERIODIC pforce;
DIEFFECT   dieff;

Settings settings;
JetSeat *jetseat;
Fan *fan;

float firc6[] = { 
    0.0777116f, 0.2607993f, 0.3240389f, 0.2332305f, 0.0903209f, 0.0135009f 
};
float firc12[] = { 
    0.0483102f, 0.0886167f, 0.1247764f, 0.1501189f, 0.1582776f, 0.1456553f, 
    0.1193849f, 0.0846268f, 0.0456667f, 0.0148898f, 0.0032250f, 0.0164516f 
};

char car[MAX_CAR_NAME];

int force = 0;
volatile float suspForce = 0; 
volatile float yawForce[DIRECT_INTERP_SAMPLES];
__declspec(align(16)) volatile float suspForceST[DIRECT_INTERP_SAMPLES];
bool stopped = true;

int numButtons = 0, numPov = 0, vjButtons = 0, vjPov = 0;
UINT samples, clippedSamples;

HANDLE wheelEvent = CreateEvent(nullptr, false, false, L"WheelEvent");
HANDLE ffbEvent   = CreateEvent(nullptr, false, false, L"FFBEvent");

HWND mainWnd, textWnd, statusWnd;

LARGE_INTEGER freq;

int vjDev = 1;
FFB_DATA ffbPacket;

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

    UNREFERENCED_PARAMETER(lParam);

    HRESULT res;
    JOYSTICK_POSITION vjData;
    ResetVJD(vjDev);

    while (true) {

        WaitForSingleObject(wheelEvent, INFINITE);

        if (!ffdevice)
            continue;

        res = ffdevice->GetDeviceState(sizeof(joyState), &joyState);
        if (res == DIERR_NOTACQUIRED || res == DIERR_INPUTLOST)
            continue;

        vjData.wAxisX = joyState.lX;
        vjData.wAxisY = joyState.lY;
        vjData.wAxisZ = joyState.lZ;

        if (vjButtons > 0)
            for (int i = 0; i < numButtons; i++) {
                if (joyState.rgbButtons[i])
                    vjData.lButtons |= 1 << i;
                else
                    vjData.lButtons &= ~(1 << i);
            }

        // This could be wrong, untested..
        if (vjPov > 0)
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

    UNREFERENCED_PARAMETER(lParam);
    int16_t mag;

    float s, r;
    __declspec(align(16)) float prod[12];
    __declspec(align(16)) float inter[4];
    LARGE_INTEGER start;

    while (true) {

        bool use360 = settings.getUse360ForDirect();

        // Signalled when force has been updated
        WaitForSingleObject(ffbEvent, INFINITE);

        if (
            settings.getFfbType() != FFBTYPE_DIRECT_FILTER &&
            settings.getFfbType() != FFBTYPE_DIRECT_FILTER_720
        )
            continue;
        
        if (((ffbPacket.data[0] & 0xF0) >> 4) != vjDev)
            continue;
        
        mag = (ffbPacket.data[3] << 8) + ffbPacket.data[2];

        QueryPerformanceCounter(&start);

        // sign extend
        force = mag;

        s = (float)force;

        if (!use360)
            s += scaleTorque(suspForce);

        if (settings.getFfbType() == FFBTYPE_DIRECT_FILTER_720) {

            prod[0] = s * firc12[0];

            _asm {
                movaps xmm0, xmmword ptr prod
                movaps xmm1, xmmword ptr prod+16
                movaps xmm2, xmmword ptr prod+32
                addps xmm0, xmm1
                addps xmm0, xmm2
                movaps xmmword ptr inter, xmm0
            }

            r = inter[0] + inter[1] + inter[2] + inter[3] + scaleTorque(yawForce[0]);

            if (use360)
                r += scaleTorque(suspForceST[0]);

            setFFB((int)r);

            for (int i = 1; i < DIRECT_INTERP_SAMPLES * 2 - 1; i++) {

                prod[i] = s * firc12[i];

                _asm {
                    movaps xmm0, xmmword ptr prod
                    movaps xmm1, xmmword ptr prod + 16
                    movaps xmm2, xmmword ptr prod + 32
                    addps xmm0, xmm1
                    addps xmm0, xmm2
                    movaps xmmword ptr inter, xmm0
                }

                r = inter[0] + inter[1] + inter[2] + inter[3];

                int idx = i >> 1;
                bool odd = i & 1;

                if (use360)
                    r +=
                        scaleTorque(
                            odd ? 
                                suspForceST[idx]  + (suspForceST[idx + 1] - suspForceST[idx]) / 2 :
                                suspForceST[idx]
                        );

                r += 
                    scaleTorque(
                        odd ?
                            yawForce[idx] + (yawForce[idx + 1] - yawForce[idx]) / 2 :
                            yawForce[idx]
                );

                sleepSpinUntil(&start, 0, 1380 * i);
                setFFB((int)r);

            }

            prod[DIRECT_INTERP_SAMPLES * 2 - 1] = s * firc12[DIRECT_INTERP_SAMPLES * 2 - 1];
            _asm {
                movaps xmm0, xmmword ptr prod
                movaps xmm1, xmmword ptr prod + 16
                movaps xmm2, xmmword ptr prod + 32
                addps xmm0, xmm1
                addps xmm0, xmm2
                movaps xmmword ptr inter, xmm0
            }

            r = inter[0] + inter[1] + inter[2] + inter[3];

            if (use360)
                r += scaleTorque(suspForceST[DIRECT_INTERP_SAMPLES - 1]);

            r += scaleTorque(yawForce[DIRECT_INTERP_SAMPLES - 1]);
            setFFB((int)r);

            continue;

        }
            
        prod[0] = s * firc6[0];
        r = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5] + scaleTorque(yawForce[0]);

        if (use360)
            r += scaleTorque(suspForceST[0]);

        setFFB((int)r);

        for (int i = 1; i < DIRECT_INTERP_SAMPLES; i++) {

            prod[i] = s * firc6[i];
            r = prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5] + scaleTorque(yawForce[i]);

            if (use360)
                r += scaleTorque(suspForceST[i]);

            sleepSpinUntil(&start, 2000, 2760 * i);
            setFFB((int)r);

        }

    }

    return 0;

}

void resetForces() {
    suspForce = 0;
    for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {
        suspForceST[i] = 0;
        yawForce[i] =  0;
    }
    force = 0;
    setFFB(0);
}

boolean getCarName() {

    char buf[64];
    const char *ptr;
    int len = -1, carIdx = -1;

    car[0] = 0;

    // Get car idx
    if (!parseYaml(irsdk_getSessionInfoStr(), "DriverInfo:DriverCarIdx:", &ptr, &len))
        return false;

    if (len < 0 || len > sizeof(buf) - 1)
        return false;
    
    memcpy(buf, ptr, len);
    buf[len] = 0;
    carIdx = atoi(buf);

    // Get car path
    sprintf_s(buf, "DriverInfo:Drivers:CarIdx:{%d}CarPath:", carIdx);
    if (!parseYaml(irsdk_getSessionInfoStr(), buf, &ptr, &len))
        return false;
    if (len < 0 || len > sizeof(car) - 1)
        return false;

    memcpy(car, ptr, len);
    car[len] = 0;

    return true;
      
}

float getCarRedline() {

    char buf[64];
    const char *ptr;
    int len = -1;

    if (parseYaml(irsdk_getSessionInfoStr(), "DriverInfo:DriverCarRedLine:", &ptr, &len)) {

        if (len < 0 || len > sizeof(buf) - 1)
            return 8000;
        
        memcpy(buf, ptr, len);
        buf[len] = 0;
        return strtof(buf, NULL);
    }
    
    return 8000;

}

void clippingReport() {

    UINT clippedPerCent = samples > 0 ? clippedSamples * 100 / samples : 0;
    text(L"%u%% of samples were clipped", clippedPerCent);
    if (clippedPerCent > 10)
        text(L"Consider increasing Max force to reduce clipping");
    samples = clippedSamples = 0;

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
    DWORD diState;
    HRESULT hr;

    float *swTorque = nullptr, *swTorqueST = nullptr, *steer = nullptr, *steerMax = nullptr;
    float *speed = nullptr, *throttle = nullptr, *rpm = nullptr;
    float *LFshockDeflST = nullptr, *RFshockDeflST = nullptr, *CFshockDeflST = nullptr;
    float *LRshockDeflST = nullptr, *RRshockDeflST = nullptr;
    float *vX = nullptr, *vY = nullptr;
    float LFshockDeflLast = -10000, RFshockDeflLast = -10000, CFshockDeflLast = -10000;
    float LRshockDeflLast = -10000, RRshockDeflLast = -10000, FshockNom, yaw;
    bool *isOnTrack = nullptr;
    int *trackSurface = nullptr, *gear = nullptr;;

    bool wasOnTrack = false, shockNomSet = false;
    int numHandles = 0, dataLen = 0, lastGear = 0;
    int STnumSamples = 0, STmaxIdx = 0;
    float halfSteerMax = 0, lastTorque = 0, lastSuspForce = 0, redline;
    float yawFilter[DIRECT_INTERP_SAMPLES];

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IRFFB));

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_IRFFB, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

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

    fan = Fan::init();
    jetseat = JetSeat::init();
    if (!jetseat) {
        DeleteMenu(GetMenu(mainWnd), ID_SETTINGS_JETSEAT, MF_BYCOMMAND);
        DrawMenuBar(mainWnd);
    }

    memset(car, 0, sizeof(car));
    setCarStatus(car);
    setConnectedStatus(false);
    setOnTrackStatus(false);
    settings.readRegSettings(true, car);
    enumDirectInput();

    LARGE_INTEGER start;
    QueryPerformanceFrequency(&freq);

    initVJD();
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(
        CreateThread(NULL, 0, readWheelThread, NULL, 0, NULL), THREAD_PRIORITY_HIGHEST
    );
    SetThreadPriority(
        CreateThread(NULL, 0, directFFBThread, NULL, 0, NULL), THREAD_PRIORITY_HIGHEST
    );

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
            setConnectedStatus(true);

            if (getCarName() && settings.getUseCarSpecific()) {
                setCarStatus(car);
                settings.readSettingsForCar(car);
            }
            else 
                setCarStatus(nullptr);
    
            redline = getCarRedline();

            // Inform iRacing of the maxForce setting
            irsdk_broadcastMsg(irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)settings.getMaxForce());

            swTorque = floatvarptr(data, "SteeringWheelTorque");
            swTorqueST = floatvarptr(data, "SteeringWheelTorque_ST");
            steer = floatvarptr(data, "SteeringWheelAngle");
            steerMax = floatvarptr(data, "SteeringWheelAngleMax");
            speed = floatvarptr(data, "Speed");
            throttle = floatvarptr(data, "Throttle");
            rpm = floatvarptr(data, "RPM");
            gear = intvarptr(data, "Gear");
            isOnTrack = boolvarptr(data, "IsOnTrack");
            trackSurface = intvarptr(data, "PlayerTrackSurface");
            vX = floatvarptr(data, "VelocityX");
            vY = floatvarptr(data, "VelocityY");

            RFshockDeflST = floatvarptr(data, "RFshockDefl_ST");
            LFshockDeflST = floatvarptr(data, "LFshockDefl_ST");
            LRshockDeflST = floatvarptr(data, "LRshockDefl_ST");
            RRshockDeflST = floatvarptr(data, "RRshockDefl_ST");
            CFshockDeflST = floatvarptr(data, "CFshockDefl_ST");

            int swTorqueSTidx = irsdk_varNameToIndex("SteeringWheelTorque_ST");
            STnumSamples = irsdk_getVarHeaderEntry(swTorqueSTidx)->count;
            STmaxIdx = STnumSamples - 1;

            lastTorque = FshockNom = 0.0f;
            wasOnTrack = shockNomSet = false;
            resetForces();
            irConnected = true;

        }

        // Try to make sure we've retained our acquisition
        if (ffdevice) {
            hr = ffdevice->GetForceFeedbackState(&diState);
            if (
                hr == DIERR_INPUTLOST || hr == DIERR_NOTEXCLUSIVEACQUIRED ||
                hr == DIERR_NOTACQUIRED || diState == DIGFFS_DEVICELOST
            )
                reacquireDIDevice();
        }

        res = MsgWaitForMultipleObjects(numHandles, handles, FALSE, 1000, QS_ALLINPUT);

        QueryPerformanceCounter(&start);

        if (numHandles > 0 && res == numHandles - 1 && irsdk_getNewData(data)) {

            if (wasOnTrack && !*isOnTrack) {
                wasOnTrack = false;
                setOnTrackStatus(false);
                lastTorque = lastSuspForce = 0.0f;
                resetForces();
                fan->setManualSpeed();
                clippingReport();
            }

            else if (!wasOnTrack && *isOnTrack) {
                wasOnTrack = true;
                RFshockDeflLast = LFshockDeflLast = 
                    LRshockDeflLast = RRshockDeflLast = 
                        CFshockDeflLast = -10000.0f;
                clippedSamples = samples = lastGear = 0;
                memset(yawFilter, 0, DIRECT_INTERP_SAMPLES * sizeof(float));
                setOnTrackStatus(true);
            }

            if (jetseat && jetseat->isEnabled()) {
                if (*isOnTrack && *rpm > 0.0f) {
                    jetseat->startEngineEffect();
                    jetseat->updateEngineEffect(*rpm * 100.0f / redline);
                }
                else                
                    jetseat->stopEngineEffect();
            }

            yaw = 0;

            if (*speed > 1.0f) {
            
                float bumpsFactor = settings.getBumpsFactor();
                float loadFactor = settings.getLoadFactor();
                bool extraLongLoad = settings.getExtraLongLoad();
                bool use360  = settings.getUse360ForDirect();
                int ffbType = settings.getFfbType();

                if (*speed > 4.0f) {

                    float halfMaxForce = (float)(settings.getMaxForce() >> 1);
                    float r = *vY / *vX;
                    float sa, asa, ar = abs(r);

                    if (ar > 1.0f) {
                        yaw = csignf(halfMaxForce, r);
                        sa = csignf(0.25f, r);
                    }
                    else {
                        sa = 0.78539816339745f * r + 0.273f * r * (1.0f - ar);
                        asa = abs(sa);
                        sa *= 2.0f - asa;
                        yaw = minf(maxf(sa * settings.getYawFactor(), -halfMaxForce), halfMaxForce);
                    }

                    if (jetseat && jetseat->isEnabled() && asa > 0.05f)
                        jetseat->yawEffect(sa);

                }

                if (
                    LFshockDeflST != nullptr && RFshockDeflST != nullptr &&
                    (bumpsFactor != 0.0f || loadFactor != 0.0f)
                ) {

                    if (LFshockDeflLast != -10000.0f) {

                        if (ffbType != FFBTYPE_DIRECT_FILTER || use360) {

                            __asm {
                                mov eax, LFshockDeflST
                                mov ecx, RFshockDeflST
                                movups xmm0, xmmword ptr [eax]
                                movups xmm1, xmmword ptr [ecx]
                                // xmm2 = LFdefl[0,1,2,3]
                                movaps xmm2, xmm0
                                // xmm3 = RFdefl[0,1,2,3]
                                movaps xmm3, xmm1
                                // xmm0 = LFdefl[-,0,1,2]
                                pslldq xmm0, 4
                                movss xmm4, LFshockDeflLast
                                // xmm1 = RFdefl[-,0,1,2]
                                pslldq xmm1, 4
                                movss xmm5, RFshockDeflLast
                                // xmm0 = LFdefl[LFlast,0,1,2]
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
                                movss xmm3, bumpsFactor
                                // xmm1 = delta[4,5]
                                subps xmm1, xmm0
                                unpcklps xmm3, xmm3
                                unpcklps xmm3, xmm3
                                // xmm2 = delta[0,1,2,3] * bumpsFactor
                                mulps xmm2, xmm3
                                // xmm1 = delta[4,5] * bumpsFactor
                                mulps xmm1, xmm3
                                // write
                                movaps xmmword ptr suspForceST[0], xmm2
                                movlps qword ptr suspForceST[16], xmm1
                                movss xmm3, FshockNom
                                movss xmm2, loadFactor
                                xorps xmm1, xmm1
                                ucomiss xmm3, xmm1
                                jz end
                                ucomiss xmm2, xmm1
                                jz end    
                                
                                // xmm6 = LFdefl[0,1,2,3]
                                // xmm7 = RFdefl[0,1,2,3]
                                // xmm4 = LFdefl[3,4,5]
                                // xmm5 = RFdefl[3,4,5]
                                // xmm3 = Fnom
                                // eax = 2.0f
                                mov eax, 0x40000000
                                // xmm0 = Fnom 
                                movaps xmm0, xmm3
                                // xmm2 = 2.0f
                                movd xmm2, eax
                                // xmm1 = LFdefl[0,1,2,3]
                                movaps xmm1, xmm6
                                // xmm0 = Fnom / 2
                                divss xmm0, xmm2
                                unpcklps xmm3, xmm3
                                unpcklps xmm0, xmm0
                                // xmm2 = RFdefl[0,1,2,3]
                                movaps xmm2, xmm7
                                unpcklps xmm0, xmm0
                                // xmm6 = LFdefl[0,1,2,3] - Fnom/2
                                subps xmm6, xmm0
                                unpcklps xmm3, xmm3
                                // xmm7 = RFdefl[0,1,2,3] - Fnom/2
                                subps xmm7, xmm0
                                // xmm4 = LFdefl[4,5]
                                psrldq xmm4, 4
                                // xmm6 = (LFdefl - Fnom/2) - (RFdefl - Fnom/2)[0,1,2,3]
                                subps xmm6, xmm7
                                // xmm7 = LFdefl[4,5]
                                movaps xmm7, xmm4
                                // xmm5 = RFdefl[4,5]
                                psrldq xmm5, 4
                                // xmm4 = LFdefl[4,5] - Fnom/2
                                subps xmm4, xmm0
                                // xmm7 = LFdefl + RFdefl [4,5]
                                addps xmm7, xmm5
                                // xmm5 = RFdefl[4,5] - Fnom/2
                                subps xmm5, xmm0
                                // xmm1 = LFdefl + RFdefl [0,1,2,3]
                                addps xmm1, xmm2
                                // xmm4 = (LFdefl - Fnom/2) - (RFdefl - Fnom/2)[4,5]
                                subps xmm4, xmm5
                                movss xmm5, loadFactor
                                // xmm1 = LFdefl + RFdefl / Fnom [0,1,2,3]
                                divps xmm1, xmm3
                                unpcklps xmm5, xmm5
                                // xmm7 = LFdefl + RFdefl / Fnom [4,5]
                                divps xmm7, xmm3
                                // xmm7 = (LFdefl + RFdefl / Fnom) ^ 4 [4,5] 
                                // xmm1 = (LFdefl + RFdefl / Fnom) ^ 4 [0,1,2,3] 
                                mulps xmm1, xmm1
                                mov al, byte ptr extraLongLoad
                                mulps xmm7, xmm7
                                unpcklps xmm5, xmm5
                                mulps xmm1, xmm1
                                test al, al
                                mulps xmm7, xmm7
                                jz upd
                                mulps xmm1, xmm1
                                mulps xmm7, xmm7
                            upd:                       
                                // xmm1 = ((LFdefl - Fnom/2) - (RFdefl - Fnom/2)) * ((LFdefl + RFdefl) / Fnom) ^ 4) [0,1,2,3]
                                mulps xmm1, xmm6
                                movaps xmm2, xmmword ptr suspForceST[0]
                                // xmm7 = ((LFdefl - Fnom/2) - (RFdefl - Fnom/2)) * ((LFdefl + RFdefl) / Fnom) ^ 4) [4,5]
                                mulps xmm7, xmm4
                                // xmm1 *= loadFactor
                                mulps xmm1, xmm5
                                movlps xmm3, qword ptr suspForceST[16]
                                // xmm7 *= loadFactor
                                mulps xmm7, xmm5
                                // add to suspForceST
                                addps xmm2, xmm1
                                addps xmm3, xmm7
                                // write
                                movaps xmmword ptr suspForceST[0], xmm2
                                movlps qword ptr suspForceST[16], xmm3

                            end:

                            }
        
                        }
                        else {
                            suspForce = 
                                (
                                    (LFshockDeflST[STmaxIdx] - LFshockDeflLast) -
                                    (RFshockDeflST[STmaxIdx] - RFshockDeflLast)
                                ) * bumpsFactor * 0.25f;

                            if (FshockNom != 0.0f && loadFactor != 0.0f) {
                                float FnomAvg = FshockNom / 2.0f;
                                suspForce +=
                                    ((LFshockDeflST[STmaxIdx] - FnomAvg) - (RFshockDeflST[STmaxIdx] - FnomAvg)) *
                                        pow(
                                            (LFshockDeflST[STmaxIdx] + RFshockDeflST[STmaxIdx]) / FshockNom,
                                            extraLongLoad ? LONGLOAD_MAXPOWER : LONGLOAD_STDPOWER
                                        ) * loadFactor;
                            }
                        }

                        if (jetseat && jetseat->isEnabled()) {
                            float LFd = LFshockDeflST[STmaxIdx] - LFshockDeflLast;
                            float RFd = RFshockDeflST[STmaxIdx] - RFshockDeflLast;

                            if (LFd > 0.0025f || RFd > 0.0025f)
                                jetseat->fBumpEffect(LFd * 200.0f, RFd * 200.0f);
                        }
                        
                    }

                    RFshockDeflLast = RFshockDeflST[STmaxIdx];
                    LFshockDeflLast = LFshockDeflST[STmaxIdx];

                }
                else if (CFshockDeflST != nullptr && bumpsFactor != 0) {

                    if (CFshockDeflLast != -10000.0f) {
                    
                        if (ffbType != FFBTYPE_DIRECT_FILTER || use360)
                            __asm {
                                mov eax, CFshockDeflST
                                movups xmm0, xmmword ptr[eax]
                                // xmm3 = bumpsFactor
                                movss xmm3, bumpsFactor
                                // xmm2 = CFdefl[0,1,2,3]
                                movaps xmm2, xmm0
                                // xmm0 = CFdefl[-,1,2,3]
                                pslldq xmm0, 4
                                movss xmm4, CFshockDeflLast
                                unpcklps xmm3, xmm3
                                // xmm0 = CFdefl[CFlast,0,1,2]
                                movss xmm0, xmm4
                                // xmm2 = CFdefl[0] - CFlast, CFdefl[1] - CFdefl[0], ...
                                subps xmm2, xmm0
                                // xmm4 = CFdefl[3,4,5]
                                movups xmm4, xmmword ptr[eax + 12]
                                // xmm1 = CFdefl[4,5]
                                movlps xmm1, qword ptr[eax + 16]
                                unpcklps xmm3, xmm3
                                // xmm1 = CFdefl[4] - CFdefl[3], CFdefl[5] - CFdefl[4]
                                subps xmm1, xmm4
                                mulps xmm2, xmm3
                                mulps xmm1, xmm3
                                movaps xmmword ptr suspForceST[0], xmm2
                                movlps qword ptr suspForceST[16], xmm1
                            }
                        else 
                            suspForce = (CFshockDeflST[STmaxIdx] - CFshockDeflLast) * bumpsFactor * 0.25f;

                        if (jetseat && jetseat->isEnabled()) {
                            float CFd = CFshockDeflST[STmaxIdx] - CFshockDeflLast;
                            if  (CFd > 0.0025f)
                                jetseat->fBumpEffect(CFd * 200.0f, CFd * 200.0f);
                        }

                    }
                    
                    CFshockDeflLast = CFshockDeflST[STmaxIdx];

                }

                if (jetseat && jetseat->isEnabled() && LRshockDeflST != nullptr) {
                    float LRd = LRshockDeflST[STmaxIdx] - LRshockDeflLast;
                    float RRd = RRshockDeflST[STmaxIdx] - RRshockDeflLast;

                    if (LRd > 0.0025f || RRd > 0.0025f)
                        jetseat->rBumpEffect(LRd * 200.0f, RRd * 200.0f);

                    LRshockDeflLast = LRshockDeflST[STmaxIdx];
                    RRshockDeflLast = RRshockDeflST[STmaxIdx];
                }

                stopped = false;
                shockNomSet = true;

            }
            else {
                stopped = true;
                if (
                    LFshockDeflST != nullptr && RFshockDeflST != nullptr &&
                    abs(*speed) < 0.1f && *throttle == 0 && !shockNomSet
                )
                    FshockNom = LFshockDeflST[STmaxIdx] + RFshockDeflST[STmaxIdx];
            }

            for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {
                yawFilter[i] = yaw * firc6[i];
                yawForce[i] = yawFilter[0] + yawFilter[1] + yawFilter[2] + yawFilter[3] + yawFilter[4] + yawFilter[5];
            }

            if (*isOnTrack)
                fan->setSpeed(*speed);

            if (jetseat && jetseat->isEnabled() && *gear != lastGear) {
                jetseat->gearEffect();
                lastGear = *gear;
            }

            if (!*isOnTrack || settings.getFfbType() == FFBTYPE_DIRECT_FILTER)
                continue;

            halfSteerMax = *steerMax / 2.0f;

            // Bump stops
            if (abs(halfSteerMax) < 8.0f && abs(*steer) > halfSteerMax) {

                float factor, invFactor;

                if (*steer > 0) {
                    factor = (-(*steer - halfSteerMax)) / STOPS_MAXFORCE_RAD;
                    factor = maxf(factor, -1.0f);
                    invFactor = 1.0f + factor;
                }
                else {
                    factor = (-(*steer + halfSteerMax)) / STOPS_MAXFORCE_RAD;
                    factor = minf(factor, 1.0f);
                    invFactor = 1.0f - factor;
                }

                setFFB((int)(factor * DI_MAX + scaleTorque(*swTorque) * invFactor));
                continue;

            }

            // Telemetry FFB
            switch (settings.getFfbType()) {

                case FFBTYPE_360HZ: {

                    for (int i = 0; i < STmaxIdx; i++) {

                        setFFB(scaleTorque(swTorqueST[i] + suspForceST[i] + yawForce[i]));
                        sleepSpinUntil(&start, 2000, 2760 * (i + 1));

                    }
                    setFFB(scaleTorque(swTorqueST[STmaxIdx] + suspForceST[STmaxIdx] + yawForce[STmaxIdx]));

                }
                break;

                case FFBTYPE_360HZ_INTERP: {

                    float diff = swTorqueST[0] - lastTorque;
                    float sdiff = suspForceST[0] - lastSuspForce;
                    setFFB(scaleTorque(lastTorque + diff / 2.0f + lastSuspForce + sdiff / 2.0f + yawForce[0]));
                    sleepSpinUntil(&start, 0, 1380);

                    for (int i = 0; i < STmaxIdx << 1; i++) {

                        int idx = i >> 1;

                        if (i & 1) {
                            diff = swTorqueST[idx + 1] - swTorqueST[idx];
                            sdiff = suspForceST[idx + 1] - suspForceST[idx];
                            setFFB(scaleTorque(swTorqueST[idx] + diff / 2.0f + suspForceST[idx] + sdiff / 2.0f + yawForce[idx]));
                        }
                        else
                            setFFB(scaleTorque(swTorqueST[idx] + suspForceST[idx] + yawForce[idx]));

                        sleepSpinUntil(&start, 0, 1380 * (i + 2));

                    }

                    setFFB(scaleTorque(swTorqueST[STmaxIdx] + suspForceST[STmaxIdx] + yawForce[STmaxIdx]));
                    lastTorque = swTorqueST[STmaxIdx];
                    lastSuspForce = suspForceST[STmaxIdx];

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
            resetForces();
            setOnTrackStatus(false);
            setConnectedStatus(false);
            if (settings.getUseCarSpecific() && car[0] != 0) 
                settings.writeSettingsForCar(car);
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

HWND combo(HWND parent, wchar_t *name, int x, int y) {

    CreateWindowW(
        L"STATIC", name,
        WS_CHILD | WS_VISIBLE,
        x, y, 300, 20, parent, NULL, hInst, NULL
    );
    return 
        CreateWindow(
            L"COMBOBOX", nullptr,
            CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_OVERLAPPED | WS_TABSTOP,
            x + 12, y + 26, 300, 240, parent, nullptr, hInst, nullptr
        );

}

sWins_t *slider(HWND parent, wchar_t *name, int x, int y, wchar_t *start, wchar_t *end) {

    sWins_t *wins = (sWins_t *)malloc(sizeof(sWins_t));

    wins->value = CreateWindowW(
        L"STATIC", name,
        WS_CHILD | WS_VISIBLE,
        x, y, 300, 20, parent, NULL, hInst, NULL
    );

    wins->trackbar = CreateWindowExW(
        0, TRACKBAR_CLASS, name,
        WS_CHILD | WS_VISIBLE | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND,
        x + 40, y + 26, 240, 30,
        parent, NULL, hInst, NULL
    );

    HWND buddyLeft = CreateWindowEx(
        0, L"STATIC", start,
        SS_LEFT | WS_CHILD | WS_VISIBLE,
        0, 0, 40, 20, parent, NULL, hInst, NULL
    );
    SendMessage(wins->trackbar, TBM_SETBUDDY, (WPARAM)TRUE, (LPARAM)buddyLeft);

    HWND buddyRight = CreateWindowEx(
        0, L"STATIC", end,
        SS_RIGHT | WS_CHILD | WS_VISIBLE,
        0, 0, 52, 20, parent, NULL, hInst, NULL
    );
    SendMessage(wins->trackbar, TBM_SETBUDDY, (WPARAM)FALSE, (LPARAM)buddyRight);

    return wins;

}

HWND checkbox(HWND parent, wchar_t *name, int x, int y) {

    return 
        CreateWindowEx(
            0, L"BUTTON", name,
            BS_CHECKBOX | BS_MULTILINE | WS_CHILD | WS_VISIBLE,
            x, y, 360, 58, parent, nullptr, hInst, nullptr
        );

}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {

    hInst = hInstance;

    mainWnd = CreateWindowW(
        szWindowClass, szTitle,
        WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 432, 980,
        NULL, NULL, hInst, NULL
    );

    if (!mainWnd)
        return FALSE;

    settings.setDevWnd(combo(mainWnd, L"FFB device:", 44, 20));
    settings.setFfbWnd(combo(mainWnd, L"FFB type:", 44, 80));
    settings.setMinWnd(slider(mainWnd, L"Min force:", 44, 144, L"0", L"20"));
    settings.setMaxWnd(slider(mainWnd, L"Max force:", 44, 216, L"5 Nm", L"65 Nm"));
    settings.setBumpsWnd(slider(mainWnd, L"Suspension bumps:", 44, 288, L"0", L"100"));
    settings.setLoadWnd(slider(mainWnd, L"Suspension load:", 44, 360, L"0", L"100"));
    settings.setYawWnd(slider(mainWnd, L"SoP effect:", 44, 428, L"0", L"100"));
    settings.setExtraLongWnd(
        checkbox(mainWnd, L" Increased longitudinal weight transfer effect?", 30, 496)
    );
    settings.setUse360Wnd(
        checkbox(
            mainWnd, 
            L" Use 360 Hz telemetry for suspension effects\r\n in direct modes?",
            30, 542
        )
    );
    settings.setCarSpecificWnd(
        checkbox(mainWnd, L"Use car specific settings?", 30, 586)
    );

    int statusParts[] = { 128, 212, 432 };

    statusWnd = CreateWindowEx(
        0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, mainWnd, NULL, hInst, NULL
    );
    SendMessage(statusWnd, SB_SETPARTS, 3, LPARAM(statusParts));
    
    textWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_VSCROLL | WS_CHILD | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
        16, 644, 384, 240,
        mainWnd, NULL, hInst, NULL
    );
    SendMessage(textWnd, EM_SETLIMITTEXT, WPARAM(256000), 0);

    ShowWindow(mainWnd, nCmdShow);
    UpdateWindow(mainWnd);

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
                case ID_SETTINGS_JETSEAT:
                    if (jetseat)
                        jetseat->createWindow(hInst);
                    break;
                case ID_SETTINGS_FAN:
                    fan->createWindow(hInst);
                    break;
                default:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        if ((HWND)lParam == settings.getDevWnd())
                            settings.setFfbDevice(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));                            
                        else if ((HWND)lParam == settings.getFfbWnd())
                            settings.setFfbType(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
                    }
                    else if (HIWORD(wParam) == BN_CLICKED) {
                        bool oldValue = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        if ((HWND)lParam == settings.getUse360Wnd())
                            settings.setUse360ForDirect(!oldValue);
                        else if ((HWND)lParam == settings.getExtraLongWnd())
                            settings.setExtraLongLoad(!oldValue);
                        else if ((HWND)lParam == settings.getCarSpecificWnd())
                            settings.setUseCarSpecific(!oldValue, car);
                    }
                    return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

        case WM_HSCROLL: {
            if ((HWND)lParam == settings.getMaxWnd()->trackbar)
                settings.setMaxForce(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
            else if ((HWND)lParam == settings.getMinWnd()->trackbar)
                settings.setMinForce(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
            else if ((HWND)lParam == settings.getBumpsWnd()->trackbar)
                settings.setBumpsFactor(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
            else if ((HWND)lParam == settings.getLoadWnd()->trackbar)
                settings.setLoadFactor(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
            else if ((HWND)lParam == settings.getYawWnd()->trackbar)
                settings.setYawFactor(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
        }
        break;

        case WM_CTLCOLORSTATIC: {
            SetBkColor((HDC)wParam, RGB(0xff, 0xff, 0xff));
            return (LRESULT)CreateSolidBrush(RGB(0xff, 0xff, 0xff));
        }
        break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
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
            if (settings.getUseCarSpecific() && car[0] != 0)
                settings.writeSettingsForCar(car);
            else
                settings.writeRegSettings();
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

void text(wchar_t *fmt, char *charstr) {

    int len = strlen(charstr) + 1;
    wchar_t *wstr = new wchar_t[len];
    mbstowcs_s(nullptr, wstr, len, charstr, len);
    text(fmt, wstr);
    delete[] wstr;

}

void setCarStatus(char *carStr) {

    if (!carStr || carStr[0] == 0) {
        SendMessage(statusWnd, SB_SETTEXT, STATUS_CAR_PART, LPARAM(L"Car: generic"));
        return;
    }

    int len = strlen(carStr) + 1;
    wchar_t *wstr = new wchar_t[len + 5];
    lstrcpy(wstr, L"Car: ");
    mbstowcs_s(nullptr, wstr + 5, len, carStr, len);
    SendMessage(statusWnd, SB_SETTEXT, STATUS_CAR_PART, LPARAM(wstr));
    delete[] wstr;

}

void setConnectedStatus(bool connected) {

    SendMessage(
        statusWnd, SB_SETTEXT, STATUS_CONNECTED_PART,
        LPARAM(connected ? L"iRacing connected" : L"iRacing disconnected")
    );

}

void setOnTrackStatus(bool onTrack) {

    SendMessage(
        statusWnd, SB_SETTEXT, STATUS_ONTRACK_PART,
        LPARAM(onTrack ? L"On track" : L"Not on track")
    );

}

BOOL CALLBACK EnumFFDevicesCallback(LPCDIDEVICEINSTANCE diDevInst, VOID *wnd) {

    UNREFERENCED_PARAMETER(wnd);

    if (lstrcmp(diDevInst->tszProductName, L"vJoy Device") == 0)
        return true;

    settings.addFfbDevice(diDevInst->guidInstance, diDevInst->tszProductName);
    
    return true;

}

BOOL CALLBACK EnumObjectCallback(const LPCDIDEVICEOBJECTINSTANCE inst, VOID *dw) {

    UNREFERENCED_PARAMETER(inst);

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
        DI8DEVCLASS_GAMECTRL, EnumFFDevicesCallback, settings.getDevWnd(),
        DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK
    );

}

void initDirectInput() {

    DIDEVICEINSTANCE di;
    HRESULT hr;

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

    if (FAILED(pDI->CreateDevice(settings.getFfbDevice(), &ffdevice, nullptr))) {
        text(L"Failed to create DI device");
        text(L"Is it connected and powered on?");
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
    text(L"DI device has %d buttons", numButtons);

    if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID *)&numPov, DIDFT_POV))) {
        text(L"Failed to enumerate DI device povs");
        return;
    }
    text(L"DI device has %d POV", numPov);

    if (FAILED(ffdevice->SetEventNotification(wheelEvent))) {
        text(L"Failed to set event notification on DI device");
        return;
    }

    if (FAILED(ffdevice->Acquire())) {
        text(L"Failed to acquire DI device");
        return;
    }

    if (FAILED(ffdevice->CreateEffect(GUID_Sine, &dieff, &effect, nullptr))) {
        text(L"Failed to create sine periodic effect");
        return;
    }

    if (!effect) {
        text(L"Effect creation failed");
        return;
    }

    hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
    if (hr == DIERR_NOTINITIALIZED || hr == DIERR_INPUTLOST || hr == DIERR_INCOMPLETEEFFECT || hr == DIERR_INVALIDPARAM)
        text(L"Error setting parameters of DIEFFECT: %d", hr);
        
}

void reacquireDIDevice() {

    if (ffdevice == nullptr)
        return;

    HRESULT hr;

    ffdevice->Unacquire();
    ffdevice->Acquire();

    if (effect) {
        hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
        if (hr == DIERR_NOTINITIALIZED || hr == DIERR_INPUTLOST || hr == DIERR_INCOMPLETEEFFECT || hr == DIERR_INVALIDPARAM)
            text(L"Error setting parameters of DIEFFECT: %d", hr);
    }

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

    return (int)(t * settings.getScaleFactor());

}

inline void setFFB(int mag) {

    if (!effect)
        return;

    if (mag < -DI_MAX) {
        mag = -DI_MAX;
        clippedSamples++;
    }
    else if (mag > DI_MAX) {
        mag = DI_MAX;
        clippedSamples++;
    }

    samples++;
    int minForce = settings.getMinForce();

    if (stopped)
        mag /= 4;
    else if (minForce) {
        if (mag > 0 && mag < minForce)
            mag = minForce;
        else if (mag < 0 && mag > -minForce)
            mag = -minForce;
    }

    pforce.lOffset = mag;
    effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);

}

bool initVJD() {

    WORD verDll, verDrv;
    int maxVjDev;
    VjdStat vjdStatus = VJD_STAT_UNKN;

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

    memset(&ffbPacket, 0 ,sizeof(ffbPacket));

    if (vjdStatus == VJD_STAT_OWN) {
        RelinquishVJD(vjDev);
        vjdStatus = GetVJDStatus(vjDev);
    }
    if (vjdStatus == VJD_STAT_FREE) {
        if (!AcquireVJD(vjDev, ffbEvent, &ffbPacket)) {
            text(L"Failed to acquire vJoy device %d!", vjDev);
            return false;
        }
    }
    else {
        text(L"ERROR: vJoy device %d status is %d", vjDev, vjdStatus);
        return false;
    }

    vjButtons = GetVJDButtonNumber(vjDev);
    vjPov = GetVJDContPovNumber(vjDev);
    vjPov += GetVJDDiscPovNumber(vjDev);

    text(L"Acquired vJoy device %d", vjDev);
    ResetVJD(vjDev);

    return true;

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
    if (fan)
        fan->setSpeed(0);

    RelinquishVJD(vjDev);

    irsdk_shutdown();

}