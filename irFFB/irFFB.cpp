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
#include "hidguardian.h"
#include "public.h"
#include "yaml_parser.h"
#include "vjoyinterface.h"

#define MAX_LOADSTRING 100

#define STATUS_CONNECTED_PART 0
#define STATUS_ONTRACK_PART 1
#define STATUS_CAR_PART 2

extern HANDLE hDataValidEvent;

// Globals
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
NOTIFYICONDATA niData;

HANDLE debugHnd = INVALID_HANDLE_VALUE;
wchar_t debugLastMsg[512];
LONG debugRepeat = 0;

LPDIRECTINPUT8 pDI = nullptr;
LPDIRECTINPUTDEVICE8 ffdevice = nullptr;
LPDIRECTINPUTEFFECT effect = nullptr;

DIJOYSTATE joyState;
DWORD axes[1] = { DIJOFS_X };
LONG  dir[1]  = { 0 };
DIPERIODIC pforce;
DIEFFECT   dieff;

LogiLedData logiLedData;
DIEFFESCAPE logiEscape;

Settings settings;
JetSeat *jetseat;
Fan *fan;
HidGuardian *hidGuardian;

float firc6[] = { 
    0.0777116f, 0.2607993f, 0.3240389f, 0.2332305f, 0.0903209f, 0.0135009f 
};
float firc12[] = { 
    0.0483102f, 0.0886167f, 0.1247764f, 0.1501189f, 0.1582776f, 0.1456553f, 
    0.1193849f, 0.0846268f, 0.0456667f, 0.0148898f, 0.0032250f, 0.0164516f 
};

char car[MAX_CAR_NAME];

int force = 0, maxSample = 0;
volatile float suspForce = 0.0f, damperForce = 0.0f; 
volatile float yawForce[DIRECT_INTERP_SAMPLES];
__declspec(align(16)) volatile float suspForceST[DIRECT_INTERP_SAMPLES];
bool onTrack = false, stopped = true, deviceChangePending = false, logiWheel = false;

bool elevated = false;

volatile bool reacquireNeeded = false;

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
    LONG lastX;
    LARGE_INTEGER lastTime, time, elapsed;
    float v, vel[6] = { 0 };
    int velIdx = 0;

    lastTime.QuadPart = 0;

    while (true) {

        WaitForSingleObject(wheelEvent, INFINITE);

        if (!ffdevice || reacquireNeeded)
            continue;

        res = ffdevice->GetDeviceState(sizeof(joyState), &joyState);
        if (res != DI_OK) {
            debug(L"GetDeviceState returned: 0x%x, requesting reacquire", res);
            reacquireNeeded = true;
            continue;
        }

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

        if (settings.getDampingFactor() == 0.0f)
            continue;
        
        QueryPerformanceCounter(&time);

        if (lastTime.QuadPart != 0) {
            elapsed.QuadPart = (time.QuadPart - lastTime.QuadPart) * 1000000;
            elapsed.QuadPart /= freq.QuadPart;
            vel[velIdx++] = -(joyState.lX - lastX) / (float)elapsed.QuadPart;
            v = (vel[0] + vel[1] + vel[2] + vel[3] + vel[4] + vel[5]) / 6;
            v *= settings.getDampingFactor() * 10.0f;
            damperForce = v;
            if (velIdx > 5)
                velIdx = 0;
        }
        
        lastTime.QuadPart = time.QuadPart;
        lastX = joyState.lX;


    }

}

// Write FFB samples for the direct modes
DWORD WINAPI directFFBThread(LPVOID lParam) {

    UNREFERENCED_PARAMETER(lParam);
    int16_t mag;

    float s;
    int r;
    __declspec(align(16)) float prod[12];
    float lastSuspForce = 0, lastYawForce = 0;
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
                haddps xmm0, xmm0
                haddps xmm0, xmm0
                cvttss2si eax, xmm0
                mov dword ptr r, eax
            }

            if (use360)
                r += scaleTorque(lastSuspForce + (suspForceST[0] - lastSuspForce) / 2.0f);

            r += scaleTorque(lastYawForce + (yawForce[0] - lastYawForce) / 2.0f);

            setFFB(r);

            for (int i = 1; i < DIRECT_INTERP_SAMPLES * 2 - 1; i++) {

                prod[i] = s * firc12[i];

                _asm {
                    movaps xmm0, xmmword ptr prod
                    movaps xmm1, xmmword ptr prod + 16
                    movaps xmm2, xmmword ptr prod + 32
                    addps xmm0, xmm1
                    addps xmm0, xmm2
                    haddps xmm0, xmm0
                    haddps xmm0, xmm0
                    cvttss2si eax, xmm0
                    mov dword ptr r, eax
                }

                int idx = (i - 1) >> 1;
                bool odd = i & 1;

                if (use360)
                    r +=
                        scaleTorque(
                            odd ?
                                suspForceST[idx] :
                                suspForceST[idx]  + (suspForceST[idx + 1] - suspForceST[idx]) / 2.0f
                        );

                r += 
                    scaleTorque(
                        odd ?
                            yawForce[idx] :
                            yawForce[idx] + (yawForce[idx + 1] - yawForce[idx]) / 2.0f
                    );

                sleepSpinUntil(&start, 0, 1380 * i);
                setFFB(r);

            }

            prod[DIRECT_INTERP_SAMPLES * 2 - 1] = s * firc12[DIRECT_INTERP_SAMPLES * 2 - 1];
            _asm {
                movaps xmm0, xmmword ptr prod
                movaps xmm1, xmmword ptr prod + 16
                movaps xmm2, xmmword ptr prod + 32
                addps xmm0, xmm1
                addps xmm0, xmm2
                haddps xmm0, xmm0
                haddps xmm0, xmm0
                cvttss2si eax, xmm0
                mov dword ptr r, eax
            }

            if (use360)
                r += scaleTorque(suspForceST[DIRECT_INTERP_SAMPLES - 1]);

            r += scaleTorque(yawForce[DIRECT_INTERP_SAMPLES - 1]);

            sleepSpinUntil(&start, 0, 1380 * (DIRECT_INTERP_SAMPLES * 2 - 1));
            setFFB(r);

            lastSuspForce = suspForceST[DIRECT_INTERP_SAMPLES - 1];
            lastYawForce = yawForce[DIRECT_INTERP_SAMPLES - 1];

            continue;

        }
            
        prod[0] = s * firc6[0];
        r = (int)(prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5]) +
                scaleTorque(yawForce[0]);

        if (use360)
            r += scaleTorque(suspForceST[0]);

        setFFB(r);

        for (int i = 1; i < DIRECT_INTERP_SAMPLES; i++) {

            prod[i] = s * firc6[i];
            r = (int)(prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5]) +
                    scaleTorque(yawForce[i]);

            if (use360)
                r += scaleTorque(suspForceST[i]);

            sleepSpinUntil(&start, 2000, 2760 * i);
            setFFB(r);

        }

    }

    return 0;

}

