#pragma once
#include "stdafx.h"
#include "irFFB.h"

class Settings {

    public:
        Settings();
        void setDevWnd(HWND);
        HWND getDevWnd();
        void setFfbWnd(HWND);
        HWND getFfbWnd();
        void setMinWnd(HWND);
        HWND getMinWnd();
        void setMaxWnd(HWND);
        HWND getMaxWnd();
        void setBumpsWnd(HWND);
        HWND getBumpsWnd();
        void setLoadWnd(HWND);
        HWND getLoadWnd();
        void setYawWnd(HWND);
        HWND getYawWnd();
        void setExtraLongWnd(HWND);
        HWND getExtraLongWnd();
        void setUse360Wnd(HWND);
        HWND getUse360Wnd();
        void setCarSpecificWnd(HWND);
        HWND getCarSpecificWnd();
        void addFfbDevice(GUID dev, const wchar_t *);
        void setFfbDevice(int);
        GUID getFfbDevice();
        void setFfbType(int);
        int getFfbType();
        void setMinForce(int);
        int getMinForce();
        void setMaxForce(int);
        int getMaxForce();    
        float getScaleFactor();
        void setBumpsFactor(int);
        float getBumpsFactor();
        void setLoadFactor(int);
        float getLoadFactor();
        void setYawFactor(int);
        float getYawFactor();
        void setExtraLongLoad(bool);
        bool getExtraLongLoad();
        void setUse360ForDirect(bool);
        bool getUse360ForDirect();
        void setUseCarSpecific(bool, char *);
        bool getUseCarSpecific();
        int getBumpsSetting();
        int getLoadSetting();
        int getMinForceSetting();
        void writeCarSpecificSetting();
        void readRegSettings(bool, char *);
        void writeRegSettings();
        void readSettingsForCar(char *);
        void writeSettingsForCar(char *);

    private:
        HWND devWnd, ffbWnd, minWnd, maxWnd, bumpsWnd, loadWnd, yawWnd;
        HWND extraLongWnd, use360Wnd, carSpecificWnd;
        int ffbType, ffdeviceIdx, minForce, maxForce;
        float scaleFactor, bumpsFactor, loadFactor, yawFactor;
        bool extraLongLoad, use360ForDirect, useCarSpecific;
        GUID devGuid = GUID_NULL, ffdevices[MAX_FFB_DEVICES];

        wchar_t *ffbTypeString(int);
        PWSTR getIniPath();
        void writeWithNewline(std::ofstream &, char *);

};