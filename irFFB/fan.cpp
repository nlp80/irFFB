#include <shlwapi.h>

#include "fan.h"

Fan *Fan::instance = nullptr;

Fan *Fan::init() {
    return new Fan();
}

Fan::Fan() {

    instance = this;

    if (FAILED(CoInitializeEx(0, COINIT_MULTITHREADED)))
        return;

    CoInitializeSecurity(
        NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL
    );

    readSettings();
    enumSerialPorts();

}

ATOM Fan::registerClass(HINSTANCE hInstance) {

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

void Fan::createWindow(HINSTANCE hInst) {

    if (!classIsRegistered) {
        registerClass(hInst);
        classIsRegistered = true;
    }

    mainWnd = CreateWindowW(
        windowClass, windowClass, WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 432, 380,
        NULL, NULL, hInst, NULL
    );

    if (!mainWnd)
        return;

    windSimFormatWnd = checkbox(mainWnd, L"Use WindSim format?", 40, 20);
    portWnd = combo(mainWnd, L"Fan controller port:", 40, 80);

    CreateWindowW(
        L"STATIC", L"Maximum car speed:   (0 = auto)",
        WS_CHILD | WS_VISIBLE,
        40, 160, 300, 20, mainWnd, NULL, hInst, NULL
    );
    maxSpeedWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE, L"EDIT", L"0",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_OVERLAPPED | ES_NUMBER,
        52, 184, 188, 24, mainWnd, NULL, hInst, NULL
    );
    speedUnitsWnd = CreateWindowW(
        L"COMBOBOX", nullptr,
        CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_OVERLAPPED | WS_TABSTOP,
        240, 184, 112, 200, mainWnd, nullptr, hInst, nullptr
    );
    SendMessage(speedUnitsWnd, CB_ADDSTRING, 0, LPARAM(unitStrings[MPH]));
    SendMessage(speedUnitsWnd, CB_ADDSTRING, 0, LPARAM(unitStrings[KPH]));

    manualWnd = slider(mainWnd, L"Manual fan speed:", 40, 234, L"0", L"100", false);

    setManualSpeed((int)manualSpeed);
    ShowWindow(mainWnd, SW_SHOWNORMAL);
    UpdateWindow(mainWnd);
    enumSerialPorts();
    readSettings();

    return;

}

LRESULT CALLBACK Fan::wndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

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
                                instance->fanPort = instance->ports[idx].dev;
                                instance->initFanPort();
                            }
                        }
                        else if (wnd == instance->speedUnitsWnd) {
                            instance->setSpeedUnits(SendMessage(wnd, CB_GETCURSEL, 0, 0));
                        }
                    }
                    else if (HIWORD(wParam) == BN_CLICKED) {
                        bool oldValue = SendMessage(wnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        if (wnd == instance->windSimFormatWnd)
                            instance->setWindSimFormat(!oldValue);
                    }
                    break;
            }
        }
        break;

        case WM_HSCROLL: {
            if (wnd == instance->manualWnd->trackbar) {
                wchar_t strbuf[8];
                int spd = SendMessage(wnd, TBM_GETPOS, 0, 0);
                swprintf_s(strbuf, L"%d", spd);
                instance->setManualSpeed(spd);
                SendMessage(instance->manualWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
            }
        }
        break;

        case WM_EDIT_VALUE: {
            if (wnd == instance->manualWnd->value) {
                instance->setManualSpeed(wParam);
                SendMessage(instance->manualWnd->trackbar, TBM_SETPOS, true, wParam);
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
            wchar_t buf[16];
            SendMessage(instance->maxSpeedWnd, WM_GETTEXT, 16, LPARAM(buf));
            instance->setMaxSpeed(StrToInt(buf));
            instance->writeSettings();
        }
        break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);

        }

    return 0;

}

void Fan::setSpeedUnits(int u) {
    
    if (u < 0 || u > 1)
        return;
    
    units = u;
    if (SendMessageW(instance->maxSpeedWnd, WM_GETTEXT, 16, LPARAM(strbuf)))
        maxSpeed = StrToInt(strbuf);
    setMaxSpeed(maxSpeed);
    SendMessage(speedUnitsWnd, CB_SETCURSEL, u, 0);
    
}

void Fan::setMaxSpeed(int s) {

    maxSpeed = s;

    if (s == 0) {
        maxSpeedMs = 53;
        goto UPDATE;
    }

    if (units == MPH)
        maxSpeedMs = (float)s / 2.237f;
    else
        maxSpeedMs = (float)s / 3.6f;

UPDATE:
    wchar_t buf[16];
    _itow_s(s, buf, 10);
    SendMessage(maxSpeedWnd, WM_SETTEXT, 0, LPARAM(buf));

}