void resetForces() {
    debug(L"Resetting forces");
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
            return 8000.0f;
        
        memcpy(buf, ptr, len);
        buf[len] = 0;
        return strtof(buf, NULL);
    }
    
    return 8000.0f;

}

void clippingReport() {

    float clippedPerCent = samples > 0 ? clippedSamples * 100 / samples : 0.0f;
    text(L"Max sample value: %d", maxSample);
    text(L"%.02f%% of samples were clipped", clippedPerCent);
    if (clippedPerCent > 5.0f)
        text(L"Consider increasing max force to reduce clipping");
    samples = clippedSamples = maxSample = 0;

}

void logiRpmLed(float *rpm, float redline) {
    
    logiLedData.rpmData.rpm = *rpm / (redline * 0.90f);
    logiLedData.rpmData.rpmFirstLed = 0.65f;
    logiLedData.rpmData.rpmRedLine = 1.0f;

    ffdevice->Escape(&logiEscape);

}

void deviceChange() {
    debug(L"Device change notification");
    if (!onTrack) {
        debug(L"Not on track, processing device change");
        deviceChangePending = false;
        enumDirectInput();
        if (!settings.isFfbDevicePresent())
            releaseDirectInput();
    }
    else {
        debug(L"Deferring device change processing whilst on track");
        deviceChangePending = true;
    }
}

DWORD getDeviceVidPid(LPDIRECTINPUTDEVICE8 dev) {

    DIPROPDWORD dipdw;
    dipdw.diph.dwSize = sizeof(DIPROPDWORD);
    dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dipdw.diph.dwObj = 0;
    dipdw.diph.dwHow = DIPH_DEVICE;

    if (dev == nullptr)
        return 0;

    if (!SUCCEEDED(dev->GetProperty(DIPROP_VIDPID, &dipdw.diph)))
        return 0;

    return dipdw.dwData;

}

void minimise() {
    debug(L"Minimising window");
    Shell_NotifyIcon(NIM_ADD, &niData);
    ShowWindow(mainWnd, SW_HIDE);
}

