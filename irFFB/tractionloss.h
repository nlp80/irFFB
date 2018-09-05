#pragma once

#include <WbemCli.h>
#include <comdef.h>

#include "stdafx.h"
#include "irFFB.h"
#include "fan.h"

class TractionLoss
{

    public:
        static TractionLoss *init();
        void createWindow(HINSTANCE);
        float getMinAngle() { return minAngle / 57.2958f; };
        void setAngle(float);
        void setEnabled(bool enable);
    private:
        TractionLoss();
        ATOM registerClass(HINSTANCE);
        static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
        void setMinAngle(int);
        void setStepsPerDeg(int);
        void enumSerialPorts();
        void initPort();
        void readSettings();
        void writeSettings();

        static TractionLoss *instance;

        wchar_t *windowClass = L"Traction Loss settings";
        HWND mainWnd, portWnd;
        sWins_t *minAngleWnd = nullptr, *stepsPerDegWnd = nullptr;
        int numPorts = 0;
        wchar_t *tlPort = nullptr;
        port *ports = nullptr;
        HANDLE tlHandle = INVALID_HANDLE_VALUE;
        wchar_t strbuf[64];
        bool classIsRegistered = false;
        bool isEnabled = false;
        int16_t minPos = 0, maxPos = 0, maxDelta = 0;
        float minAngle, stepsPerDeg;

};

