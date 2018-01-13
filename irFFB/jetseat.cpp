#include "jetseat.h"

JetSeat *JetSeat::instance = nullptr;

JetSeat *JetSeat::init() {
    
    JetSeat *js = new JetSeat();
    if (js->initialise())
        return js;
    
    return nullptr;

}

JetSeat::JetSeat() {
    instance = this;
}

JetSeat::~JetSeat() {

    UINT status;

    if (FAILED(jetseat->uwGetStatus(&status)))
        return;

    if (status == UW_STATUS_IN_USE)
        jetseat->uwClose();

}

bool JetSeat::initialise() {

    readSettings();

    if (FAILED(CoInitializeEx(0, COINIT_MULTITHREADED)))
        return false;

    CoInitializeSecurity(
        NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL
     );

    HRESULT hr = CoCreateInstance(CLSID_Uberwoorf, NULL, CLSCTX_ALL, IID_IUberwoorf, (void **)&jetseat);
    if (FAILED(hr))
        return false;

    if (enabled)
        return open();
    
    return true;

}

bool JetSeat::open() {

    UINT uwOpenStatus;
    if (FAILED(jetseat->uwOpen(&uwOpenStatus))) {
        text(L"JetSeat found but open failed");
        return false;
    }

    jetseat->uwReset();
    text(L"JetSeat init OK");
    return initEffects();

}

bool JetSeat::initEffects() {

    effect_handles[GW_LEG_LEFT]    = createEffect(UW_SIT_LEFT);
    effect_handles[GW_LEG_RIGHT]   = createEffect(UW_SIT_RIGHT);
    effect_handles[GW_LEG_BOTH]    = createEffect(UW_SIT);
    effect_handles[GW_SLIDE_LEFT]  = createSlideEffect(UW_SIT_LEFT);
    effect_handles[GW_SLIDE_RIGHT] = createSlideEffect(UW_SIT_RIGHT);
    effect_handles[GW_ALL_LEFT]    = createEffect(UW_LEFT);
    effect_handles[GW_ALL_RIGHT]   = createEffect(UW_RIGHT);
    effect_handles[GW_ALL_BOTH]    = createEffect(UW_ALL_ZONES);
    effect_handles[GW_BACK_LEFT]   = createEffect(UW_BACK_LOW_LEFT);
    effect_handles[GW_BACK_RIGHT]  = createEffect(UW_BACK_LOW_RIGHT);
    effect_handles[GW_BACK_BOTH]   = createEffect(UW_BACK_LOW);

    for (int i = GW_LEG_LEFT; i <= GW_BACK_BOTH; i++)
        if (effect_handles[i] < 0) {
            text(L"Create effect %d failed\n", i);
            return false;
        }

    createEngineEffect();

    text(L"JetSeat effects initialised");
    startEffect(GW_BACK_BOTH, 100, 1);

    return true;

}

ATOM JetSeat::registerClass(HINSTANCE hInstance) {

    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IRFFB));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = 0;
    wcex.lpszClassName = windowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);

}