void restore() {
    debug(L"Restoring window");
    Shell_NotifyIcon(NIM_DELETE, &niData);
    ShowWindow(mainWnd, SW_SHOW);
}

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow
) {

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    if (StrStrW(lpCmdLine, L"service")) {

        SERVICE_TABLE_ENTRYW SvcDispatchTable[] = {
            { SVCNAME, (LPSERVICE_MAIN_FUNCTION)HidGuardian::SvcMain },
            { NULL, NULL }
        };

        if (!StartServiceCtrlDispatcherW(SvcDispatchTable)) {
            HidGuardian::svcReportError(L"Failed to start service ctrl dispatcher");
            exit(1);
        }

        exit(0);

    }

    INITCOMMONCONTROLSEX ccEx;

    HANDLE handles[1];
    char *data = nullptr;
    bool irConnected = false;
    MSG msg;

    float *swTorque = nullptr, *swTorqueST = nullptr, *steer = nullptr, *steerMax = nullptr;
    float *speed = nullptr, *throttle = nullptr, *rpm = nullptr;
    float *LFshockDeflST = nullptr, *RFshockDeflST = nullptr, *CFshockDeflST = nullptr;
    float *LRshockDeflST = nullptr, *RRshockDeflST = nullptr;
    float *vX = nullptr, *vY = nullptr;
    float LFshockDeflLast = -10000, RFshockDeflLast = -10000, CFshockDeflLast = -10000;
    float LRshockDeflLast = -10000, RRshockDeflLast = -10000;
    bool *isOnTrack = nullptr, *isInGarage = nullptr, *isOnTrackCar = nullptr;
    int *trackSurface = nullptr, *gear = nullptr;

    bool inGarage = false, onTrackCar = false;
    int numHandles = 0, dataLen = 0, lastGear = 0;
    int STnumSamples = 0, STmaxIdx = 0, lastTrackSurface = -1;
    float halfSteerMax = 0, lastTorque = 0, lastSuspForce = 0, redline;
    float yaw = 0.0f, yawFilter[DIRECT_INTERP_SAMPLES];

    ccEx.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    ccEx.dwSize = sizeof(ccEx);
    InitCommonControlsEx(&ccEx);

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

    ZeroMemory(&logiLedData, sizeof(logiLedData));
    logiLedData.size = sizeof(logiLedData);
    logiLedData.version = 1;
    ZeroMemory(&logiEscape, sizeof(logiEscape));
    logiEscape.dwSize = sizeof(DIEFFESCAPE);
    logiEscape.dwCommand = 0;
    logiEscape.lpvInBuffer = &logiLedData;
    logiEscape.cbInBuffer = sizeof(logiLedData);

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    hidGuardian = HidGuardian::init(GetCurrentProcessId());
    if (StrStrW(lpCmdLine, L"instHG"))
        hidGuardian->install();

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
    settings.readGenericSettings();
    settings.readRegSettings(car);

    if (settings.getStartMinimised())
        minimise();
    else
        restore();

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

    debug(L"Init complete, entering mainloop");

    while (TRUE) {

        DWORD res;
        const irsdk_header *hdr = NULL;

        if (
            irsdk_startup() && (hdr = irsdk_getHeader()) &&
            hdr->status & irsdk_stConnected && hdr->bufLen != dataLen && hdr->bufLen != 0
        ) {

            debug(L"New iRacing session");

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

            debug(L"Redline is %f", redline);
            debug(L"Informing iRacing that maxForce is %d", settings.getMaxForce());
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
            isOnTrackCar = boolvarptr(data, "IsOnTrackCar");
            isInGarage = boolvarptr(data, "IsInGarage");

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

            lastTorque = 0.0f;
            onTrack = false;
            resetForces();
            irConnected = true;
            timeBeginPeriod(1);

        }

        // Try to make sure we've retained our acquisition
        if (ffdevice && reacquireNeeded) {
            debug(L"Reacquiring DI device");
            reacquireDIDevice();
            reacquireNeeded = false;
        }

        res = MsgWaitForMultipleObjects(numHandles, handles, FALSE, 1000, QS_ALLINPUT);

        QueryPerformanceCounter(&start);

        if (numHandles > 0 && res == numHandles - 1 && irsdk_getNewData(data)) {

            if (onTrack && !*isOnTrack) {
                debug(L"No longer on track");
                onTrack = false;
                setOnTrackStatus(onTrack);
                lastTorque = lastSuspForce = 0.0f;
                resetForces();
                fan->setManualSpeed();
                clippingReport();
            }

            else if (!onTrack && *isOnTrack) {
                debug(L"Now on track");
                onTrack = true;
                setOnTrackStatus(onTrack);
                RFshockDeflLast = LFshockDeflLast = 
                    LRshockDeflLast = RRshockDeflLast = 
                        CFshockDeflLast = -10000.0f;
                clippedSamples = samples = lastGear = 0;
                memset(yawFilter, 0, DIRECT_INTERP_SAMPLES * sizeof(float));
            }

            if (*trackSurface != lastTrackSurface) {
                debug(L"Track surface is now: %d", *trackSurface);
                lastTrackSurface = *trackSurface;
            }

            if (inGarage != *isInGarage) {
                debug(L"IsInGarage is now %d", *isInGarage);
                inGarage = *isInGarage;
            }

            if (onTrackCar != *isOnTrackCar) {
                debug(L"IsOnTrackCar is now %d", *isOnTrackCar);
                onTrackCar = *isOnTrackCar;
            }

            if (jetseat && jetseat->isEnabled()) {
                if (*isOnTrack && *rpm > 0.0f) {
                    jetseat->startEngineEffect();
                    jetseat->updateEngineEffect(*rpm * 100.0f / redline);
                }
                else                
                    jetseat->stopEngineEffect();
            }

            if (ffdevice && logiWheel)
                logiRpmLed(rpm, redline);

            yaw = 0.0f;

            if (*speed > 2.0f) {
            
                float bumpsFactor = settings.getBumpsFactor();
                float sopFactor = settings.getSopFactor();
                float sopOffset = settings.getSopOffset();

                bool use360  = settings.getUse360ForDirect();
                int ffbType = settings.getFfbType();

                if (*speed > 5.0f) {

                    float halfMaxForce = (float)(settings.getMaxForce() >> 1);
                    float r = *vY / *vX;
                    float sa, asa, ar = abs(r);

                    if (*vX < 0.0f)
                        r = -r;

                    if (ar > 1.0f) {
                        sa = csignf(0.785f, r);
                        asa = 0.785f;
                        yaw = minf(maxf(sa * sopFactor, -halfMaxForce), halfMaxForce);
                    }
                    else {
                        sa = 0.78539816339745f * r + 0.273f * r * (1.0f - ar);
                        asa = abs(sa);
                        if (asa > sopOffset) {
                            sa -= csignf(sopOffset, sa);
                            yaw =
                                minf(
                                    maxf(
                                        sa * (2.0f - asa) * sopFactor,
                                        -halfMaxForce
                                    ),
                                    halfMaxForce
                                );
                        }
                    }
                    
                    if (jetseat && jetseat->isEnabled() && asa > sopOffset)
                        jetseat->yawEffect(sa);

                }

                if (
                    LFshockDeflST != nullptr && RFshockDeflST != nullptr && bumpsFactor != 0.0f
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
                            }
        
                        }
                        else {
                            suspForce = 
                                (
                                    (LFshockDeflST[STmaxIdx] - LFshockDeflLast) -
                                    (RFshockDeflST[STmaxIdx] - RFshockDeflLast)
                                ) * bumpsFactor * 0.25f;
                        }

                        if (jetseat && jetseat->isEnabled()) {
                            float LFd = LFshockDeflST[STmaxIdx] - LFshockDeflLast;
                            float RFd = RFshockDeflST[STmaxIdx] - RFshockDeflLast;

                            if (LFd > 0.0025f || RFd > 0.0025f)
                                jetseat->fBumpEffect(LFd * 220.0f, RFd * 220.0f);
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
                                jetseat->fBumpEffect(CFd * 220.0f, CFd * 220.0f);
                        }

                    }
                    
                    CFshockDeflLast = CFshockDeflST[STmaxIdx];

                }

                if (jetseat && jetseat->isEnabled() && LRshockDeflST != nullptr) {
                    float LRd = LRshockDeflST[STmaxIdx] - LRshockDeflLast;
                    float RRd = RRshockDeflST[STmaxIdx] - RRshockDeflLast;

                    if (LRd > 0.0025f || RRd > 0.0025f)
                        jetseat->rBumpEffect(LRd * 220.0f, RRd * 220.0f);

                    LRshockDeflLast = LRshockDeflST[STmaxIdx];
                    RRshockDeflLast = RRshockDeflST[STmaxIdx];
                }

                stopped = false;

            }
            else
                stopped = true;

            if (*isOnTrack)
                fan->setSpeed(*speed);

            if (jetseat && jetseat->isEnabled() && *gear != lastGear) {
                jetseat->gearEffect();
                lastGear = *gear;
            }

            for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {

                yawFilter[i] = yaw * firc6[i];

                yawForce[i] =
                    yawFilter[0] + yawFilter[1] + yawFilter[2] +
                        yawFilter[3] + yawFilter[4] + yawFilter[5];

            }

            if (
                !*isOnTrack ||
                settings.getFfbType() == FFBTYPE_DIRECT_FILTER ||
                settings.getFfbType() == FFBTYPE_DIRECT_FILTER_720
            )
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
                    setFFB(
                        scaleTorque(
                            swTorqueST[STmaxIdx] + suspForceST[STmaxIdx] + yawForce[STmaxIdx]
                        )
                    );

                }
                break;

                case FFBTYPE_360HZ_INTERP: {

                    float diff = (swTorqueST[0] - lastTorque) / 2.0f;
                    float sdiff = (suspForceST[0] - lastSuspForce) / 2.0f;
                    int force, iMax = STmaxIdx << 1;

                    setFFB(
                        scaleTorque(
                            lastTorque + diff + lastSuspForce + sdiff + yawForce[0]
                        )
                    );

                    for (int i = 0; i < iMax; i++) {

                        int idx = i >> 1;

                        if (i & 1) {
                            diff = (swTorqueST[idx + 1] - swTorqueST[idx]) / 2.0f;
                            sdiff = (suspForceST[idx + 1] - suspForceST[idx]) / 2.0f;
                            force =
                                scaleTorque(
                                    swTorqueST[idx] + diff + suspForceST[idx] +
                                        sdiff + yawForce[idx]
                                );
                        }
                        else
                            force =
                                scaleTorque(
                                    swTorqueST[idx] + suspForceST[idx] + yawForce[idx]
                                );

                        sleepSpinUntil(&start, 0, 1380 * (i + 1));
                        setFFB(force);

                    }

                    sleepSpinUntil(&start, 0, 1380 * (iMax + 1));
                    setFFB(
                        scaleTorque(
                            swTorqueST[STmaxIdx] + suspForceST[STmaxIdx] + yawForce[STmaxIdx]
                        )
                    );
                    lastTorque = swTorqueST[STmaxIdx];
                    lastSuspForce = suspForceST[STmaxIdx];

                }
                break;

            }


        }

        // Did we lose iRacing?
        if (numHandles > 0 && !(hdr->status & irsdk_stConnected)) {
            debug(L"Disconnected from iRacing");
            numHandles = 0;
            dataLen = 0;
            if (data != NULL) {
                free(data);
                data = NULL;
            }
            resetForces();
            onTrack = false;
            setOnTrackStatus(onTrack);
            setConnectedStatus(false);
            fan->setManualSpeed();
            timeEndPeriod(1);
            if (settings.getUseCarSpecific() && car[0] != 0) 
                settings.writeSettingsForCar(car);
        }

        // Window messages
        if (res == numHandles) {

            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT)
                    DestroyWindow(mainWnd);
                if (!IsDialogMessage(mainWnd, &msg)) {
                    if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
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

LRESULT CALLBACK EditWndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subId, DWORD_PTR rData) {

    if (msg == WM_CHAR) {

        wchar_t buf[8];

        if (subId == EDIT_FLOAT) {

            if (GetWindowTextW(wnd, buf, 8) && StrChrIW(buf, L'.') && wParam == '.')
                return 0;

            if (
                !(
                    (wParam >= L'0' && wParam <= L'9') ||
                    wParam == L'.' ||
                    wParam == VK_RETURN ||
                    wParam == VK_DELETE ||
                    wParam == VK_BACK
                )
            )
                return 0;

            LRESULT ret = DefSubclassProc(wnd, msg, wParam, lParam);

            wchar_t *end;
            float val = 0.0f;

            GetWindowText(wnd, buf, 8);
            val = wcstof(buf, &end);
            if (end - buf == wcslen(buf))
                SendMessage(GetParent(wnd), WM_EDIT_VALUE, reinterpret_cast<WPARAM &>(val), (LPARAM)wnd);

            return ret;

        }
        else {

            if (
                !(
                    (wParam >= L'0' && wParam <= L'9') ||
                    wParam == VK_RETURN ||
                    wParam == VK_DELETE ||
                    wParam == VK_BACK
                )
            )
                return 0;

            LRESULT ret = DefSubclassProc(wnd, msg, wParam, lParam);
            GetWindowText(wnd, buf, 8);
            int val = _wtoi(buf);
            SendMessage(GetParent(wnd), WM_EDIT_VALUE, (WPARAM)val, (LPARAM)wnd);
            return ret;

        }

    }

    return DefSubclassProc(wnd, msg, wParam, lParam);

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

sWins_t *slider(HWND parent, wchar_t *name, int x, int y, wchar_t *start, wchar_t *end, bool floatData) {

    sWins_t *wins = (sWins_t *)malloc(sizeof(sWins_t));

    wins->label = CreateWindowW(
        L"STATIC", name,
        WS_CHILD | WS_VISIBLE,
        x, y, 300, 20, parent, NULL, hInst, NULL
    );

    wins->value = CreateWindowW(
        L"EDIT", L"", 
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_CENTER,
        x + 210, y, 50, 20, parent, NULL, hInst, NULL
    );

    SetWindowSubclass(wins->value, EditWndProc, floatData ? 1 : 0, 0);

    SendMessage(wins->value, EM_SETLIMITTEXT, 5, 0);

    wins->trackbar = CreateWindowExW(
        0, TRACKBAR_CLASS, name,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND,
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
            BS_CHECKBOX | BS_MULTILINE | WS_CHILD | WS_TABSTOP | WS_VISIBLE,
            x, y, 360, 58, parent, nullptr, hInst, nullptr
        );

}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {

    DEV_BROADCAST_DEVICEINTERFACE devFilter;

    hInst = hInstance;

    mainWnd = CreateWindowW(
        szWindowClass, szTitle,
        WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 864, 720,
        NULL, NULL, hInst, NULL
    );

    if (!mainWnd)
        return FALSE;
    
    memset(&niData, 0, sizeof(niData));
    niData.uVersion = NOTIFYICON_VERSION;
    niData.cbSize = NOTIFYICONDATA_V1_SIZE;
    niData.hWnd = mainWnd;
    niData.uID = 1;
    niData.uFlags = NIF_ICON | NIF_MESSAGE;
    niData.uCallbackMessage = WM_TRAY_ICON;
    niData.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL));

    settings.setDevWnd(combo(mainWnd, L"FFB device:", 44, 20));
    settings.setFfbWnd(combo(mainWnd, L"FFB type:", 44, 80));
    settings.setMinWnd(slider(mainWnd, L"Min force:", 44, 154, L"0", L"20", false));
    settings.setMaxWnd(slider(mainWnd, L"Max force:", 44, 226, L"5 Nm", L"65 Nm", false));
    settings.setBumpsWnd(slider(mainWnd, L"Suspension bumps:", 464, 40, L"0", L"100", true));
    settings.setDampingWnd(slider(mainWnd, L"Damping:", 464, 100, L"0", L"100", true));
    settings.setSopWnd(slider(mainWnd, L"SoP effect:", 464, 220, L"0", L"100", true));
    settings.setSopOffsetWnd(slider(mainWnd, L"SoP deadzone:", 464, 280, L"0", L"100", true));
    settings.setUse360Wnd(
        checkbox(
            mainWnd, 
            L" Use 360 Hz telemetry for suspension effects\r\n in direct modes?",
            460, 340
        )
    );
    settings.setCarSpecificWnd(
        checkbox(mainWnd, L" Use car specific settings?", 460, 400)
    );
    settings.setReduceWhenParkedWnd(
        checkbox(mainWnd, L" Reduce force when parked?", 460, 440)
    );
    settings.setRunOnStartupWnd(
        checkbox(mainWnd, L" Run on startup?", 460, 480)
    );
    settings.setStartMinimisedWnd(
        checkbox(mainWnd, L" Start minimised?", 460, 520)
    );
    settings.setDebugWnd(
        checkbox(mainWnd, L"Debug logging?", 460, 560)
    );

    int statusParts[] = { 256, 424, 864 };

    statusWnd = CreateWindowEx(
        0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, mainWnd, NULL, hInst, NULL
    );
    SendMessage(statusWnd, SB_SETPARTS, 3, LPARAM(statusParts));
    
    textWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_VSCROLL | WS_CHILD | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
        32, 312, 376, 300,
        mainWnd, NULL, hInst, NULL
    );
    SendMessage(textWnd, EM_SETLIMITTEXT, WPARAM(256000), 0);

    ShowWindow(mainWnd, SW_HIDE);
    UpdateWindow(mainWnd);

    memset(&devFilter, 0, sizeof(devFilter));
    devFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    devFilter.dbcc_size = sizeof(devFilter);
    devFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    RegisterDeviceNotificationW(mainWnd, &devFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    return TRUE;

}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    HWND wnd = (HWND)lParam;

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
                case ID_SETTINGS_HIDGUARDIAN:
                    hidGuardian->createWindow(hInst);
                default:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        if (wnd == settings.getDevWnd()) {
                            GUID oldDevice = settings.getFfbDevice();
                            DWORD vidpid = 0;  
                            if (oldDevice != GUID_NULL)
                                vidpid = getDeviceVidPid(ffdevice); 
                            settings.setFfbDevice(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
                            if (vidpid != 0 && oldDevice != settings.getFfbDevice())
                                hidGuardian->removeDevice(LOWORD(vidpid), HIWORD(vidpid), false);
                        }
                        else if (wnd == settings.getFfbWnd())
                            settings.setFfbType(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
                    }
                    else if (HIWORD(wParam) == BN_CLICKED) {
                        bool oldValue = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        if (wnd == settings.getUse360Wnd())
                            settings.setUse360ForDirect(!oldValue);
                        else if (wnd == settings.getCarSpecificWnd()) {
                            if (!oldValue)
                                getCarName();
                            settings.setUseCarSpecific(!oldValue, car);
                        }
                        else if (wnd == settings.getReduceWhenParkedWnd())
                            settings.setReduceWhenParked(!oldValue);
                        else if (wnd == settings.getRunOnStartupWnd())
                            settings.setRunOnStartup(!oldValue);
                        else if (wnd == settings.getStartMinimisedWnd())
                            settings.setStartMinimised(!oldValue);
                        else if (wnd == settings.getDebugWnd()) {
                            settings.setDebug(!oldValue);
                            if (settings.getDebug()) {
                                debugHnd = CreateFileW(settings.getLogPath(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                                int chars = SendMessageW(textWnd, WM_GETTEXTLENGTH, 0, 0);
                                wchar_t *buf = new wchar_t[chars + 1];
                                SendMessageW(textWnd, WM_GETTEXT, chars + 1, (LPARAM)buf);
                                debug(buf);
                                delete[] buf;
                            }
                            else if (debugHnd != INVALID_HANDLE_VALUE) {
                                CloseHandle(debugHnd);
                                debugHnd = INVALID_HANDLE_VALUE;
                            }
                        }
                    }
                    return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

        case WM_EDIT_VALUE: {
            if (wnd == settings.getMaxWnd()->value)
                settings.setMaxForce(wParam, wnd);
            else if (wnd == settings.getMinWnd()->value)
                settings.setMinForce(wParam, wnd);
            else if (wnd == settings.getBumpsWnd()->value)
                settings.setBumpsFactor(reinterpret_cast<float &>(wParam), wnd);
            else if (wnd == settings.getDampingWnd()->value)
                settings.setDampingFactor(reinterpret_cast<float &>(wParam), wnd);
            else if (wnd == settings.getSopWnd()->value)
                settings.setSopFactor(reinterpret_cast<float &>(wParam), wnd);
            else if (wnd == settings.getSopOffsetWnd()->value)
                settings.setSopOffset(reinterpret_cast<float &>(wParam), wnd);
        }
        break;
             

        case WM_HSCROLL: {
            if (wnd == settings.getMaxWnd()->trackbar)
                settings.setMaxForce(SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
            else if (wnd == settings.getMinWnd()->trackbar)
                settings.setMinForce(SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
            else if (wnd == settings.getBumpsWnd()->trackbar)
                settings.setBumpsFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
            else if (wnd == settings.getDampingWnd()->trackbar)
                settings.setDampingFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
            else if (wnd == settings.getSopWnd()->trackbar)
                settings.setSopFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
            else if (wnd == settings.getSopOffsetWnd()->trackbar)
                settings.setSopOffset((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
        }
        break;

        case WM_CTLCOLORSTATIC: {
            SetBkColor((HDC)wParam, RGB(0xff, 0xff, 0xff));
            return (LRESULT)CreateSolidBrush(RGB(0xff, 0xff, 0xff));
        }
        break;

        case WM_PRINTCLIENT: {
            RECT r = { 0 };
            GetClientRect(hWnd, &r);
            FillRect((HDC)wParam, &r, CreateSolidBrush(RGB(0xff, 0xff, 0xff)));
        }
        break;

        case WM_SIZE: {
            SendMessage(statusWnd, WM_SIZE, wParam, lParam);
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;

        case WM_POWERBROADCAST: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case PBT_APMSUSPEND:
                    debug(L"Computer is suspending, release all");
                    releaseAll();
                break;
                case PBT_APMRESUMESUSPEND:
                    debug(L"Computer is resuming, init all");
                    initAll();
                break;
            }
        }
        break;

        case WM_TRAY_ICON: {
            switch (lParam) {
                case WM_LBUTTONUP:
                    restore();
                    break;
                case WM_RBUTTONUP: {
                    HMENU trayMenu = CreatePopupMenu();
                    POINT curPoint;
                    AppendMenuW(trayMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                    GetCursorPos(&curPoint);
                    SetForegroundWindow(hWnd);
                    if (
                        TrackPopupMenu(
                            trayMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                            curPoint.x, curPoint.y, 0, hWnd, NULL
                        ) == ID_TRAY_EXIT
                    )
                        PostQuitMessage(0);
                    DestroyMenu(trayMenu);
                }
                break;
            }
                    
        }
        break;

        case WM_DEVICECHANGE: {
            DEV_BROADCAST_HDR *hdr = (DEV_BROADCAST_HDR *)lParam;
            if (wParam != DBT_DEVICEARRIVAL && wParam != DBT_DEVICEREMOVECOMPLETE)
                return 0;
            if (hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
                return 0;
            deviceChange();
        }
        break;

        case WM_SYSCOMMAND: {
            switch (wParam & 0xfff0) {
                case SC_MINIMIZE:
                    minimise();
                    return 0;
                default:
                    return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;

        case WM_DESTROY: {
            debug(L"Exiting");
            Shell_NotifyIcon(NIM_DELETE, &niData);
            releaseAll();
            if (settings.getUseCarSpecific() && car[0] != 0)
                settings.writeSettingsForCar(car);
            else
                settings.writeGenericSettings();
            settings.writeRegSettings();
            hidGuardian->stop(GetCurrentProcessId());
            if (debugHnd != INVALID_HANDLE_VALUE)
                CloseHandle(debugHnd);
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

    debug(msg);

}

void text(wchar_t *fmt, char *charstr) {

    int len = strlen(charstr) + 1;
    wchar_t *wstr = new wchar_t[len];
    mbstowcs_s(nullptr, wstr, len, charstr, len);
    text(fmt, wstr);
    delete[] wstr;

}

void debug(wchar_t *fmt, ...) {

    if (!settings.getDebug())
        return;

    DWORD written;
    va_list argp;
    wchar_t msg[512];
    SYSTEMTIME lt;

    GetLocalTime(&lt);
    StringCbPrintf(
        msg, sizeof(msg), L"%d-%02d-%02d %02d:%02d:%02d.%03d ",
        lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds
    );

    int len = wcslen(msg);

    va_start(argp, fmt);
    StringCbVPrintf(msg + len, sizeof(msg) - (len + 2) * sizeof(wchar_t), fmt, argp);
    va_end(argp);

    StringCbCat(msg, sizeof(msg), L"\r\n");
    if (!wcscmp(msg + len, debugLastMsg)) {
        debugRepeat++;
        return;
    }
    else if (debugRepeat) {
        wchar_t rm[256];
        StringCbPrintfW(rm, sizeof(rm), L"-- Last message repeated %d times --\r\n", debugRepeat);
        WriteFile(debugHnd, rm, wcslen(rm) * sizeof(wchar_t), &written, NULL);
        debugRepeat = 0;
    }

    StringCbCopy(debugLastMsg, sizeof(debugLastMsg), msg + len);
    WriteFile(debugHnd, msg, wcslen(msg) * sizeof(wchar_t), &written, NULL);

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

    if (!onTrack && deviceChangePending) {
        debug(L"Processing deferred device change notification");
        deviceChange();
    }

}

void setLogiWheelRange(WORD prodId) {

    if (prodId == G25PID || prodId == DFGTPID || prodId == G27PID) {

        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        text(L"DFGT/G25/G27 detected, setting range using raw HID");

        HANDLE devInfoSet = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devInfoSet == INVALID_HANDLE_VALUE) {
            text(L"LogiWheel: Error enumerating HID devices");
            return;
        }

        SP_DEVICE_INTERFACE_DATA intfData;
        SP_DEVICE_INTERFACE_DETAIL_DATA *intfDetail;
        DWORD idx = 0;
        DWORD error = 0;
        DWORD size;

        while (true) {

            intfData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

            if (!SetupDiEnumDeviceInterfaces(devInfoSet, NULL, &hidGuid, idx++, &intfData)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;
                continue;
            }

            if (!SetupDiGetDeviceInterfaceDetailW(devInfoSet, &intfData, NULL, 0, &size, NULL))
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                    text(L"LogiWheel: Error getting intf detail");
                    continue;
                }

            intfDetail = (SP_DEVICE_INTERFACE_DETAIL_DATA *)malloc(size);
            intfDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (!SetupDiGetDeviceInterfaceDetailW(devInfoSet, &intfData, intfDetail, size, NULL, NULL)) {
                free(intfDetail);
                continue;
            }

            if (
                wcsstr(intfDetail->DevicePath, G25PATH)  != NULL ||
                wcsstr(intfDetail->DevicePath, DFGTPATH) != NULL ||
                wcsstr(intfDetail->DevicePath, G27PATH) != NULL
            ) {

                HANDLE file = CreateFileW(
                    intfDetail->DevicePath,
                    GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
                );

                if (file == INVALID_HANDLE_VALUE) {
                    text(L"LogiWheel: Failed to open HID device");
                    free(intfDetail);
                    SetupDiDestroyDeviceInfoList(devInfoSet);
                    return;
                }

                DWORD written;

                if (!WriteFile(file, LOGI_WHEEL_HID_CMD, LOGI_WHEEL_HID_CMD_LEN, &written, NULL))
                    text(L"LogiWheel: Failed to write to HID device");
                else
                    text(L"LogiWheel: Range set to 900 deg via raw HID");

                CloseHandle(file);
                free(intfDetail);
                SetupDiDestroyDeviceInfoList(devInfoSet);
                return;

            }

            free(intfDetail);

        }

        text(L"Failed to locate Logitech wheel HID device, can't set range");
        SetupDiDestroyDeviceInfoList(devInfoSet);
        return;

    }

    text(L"Attempting to set range via LGS");

    UINT msgId = RegisterWindowMessage(L"LGS_Msg_SetOperatingRange");
    if (!msgId) {
        text(L"Failed to register LGS window message, can't set range..");
        return;
    }

    HWND LGSmsgHandler =
        FindWindowW(
            L"LCore_MessageHandler_{C464822E-04D1-4447-B918-6D5EB33E0E5D}",
            NULL
        );

    if (LGSmsgHandler == NULL) {
        text(L"Failed to locate LGS msg handler, can't set range..");
        return;
    }

    SendMessageW(LGSmsgHandler, msgId, prodId, 900);
    text(L"Range of Logitech wheel set to 900 deg via LGS");

}

BOOL CALLBACK EnumFFDevicesCallback(LPCDIDEVICEINSTANCE diDevInst, VOID *wnd) {

    UNREFERENCED_PARAMETER(wnd);

    if (lstrcmp(diDevInst->tszProductName, L"vJoy Device") == 0)
        return true;

    settings.addFfbDevice(diDevInst->guidInstance, diDevInst->tszProductName);
    debug(L"Adding DI device: %s", diDevInst->tszProductName);

    return true;

}

BOOL CALLBACK EnumObjectCallback(const LPCDIDEVICEOBJECTINSTANCE inst, VOID *dw) {

    UNREFERENCED_PARAMETER(inst);

    (*(int *)dw)++;
    return DIENUM_CONTINUE;

}

void enumDirectInput() {

    settings.clearFfbDevices();

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
    di.dwSize = sizeof(DIDEVICEINSTANCE);

    if (ffdevice && effect && ffdevice->GetDeviceInfo(&di) >= 0 && di.guidInstance == settings.getFfbDevice())
        return;

    releaseDirectInput();

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

    if (FAILED(ffdevice->GetDeviceInfo(&di))) {
        text(L"Failed to get info for DI device!");
        return;
    }

    if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID *)&numButtons, DIDFT_BUTTON))) {
        text(L"Failed to enumerate DI device buttons");
        return;
    }

    if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID *)&numPov, DIDFT_POV))) {
        text(L"Failed to enumerate DI device povs");
        return;
    }

    if (FAILED(ffdevice->SetEventNotification(wheelEvent))) {
        text(L"Failed to set event notification on DI device");
        return;
    }

    DWORD vidpid = getDeviceVidPid(ffdevice);
    if (LOWORD(vidpid) == 0x046d) {
        logiWheel = true;
        setLogiWheelRange(HIWORD(vidpid));
    }
    else
        logiWheel = false;

    if (FAILED(ffdevice->Acquire())) {
        text(L"Failed to acquire DI device");
        return;
    }

    text(L"Acquired DI device with %d buttons and %d POV", numButtons, numPov);

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

    if (vidpid != 0)
        hidGuardian->setDevice(LOWORD(vidpid), HIWORD(vidpid));

}

