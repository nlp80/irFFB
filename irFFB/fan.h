#pragma once

#include <WbemCli.h>
#include <comdef.h>

#include "stdafx.h"
#include "irFFB.h"

typedef struct {
    wchar_t *name;
    wchar_t *dev;
} port;

#define MPH 0
#define KPH 1

class Fan {

    public:
        static Fan *init();
        void createWindow(HINSTANCE);
        void setSpeed(float);
        void setManualSpeed();

    private:
        Fan();
        ATOM registerClass(HINSTANCE);
        static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
        void enumSerialPorts();
        void initFanPort();
        void setSpeedUnits(int);
        void setMaxSpeed(int);
        void setManualSpeed(int);
        void setWindSimFormat(bool);
        void readSettings();
        void writeSettings();
        
        static Fan *instance;

        wchar_t *windowClass = L"Fan settings";
        wchar_t *unitStrings[2] = { L"mph", L"kph" };
        HWND mainWnd, portWnd, maxSpeedWnd, speedUnitsWnd, windSimFormatWnd;
        sWins_t *manualWnd;
        bool classIsRegistered = false, windSimFormat = true;
        float maxSpeedMs = 0, manualSpeed = 0;
        int numPorts = 0, maxSpeed = 0, units = MPH;
        wchar_t *fanPort = nullptr;
        port *ports = nullptr;
        HANDLE fanHandle = INVALID_HANDLE_VALUE;
        wchar_t strbuf[64];

};