void JetSeat::effectControls(
    wchar_t *effect, int x, int y, HWND *placeWnd, sWins_t *gainWnd, HINSTANCE hInst
) {

    wchar_t gain[64];
    swprintf_s(gain, L"%s gain:",   effect);

    if (placeWnd != nullptr) {

        wchar_t eff[64];
        swprintf_s(eff, L"%s effect location:", effect);

        CreateWindowW(
            L"STATIC", eff,
            WS_CHILD | WS_VISIBLE,
            x, y, 200, 20, mainWnd, NULL, hInst, NULL
        );

        *placeWnd = CreateWindow(
            L"COMBOBOX", nullptr,
            CBS_DROPDOWN | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            x, y + 40, 200, 100, mainWnd, nullptr, hInst, nullptr
        );

        for (int i = 0; i <= ALL; i++)
            SendMessage(*placeWnd, CB_ADDSTRING, 0, LPARAM(effectPlaces[i]));

    }

    gainWnd->value = CreateWindowW(
        L"STATIC", gain,
        WS_CHILD | WS_VISIBLE,
        x + 256, y, 200, 20, mainWnd, NULL, hInst, NULL
    );

    gainWnd->trackbar = CreateWindowEx(
        0, TRACKBAR_CLASS, gain,
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND,
        x + 256, y + 40, 320, 30,
        mainWnd, NULL, hInst, NULL
    );

    HWND buddyLeft = CreateWindowEx(
        0, L"STATIC", L"0",
        SS_LEFT | WS_CHILD | WS_VISIBLE,
        0, 0, 20, 20, mainWnd, NULL, hInst, NULL
    );

    SendMessage(gainWnd->trackbar, TBM_SETBUDDY, (WPARAM)TRUE, (LPARAM)buddyLeft);

    HWND buddyRight = CreateWindowEx(
        0, L"STATIC", L"100",
        SS_RIGHT | WS_CHILD | WS_VISIBLE,
        0, 0, 30, 20, mainWnd, NULL, hInst, NULL
    );

    SendMessage(gainWnd->trackbar, TBM_SETBUDDY, (WPARAM)FALSE, (LPARAM)buddyRight);

}

void JetSeat::createWindow(HINSTANCE hInst) {

    if (!classIsRegistered) {
        registerClass(hInst);
        classIsRegistered = true;
    }

    mainWnd = CreateWindowW(
        windowClass, windowClass, WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 712, 480,
        NULL, NULL, hInst, NULL
    );

    if (!mainWnd)
        return;

    enableWnd = CreateWindowEx(
        0, L"BUTTON", L"Enable irFFB control",
        BS_CHECKBOX | BS_MULTILINE | WS_CHILD | WS_VISIBLE,
        40, 260, 180, 58, mainWnd, nullptr, hInst, nullptr
    );
    effectControls(L"Engine", 40, 30, &enginePlaceWnd, &engineGainWnd, hInst);
    effectControls(L"Gear shift", 40, 130, &gearPlaceWnd, &gearGainWnd, hInst);
    effectControls(L"Bumps", 40, 230, nullptr, &bumpsGainWnd, hInst);
    effectControls(L"Slide", 40, 330, nullptr, &yawGainWnd, hInst);

    readSettings();

    ShowWindow(mainWnd, SW_SHOWNORMAL);
    UpdateWindow(mainWnd);

    return;

}