void releaseDirectInput() {

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

}

void reacquireDIDevice() {

    if (ffdevice == nullptr) {
        debug(L"!! ffdevice was null during reacquire !!");
        return;
    }

    HRESULT hr;

    ffdevice->Unacquire();
    ffdevice->Acquire();

    if (effect == nullptr) {
        if (FAILED(ffdevice->CreateEffect(GUID_Sine, &dieff, &effect, nullptr))) {
            text(L"Failed to create periodic effect during reacquire");
            return;
        }
    }

    hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
    if (hr == DIERR_NOTINITIALIZED || hr == DIERR_INPUTLOST || hr == DIERR_INCOMPLETEEFFECT || hr == DIERR_INVALIDPARAM)
        text(L"Error setting parameters of DIEFFECT during reacquire: 0x%x", hr);

}

inline void sleepSpinUntil(PLARGE_INTEGER base, UINT sleep, UINT offset) {

    LARGE_INTEGER time;
    LONGLONG until = base->QuadPart + (offset * freq.QuadPart) / 1000000;

    std::this_thread::sleep_for(std::chrono::microseconds(sleep));
    do {
        _asm { pause };
        QueryPerformanceCounter(&time);
    } while (time.QuadPart < until);

}

inline int scaleTorque(float t) {

    return (int)(t * settings.getScaleFactor());

}

