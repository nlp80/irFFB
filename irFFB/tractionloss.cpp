#include <shlwapi.h>

#include "irFFB.h"
#include "tractionloss.h"

TractionLoss *TractionLoss::instance = nullptr;

TractionLoss *TractionLoss::init() {
    return new TractionLoss();
}

TractionLoss::TractionLoss() {
    
    if (FAILED(CoInitializeEx(0, COINIT_MULTITHREADED)))
        return;

    CoInitializeSecurity(
        NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL
    );

    instance = this;
    readSettings();
    enumSerialPorts();

}

ATOM TractionLoss::registerClass(HINSTANCE hInstance) {

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

void TractionLoss::createWindow(HINSTANCE hInst) {

    if (!classIsRegistered) {
        registerClass(hInst);
        classIsRegistered = true;
    }

    mainWnd = CreateWindowW(
        windowClass, windowClass, WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 360,
        NULL, NULL, hInst, NULL
    );

    if (!mainWnd)
        return;

    portWnd = combo(mainWnd, L"TL controller port:", 40, 40);
    minAngleWnd = slider(mainWnd, L"Min angle:", 40, 120, L"0", L"50", false);
    SendMessage(minAngleWnd->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50));
    stepsPerDegWnd = slider(mainWnd, L"Steps per deg:", 40, 200, L"0", L"500", false);
    SendMessage(stepsPerDegWnd->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 500));

    ShowWindow(mainWnd, SW_SHOWNORMAL);
    UpdateWindow(mainWnd);
    enumSerialPorts();
    readSettings();

    return;

}