LRESULT CALLBACK JetSeat::wndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    switch (message) {

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                if ((HWND)lParam == instance->gearPlaceWnd)
                    instance->setGearPlace(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
                else if ((HWND)lParam == instance->enginePlaceWnd) {
                    instance->setEnginePlace(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
                }
            }
            else if (HIWORD(wParam) == BN_CLICKED) {
                bool oldValue = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if ((HWND)lParam == instance->enableWnd)
                    instance->setEnabled(!oldValue);
            }
            break;
        }
    }
    break;

    case WM_HSCROLL: {
        if ((HWND)lParam == instance->gearGainWnd.trackbar)
            instance->gearGain = (float)(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
        else if ((HWND)lParam == instance->bumpsGainWnd.trackbar)
            instance->bumpsGain = (float)(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
        else if ((HWND)lParam == instance->engineGainWnd.trackbar)
            instance->engineGain = (float)(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
        else if ((HWND)lParam == instance->yawGainWnd.trackbar)
            instance->yawGain = (float)(SendMessage((HWND)lParam, TBM_GETPOS, 0, 0));
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

    case WM_DESTROY: {
        instance->writeSettings();
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);

    }

    return 0;

}

void JetSeat::setGearPlace(int loc) {
    SendMessage(gearPlaceWnd, CB_SETCURSEL, loc, 0);
    gearPlace = effectLocation(loc);
}
int JetSeat::getGearPlace() {
    return effectSetting(gearPlace);
}

void JetSeat::setEnginePlace(int loc) {
    SendMessage(enginePlaceWnd, CB_SETCURSEL, loc, 0);
    enginePlace = effectLocation(loc);
    switch (enginePlace) {
        case GW_LEG_BOTH: {
            engineLeft = UW_SIT_LEFT;
            engineRight = UW_SIT_RIGHT;
        }
        break;
        case GW_BACK_BOTH: {
            engineLeft = UW_BACK_LOW_LEFT;
            engineRight = UW_BACK_LOW_RIGHT;
        }
        break;
        case GW_ALL_BOTH: {
            engineLeft = UW_LEFT;
            engineRight = UW_RIGHT;
        }
        break;
    }
    if (engineOn)
        stopEngineEffect();
}
int JetSeat::getEnginePlace() {
    return effectSetting(enginePlace);
}

void JetSeat::setGearGain(float gain) {
    gearGain = gain;
    SendMessage(gearGainWnd.trackbar, TBM_SETPOS, TRUE, (int)gain);
    SendMessage(gearGainWnd.trackbar, TBM_SETPOSNOTIFY, 0, (int)gain);
    swprintf_s(strbuf, L"Gear shift gain  [ %d ]", (int)gain);
    SendMessage(gearGainWnd.value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
void JetSeat::setEngineGain(float gain) {
    engineGain = gain;
    SendMessage(engineGainWnd.trackbar, TBM_SETPOS, TRUE, (int)gain);
    SendMessage(engineGainWnd.trackbar, TBM_SETPOSNOTIFY, 0, (int)gain);
    swprintf_s(strbuf, L"Engine gain  [ %d ]", (int)gain);
    SendMessage(engineGainWnd.value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
void JetSeat::setBumpsGain(float gain) {
    bumpsGain = gain;
    SendMessage(bumpsGainWnd.trackbar, TBM_SETPOS, TRUE, (int)gain);
    SendMessage(bumpsGainWnd.trackbar, TBM_SETPOSNOTIFY, 0, (int)gain);
    swprintf_s(strbuf, L"Bumps gain  [ %d ]", (int)gain);
    SendMessage(bumpsGainWnd.value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
void JetSeat::setYawGain(float gain) {
    yawGain = gain;
    SendMessage(yawGainWnd.trackbar, TBM_SETPOS, TRUE, (int)gain);
    SendMessage(yawGainWnd.trackbar, TBM_SETPOSNOTIFY, 0, (int)gain);
    swprintf_s(strbuf, L"Slide gain  [ %d ]", (int)gain);
    SendMessage(yawGainWnd.value, WM_SETTEXT, NULL, LPARAM(strbuf));
}

void JetSeat::setEnabled(bool en) {
    
    UINT status;

    enabled = en;
    SendMessage(enableWnd, BM_SETCHECK, en ? BST_CHECKED : BST_UNCHECKED, NULL);
    EnableWindow(gearPlaceWnd, en);
    EnableWindow(gearGainWnd.trackbar, en);
    EnableWindow(gearGainWnd.value, en);
    EnableWindow(enginePlaceWnd, en);
    EnableWindow(engineGainWnd.trackbar, en);
    EnableWindow(engineGainWnd.value, en);
    EnableWindow(bumpsGainWnd.trackbar, en);
    EnableWindow(bumpsGainWnd.value, en);
    EnableWindow(yawGainWnd.trackbar, en);
    EnableWindow(yawGainWnd.value, en);

    if (!jetseat) 
        return;

    if (FAILED(jetseat->uwGetStatus(&status))) {
        text(L"Failed to get JetSeat status");
        enabled = false;
        return;
    }

    if (!en && status == UW_STATUS_IN_USE) {
        jetseat->uwClose();
        text(L"JetSeat closed");
        return;
    }

    else if (en && status == UW_STATUS_READY) {
        if (open())
            return;
        else
            enabled = false;
    }
    
}

bool JetSeat::isEnabled() {
    return enabled;
}

int JetSeat::effectLocation(int idx) {

    switch (idx) {
        case LEGS: return GW_LEG_BOTH;
        case BACK: return GW_BACK_BOTH;
        case ALL:  return GW_ALL_BOTH;
    }

    return GW_ALL_BOTH;

}

int JetSeat::effectSetting(int loc) {

    switch (loc) {
        case GW_LEG_BOTH:  return LEGS;
        case GW_BACK_BOTH: return BACK;
        case GW_ALL_BOTH:  return ALL;
    }

    return ALL;

}

int JetSeat::createEffect(UINT zones) {

    UINT handle, vib;
    UINT dur[2] = { 0, 100 };
    BYTE amp[2] = { 255, 255 };

    jetseat->uwCreateEffect(&handle);
    jetseat->uwSetVibration(handle, zones, dur, amp, 2, &vib);

    return handle;

}

int JetSeat::createSlideEffect(UINT zones) {

    UINT handle, vib;
    UINT dur[6] = { 0, 50, 50, 75, 75, 125 };
    BYTE amp[6] = { 255, 255, 0, 0, 255, 255 };

    jetseat->uwCreateEffect(&handle);
    jetseat->uwSetVibration(handle, zones, dur, amp, 6, &vib);

    return handle;

}

void JetSeat::createEngineEffect() {

    UINT dur[2] = { 150, 150 };
    BYTE amp[2] = { 0, 0 };

    jetseat->uwCreateEffect(&engineEffect);
    jetseat->uwSetVibration(engineEffect, engineLeft, dur, amp, 2, &engineVibL1);
    jetseat->uwSetVibration(engineEffect, engineRight, dur, amp, 2, &engineVibL2);
    jetseat->uwSetVibration(engineEffect, engineLeft, dur, amp, 2, &engineVibR1);
    jetseat->uwSetVibration(engineEffect, engineRight, dur, amp, 2, &engineVibR2);

}

void JetSeat::startEffect(int effect, float gain, int count) {

    gain = minf(gain, 100);
    jetseat->uwSetEffectGain(effect_handles[effect], (UINT)gain);
    jetseat->uwStartEffect(effect_handles[effect], count);

}

void JetSeat::stopEffect(int effect) {

    jetseat->uwStopEffect(effect_handles[effect]);

}

void JetSeat::startEngineEffect() {

    if (engineGain == 0.0f || engineOn)
        return;
    jetseat->uwStartEffect(engineEffect, INFINITE);
    engineOn = true;
    engineCounter = 0;

}

void JetSeat::stopEngineEffect() {

    if (!jetseat || !engineOn)
        return;
    jetseat->uwStopEffect(engineEffect);
    engineOn = false;

}

void JetSeat::updateEngineEffect(float rpmPerCent) {

    if (engineGain == 0 || !engineOn || engineCounter++ < 10)
        return;

    UINT intervalsOn[2];
    UINT intervalsOff[2];
    BYTE amps[2] = { 0, 0 };
    BYTE ampsZero[2] = { 0, 0 };

    engineCounter = 0;

    UINT on = (UINT)(rpmPerCent * 3.0f);

    if (on < 60)
        on = 60;

    int amp = (int)(engineGain * 2.55f);

    int space = 150 - on;
    UINT delay = space > 0 ? space >> 1 : 0;

    amps[0] = amps[1] = amp > 255 ? 255 : amp;

    intervalsOff[0] = 0;
    intervalsOn[0] = delay;
    intervalsOn[1] = delay + on;
    intervalsOff[1] = 300 - on - delay;

    jetseat->uwUpdateVibration(engineEffect, engineVibL1, engineLeft, intervalsOn, amps, 2);
    jetseat->uwUpdateVibration(engineEffect, engineVibR1, engineRight, intervalsOff, ampsZero, 2);

    intervalsOff[0] = delay + on;
    intervalsOn[0] = 300 - on - delay;
    intervalsOff[1] = 300;
    intervalsOn[1] = 300 - delay;
    jetseat->uwUpdateVibration(engineEffect, engineVibL2, engineLeft, intervalsOff, ampsZero, 2);
    jetseat->uwUpdateVibration(engineEffect, engineVibR2, engineRight, intervalsOn, amps, 2);

}

void JetSeat::gearEffect() {
    if (gearGain > 0.0f)
        startEffect(gearPlace, gearGain, 1);
}

void JetSeat::fBumpEffect(float l, float r) {
    if (bumpsGain == 0.0f)
        return;
    startEffect(GW_LEG_LEFT, l * bumpsGain, 1);
    startEffect(GW_LEG_RIGHT, r * bumpsGain, 1);
}

void JetSeat::rBumpEffect(float l, float r) {
    if (bumpsGain == 0.0f)
        return;
    startEffect(GW_BACK_RIGHT, l * bumpsGain, 1);
    startEffect(GW_BACK_RIGHT, r * bumpsGain, 1);
}

void JetSeat::yawEffect(float f) {
    if (yawGain == 0.0f)
        return;
    if (f > 0.0f)
        startEffect(GW_SLIDE_LEFT, f * 5.0f * yawGain, 3);
    else
        startEffect(GW_SLIDE_RIGHT, -f * 5.0f * yawGain, 3);
}

void JetSeat::readSettings() {

    HKEY regKey;
    DWORD dval;
    float fval;
    DWORD sz = sizeof(dval);

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

        if (RegGetValueW(regKey, nullptr, L"jsGearPlace", RRF_RT_REG_DWORD, nullptr, &dval, &sz))
            setGearPlace(BACK);
        else
            setGearPlace(dval);

        if (RegGetValue(regKey, nullptr, L"jsEnginePlace", RRF_RT_REG_DWORD, nullptr, &dval, &sz))
            setEnginePlace(ALL);
        else
            setEnginePlace(dval);
        if (RegGetValue(regKey, nullptr, L"jsGearGain", RRF_RT_REG_DWORD, nullptr, &fval, &sz))
            setGearGain(80);
        else
            setGearGain(fval);
        if (RegGetValue(regKey, nullptr, L"jsEngineGain", RRF_RT_REG_DWORD, nullptr, &fval, &sz))
            setEngineGain(0);
        else
            setEngineGain(fval);
        if (RegGetValue(regKey, nullptr, L"jsBumpsGain", RRF_RT_REG_DWORD, nullptr, &fval, &sz))
            setBumpsGain(80);
        else
            setBumpsGain(fval);
        if (RegGetValue(regKey, nullptr, L"jsYawGain", RRF_RT_REG_DWORD, nullptr, &fval, &sz))
            setYawGain(80);
        else
            setYawGain(fval);
        if (RegGetValue(regKey, nullptr, L"jsEnabled", RRF_RT_REG_DWORD, nullptr, &dval, &sz))
            setEnabled(true);
        else
            setEnabled(dval > 0);

    }
    else {
        setGearPlace(BACK);
        setEnginePlace(ALL);
        setGearGain(80);
        setEngineGain(0);
        setBumpsGain(80);
        setYawGain(80);
        setEnabled(true);
    }

}

void JetSeat::writeSettings() {

    HKEY regKey;
    DWORD sz = sizeof(int);
    DWORD gearP = getGearPlace();
    DWORD engineP = getEnginePlace();
    DWORD enabled = isEnabled();

    RegCreateKeyEx(
        HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

        RegSetValueEx(regKey, L"jsGearPlace", 0, REG_DWORD, (BYTE *)&gearP, sz);
        RegSetValueEx(regKey, L"jsEnginePlace", 0, REG_DWORD, (BYTE *)&engineP, sz);
        RegSetValueEx(regKey, L"jsGearGain", 0, REG_DWORD, (BYTE *)&gearGain, sz);
        RegSetValueEx(regKey, L"jsEngineGain", 0, REG_DWORD, (BYTE *)&engineGain, sz);
        RegSetValueEx(regKey, L"jsBumpsGain", 0, REG_DWORD, (BYTE *)&bumpsGain, sz);
        RegSetValueEx(regKey, L"jsYawGain", 0, REG_DWORD, (BYTE *)&yawGain, sz);
        RegSetValueEx(regKey, L"jsEnabled", 0, REG_DWORD, (BYTE *)&enabled, sz);

    }

}