inline void setFFB(int mag) {

    if (!effect)
        return;

    int amag = abs(mag);

    if (amag > maxSample)
        maxSample = amag;

    if (mag <= -IR_MAX) {
        mag = -IR_MAX;
        clippedSamples++;
    }
    else if (mag >= IR_MAX) {
        mag = IR_MAX;
        clippedSamples++;
    }

    samples++;
    int minForce = settings.getMinForce();

    if (stopped && settings.getReduceWhenParked())
        mag /= 4;
    else if (minForce) {
        if (mag > 0 && mag < minForce)
            mag = minForce;
        else if (mag < 0 && mag > -minForce)
            mag = -minForce;
    }

    float df = damperForce;

    pforce.lOffset = mag + scaleTorque(df);
    HRESULT hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
    if (hr != DI_OK) {
        debug(L"SetParameters returned 0x%x, requesting reacquire", hr);
        reacquireNeeded = true;
    }

}

bool initVJD() {

    WORD verDll, verDrv;
    int maxVjDev;
    VjdStat vjdStatus = VJD_STAT_UNKN;

    if (!vJoyEnabled()) {
        text(L"vJoy not enabled!");
        return false;
    }
    else if (!DriverMatch(&verDll, &verDrv)) {
        text(L"vJoy driver version %04x != required version %04x!", verDrv, verDll);
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

    releaseDirectInput();

    if (fan)
        fan->setSpeed(0);

    RelinquishVJD(vjDev);

    irsdk_shutdown();

}