void Fan::setManualSpeed(int s) {

    SendMessage(manualWnd->trackbar, TBM_SETPOS, TRUE, s);
    swprintf_s(strbuf, L"%d", s);
    SendMessage(manualWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    manualSpeed = (float)s;
    setSpeed(manualSpeed * maxSpeedMs / 100);

}

void Fan::setWindSimFormat(bool ws) {

    SendMessage(windSimFormatWnd, BM_SETCHECK, ws ? BST_CHECKED : BST_UNCHECKED, NULL);
    bool reInit = windSimFormat != ws;
    windSimFormat = ws;
    if (reInit)
        initFanPort();

}

void Fan::enumSerialPorts() {

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

                    if (fanPort != nullptr && _wcsnicmp(ports[numPorts + i].dev, fanPort, 128) == 0) {
                        SendMessage((HWND)portWnd, CB_SETCURSEL, numPorts + i, 0);
                        if (fanHandle == INVALID_HANDLE_VALUE)
                            initFanPort();
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

void Fan::initFanPort() {

    if (fanPort == nullptr)
        return;

    wchar_t *settings = windSimFormat ? L"9600,n,8,1" : L"115200,n,8,1";

    if (fanHandle != INVALID_HANDLE_VALUE)
        CloseHandle(fanHandle);

    fanHandle = CreateFile(
        fanPort, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, 0
    );

    DCB dcb;

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!BuildCommDCB(settings, &dcb)) {
        text(L"Error building fan control port parameters");
        CloseHandle(fanHandle);
        fanHandle = INVALID_HANDLE_VALUE;
        return;
    }

    dcb.fAbortOnError = FALSE;

    if (!SetCommState(fanHandle, &dcb)) {
        text(L"Error setting fan control port parameters");
        CloseHandle(fanHandle);
        fanHandle = INVALID_HANDLE_VALUE;
        return;
    }
    
    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = 10;
    SetCommTimeouts(fanHandle, &timeouts);
    
    text(L"Connected to fan control port");

}

void Fan::setSpeed(float speed) {

    if (fanHandle == INVALID_HANDLE_VALUE)
        return;

    if (maxSpeed == 0 && speed > maxSpeedMs)
        maxSpeedMs = speed;

    byte buf[4];

    speed *= 100 / maxSpeedMs;
    speed = minf(speed, 100.0f);
    speed *= windSimFormat ? 2.55f : 1.6f;

    int ispeed = (int)speed;

    buf[0] = (byte)(ispeed);
    DWORD written;
    DWORD toWrite = 1;

    if (windSimFormat) {
        buf[3] = buf[0];
        buf[0] = buf[2] = 255;
        buf[1] = 88;
        toWrite = 4;
    }

    COMSTAT comstat;
    DWORD errors;

    if (!WriteFile(fanHandle, buf, toWrite, &written, NULL))
        if (GetLastError() != ERROR_IO_PENDING)
            ClearCommError(fanHandle, &errors, &comstat);

}

void Fan::setManualSpeed() {
    setSpeed(manualSpeed * maxSpeedMs / 100);
}

void Fan::readSettings() {

    HKEY key = Settings::getSettingsRegKey();

    if (fanPort != nullptr)
        delete[] fanPort;

    if (key == NULL) {
        setWindSimFormat(true);
        delete[] fanPort;
        fanPort = nullptr;
        setMaxSpeed(120);
        setSpeedUnits(MPH);
        return;
    }

    fanPort = new wchar_t[128];
    DWORD fanPortSize = 128 * sizeof(wchar_t);

    if (RegGetValueW(key, nullptr, L"fanPort", RRF_RT_REG_SZ, nullptr, fanPort, &fanPortSize)) {
        delete[] fanPort;
        fanPort = nullptr;
    }
    
    setWindSimFormat(Settings::getRegSetting(key, L"fanWindSimFormat", true));
    setMaxSpeed(Settings::getRegSetting(key, L"fanMaxSpeed", 120));
    setSpeedUnits(Settings::getRegSetting(key, L"fanMaxSpeedUnits", MPH));

    RegCloseKey(key);

}

void Fan::writeSettings() {

    HKEY key = Settings::getSettingsRegKey();
    DWORD fanPortSz = 0;
    
    if (key == NULL)
        return;
    
    if (fanPort) {
        fanPortSz = (lstrlen(fanPort) + 1) * sizeof(wchar_t);
        RegSetValueEx(key, L"fanPort", 0, REG_SZ, (BYTE *)fanPort, fanPortSz);
    }
        
    Settings::setRegSetting(key, L"fanWindSimFormat", windSimFormat);
    Settings::setRegSetting(key, L"fanMaxSpeed", maxSpeed);
    Settings::setRegSetting(key, L"fanMaxSpeedUnits", units);

    RegCloseKey(key);

}