LRESULT CALLBACK TractionLoss::wndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    HWND wnd = (HWND)lParam;
    
    switch (message) {

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    if (wnd == instance->portWnd) {
                        int idx = (int)SendMessage(wnd, CB_GETCURSEL, 0, 0);
                        if (idx >= 0 && idx < instance->numPorts) {
                            instance->tlPort = instance->ports[idx].dev;
                            instance->initPort();
                        }
                    }
                }
                break;
            }
        }

        case WM_HSCROLL: {
            if (wnd == instance->minAngleWnd->trackbar) {
                wchar_t strbuf[8];
                int angle = SendMessage(wnd, TBM_GETPOS, 0, 0);
                swprintf_s(strbuf, L"%d", angle);
                instance->setMinAngle(angle);
                SendMessage(instance->minAngleWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
            }
            else if (wnd == instance->stepsPerDegWnd->trackbar) {
                wchar_t strbuf[8];
                int steps = SendMessage(wnd, TBM_GETPOS, 0, 0);
                swprintf_s(strbuf, L"%d", steps);
                instance->setStepsPerDeg(steps);
                SendMessage(instance->stepsPerDegWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
            }
        }
        break;

        case WM_EDIT_VALUE: {
            if (wnd == instance->minAngleWnd->value) {
                instance->setMinAngle(wParam);
                SendMessage(instance->minAngleWnd->trackbar, TBM_SETPOS, true, wParam);
            }
            else if (wnd == instance->stepsPerDegWnd->value) {
                instance->setStepsPerDeg(wParam);
                SendMessage(instance->stepsPerDegWnd->trackbar, TBM_SETPOS, true, wParam);
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

void TractionLoss::setMinAngle(int angle) {
    if (angle < 0 || angle > 50) return;
    minAngle = angle / 10.0f;
    if (minAngleWnd == nullptr) return;
    SendMessage(minAngleWnd->trackbar, TBM_SETPOS, TRUE, angle);
    swprintf_s(strbuf, L"%d", angle);
    SendMessage(minAngleWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
}

void TractionLoss::setStepsPerDeg(int steps) {
    if (steps < 0 || steps > 500) return;
    stepsPerDeg = steps / 10.0f;
    if (stepsPerDegWnd == nullptr) return;
    SendMessage(stepsPerDegWnd->trackbar, TBM_SETPOS, TRUE, steps);
    swprintf_s(strbuf, L"%d", steps);
    SendMessage(stepsPerDegWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
}


void TractionLoss::enumSerialPorts() {

    UINT portsSize = numPorts = 0;

    IWbemLocator *locator = NULL;
    if (
        FAILED(
            CoCreateInstance(
                CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                IID_IWbemLocator, reinterpret_cast<void**>(&locator)
            )
        )
        ) {
        text(L"Failed to CoCreateInstance of WbemLocator");
        return;
    }

    IWbemServices *services = NULL;
    if (
        FAILED(
            locator->ConnectServer(
                _bstr_t("\\\\.\\root\\cimv2"), NULL, NULL, NULL, 0, NULL, NULL, &services
            )
        )
        ) {
        text(L"Failed to connect to cimv2 server");
        locator->Release();
        return;
    }

    IEnumWbemClassObject *classObject = NULL;
    HRESULT hr = services->ExecQuery(
        _bstr_t("WQL"), _bstr_t("SELECT * FROM Win32_SerialPort"),
        WBEM_FLAG_RETURN_WBEM_COMPLETE, NULL, &classObject
    );

    if (FAILED(hr)) {
        _com_error err(hr);
        text(L"SerialPort query failed: %s", err.ErrorMessage());
        services->Release();
        locator->Release();
        return;
    }

    hr = WBEM_S_NO_ERROR;

    while (hr == WBEM_S_NO_ERROR) {

        ULONG num = 0;
        IWbemClassObject *obj[16];

        if (
            SUCCEEDED(
                classObject->Next(
                    WBEM_INFINITE, 16, reinterpret_cast<IWbemClassObject**>(obj), &num
                )
            )
        ) {

            UINT i = 0;

            if (num == 0)
                break;

            portsSize += sizeof(port) * num;
            ports = (port *)realloc(ports, portsSize);

            for (ULONG n = 0; n < num; n++) {

                VARIANT name;
                HRESULT hrGet = obj[n]->Get(L"DeviceID", 0, &name, NULL, NULL);

                if (
                    SUCCEEDED(hrGet) && (name.vt == VT_BSTR) && (wcslen(name.bstrVal) > 3)
                ) {

                    if (_wcsnicmp(name.bstrVal, L"COM", 3) != 0)
                        continue;

                    VARIANT fname;
                    ports[numPorts + i].dev = (wchar_t *)malloc((lstrlen(name.bstrVal) + 5) * sizeof(wchar_t));
                    lstrcpy(ports[numPorts + i].dev, L"\\\\.\\");
                    lstrcat(ports[numPorts + i].dev, name.bstrVal);
                    if (
                        FAILED(obj[n]->Get(L"Name", 0, &fname, NULL, NULL)) ||
                        fname.vt != VT_BSTR
                    )
                        ports[numPorts + i].name = StrDupW(name.bstrVal);
                    else
                        ports[numPorts + i].name = StrDupW(fname.bstrVal);

                    SendMessage(
                        portWnd, CB_ADDSTRING, 0,
                        LPARAM(ports[numPorts + i].name)
                    );

                    if (tlPort != nullptr && _wcsnicmp(ports[numPorts + i].dev, tlPort, 128) == 0) {
                        SendMessage((HWND)portWnd, CB_SETCURSEL, numPorts + i, 0);
                        if (tlHandle == INVALID_HANDLE_VALUE)
                            initPort();
                    }

                    i++;

                }

                obj[n]->Release();

            }

            numPorts += i;

        }
    }

    classObject->Release();
    services->Release();
    locator->Release();

}

void TractionLoss::initPort() {

    byte buf[] = { '[', 'r', 'd', 'S', ']' };

    DWORD written;
    COMSTAT comstat;
    DWORD errors;
    
    if (tlPort == nullptr)
        return;

    wchar_t *settings = L"500000,n,8,1";

    if (tlHandle != INVALID_HANDLE_VALUE)
        CloseHandle(tlHandle);

    tlHandle = CreateFile(
        tlPort, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0
    );

    DCB dcb;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!BuildCommDCB(settings, &dcb)) {
        text(L"Error building TL control port parameters");
        CloseHandle(tlHandle);
        tlHandle = INVALID_HANDLE_VALUE;
        return;
    }

    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = TRUE;

    if (!SetCommState(tlHandle, &dcb)) {
        text(L"Error setting TL control port parameters");
        CloseHandle(tlHandle);
        tlHandle = INVALID_HANDLE_VALUE;
        return;
    }

    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutConstant = 1000;
    SetCommTimeouts(tlHandle, &timeouts);

    Sleep(1000);

    if (!WriteFile(tlHandle, buf, 5, &written, NULL)) {
        if (GetLastError() != ERROR_IO_PENDING)
            ClearCommError(tlHandle, &errors, &comstat);
        text(L"Error writing to TL control port");
        tlHandle = INVALID_HANDLE_VALUE;
        return;
    }

    memset(buf, 0, 5);
    
    for (int i = 0; i < 5; i++)
        if (!ReadFile(tlHandle, buf + i, 1, &written, NULL))
            break;
    
    if (buf[4] != ']') {
        text(L"Error talking to SMC, correct port?");
        CloseHandle(tlHandle);
        tlHandle = INVALID_HANDLE_VALUE;
        return;
    }

    minPos = buf[3] + 12;
    maxPos = 1023 - minPos;
    maxDelta = maxPos - minPos;

    text(L"Connected to TL control port, max delta: %hd", maxDelta);

}

void TractionLoss::setEnabled(bool enabled) {

    static byte buf[5] = { '[', 0, 0, 0, ']' };

    DWORD written;
    COMSTAT comstat;
    DWORD errors;

    if (tlHandle == INVALID_HANDLE_VALUE)
        return;

    isEnabled = enabled;

    if (!enabled) {

        buf[1] = 'A';
        buf[2] = 512 >> 8;
        buf[3] = 0;

        if (!WriteFile(tlHandle, buf, 5, &written, NULL)) {
            text(L"Error writing to TL port: %d", GetLastError());
            if (GetLastError() != ERROR_IO_PENDING)
                ClearCommError(tlHandle, &errors, &comstat);
        }

    }

    buf[1] = 'N';
    buf[2] = 0;
    buf[3] = enabled ? 1 : 0;
    
    if (!WriteFile(tlHandle, buf, 5, &written, NULL)) {
        text(L"Error writing to TL port: %d", GetLastError());
        if (GetLastError() != ERROR_IO_PENDING)
            ClearCommError(tlHandle, &errors, &comstat);
    }

    if (enabled) {

        buf[1] = 'e';
        buf[2] = 'n';
        buf[3] = '1';

        if (!WriteFile(tlHandle, buf, 5, &written, NULL)) {
            text(L"Error writing to TL port: %d", GetLastError());
            if (GetLastError() != ERROR_IO_PENDING)
                ClearCommError(tlHandle, &errors, &comstat);
        }

        text(L"TL enabled");

    }
    else {
        text(L"TL disabled");
    }

}

void TractionLoss::setAngle(float angle) {

    static byte buf[] = { '[', 'A', 0, 0, ']' };

    DWORD written;
    COMSTAT comstat;
    DWORD errors;

    if (tlHandle == INVALID_HANDLE_VALUE)
        return;

    int16_t pos = 512 + (int16_t)(angle * 57.2958f * stepsPerDeg);

    if (pos > maxPos) pos = maxPos;
    else if (pos < minPos) pos = minPos;

    buf[2] = pos >> 8;
    buf[3] = pos & 0xff;

    if (!WriteFile(tlHandle, buf, 5, &written, NULL)) {
        text(L"Error writing to TL control port: %d", GetLastError());
        if (GetLastError() != ERROR_IO_PENDING)
            ClearCommError(tlHandle, &errors, &comstat);
    }

}

void TractionLoss::readSettings() {

    HKEY regKey;
    DWORD val;
    DWORD sz = sizeof(val);

    if (tlPort != nullptr)
        delete[] tlPort;

    tlPort = new wchar_t[128];
    DWORD tlPortSize = 128 * sizeof(wchar_t);

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

        if (RegGetValueW(regKey, nullptr, L"tlPort", RRF_RT_REG_SZ, nullptr, tlPort, &tlPortSize)) {
            delete[] tlPort;
            tlPort = nullptr;
        }
        if (RegGetValueW(regKey, nullptr, L"tlMinAngle", RRF_RT_REG_DWORD, nullptr, (BYTE *)&val, &sz))
            setMinAngle(35);
        else
            setMinAngle(val);
        if (RegGetValueW(regKey, nullptr, L"tlStepsPerDeg", RRF_RT_REG_DWORD, nullptr, (BYTE *)&val, &sz))
            setStepsPerDeg(350);
        else
            setStepsPerDeg(val);

    }
    else {
        delete[] tlPort;
        tlPort = nullptr;
        setMinAngle(35);
        setStepsPerDeg(350);
    }

}

void TractionLoss::writeSettings() {

    HKEY regKey;
    DWORD sz = sizeof(int);
    DWORD tlPortSz = 0;
    DWORD minAngleVal = (DWORD)(minAngle * 10.0f);
    DWORD stepsPerDegVal = (DWORD)(stepsPerDeg * 10.0f);

    if (tlPort)
        tlPortSz = (lstrlen(tlPort) + 1) * sizeof(wchar_t);

    RegCreateKeyEx(
        HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {
        if (tlPort)
            RegSetValueEx(regKey, L"tlPort", 0, REG_SZ, (BYTE *)tlPort, tlPortSz);
        RegSetValueEx(regKey, L"tlMinAngle", 0, REG_DWORD, (BYTE *)&minAngleVal, sz);
        RegSetValueEx(regKey, L"tlStepsPerDeg", 0, REG_DWORD, (BYTE *)&stepsPerDegVal, sz);
    }

}