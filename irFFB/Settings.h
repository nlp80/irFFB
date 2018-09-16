#pragma once
#include "stdafx.h"
#include "irFFB.h"

class Settings {

    public:

        static HKEY getSettingsRegKey();
        static LSTATUS setRegSetting(HKEY, wchar_t *, int);
        static LSTATUS setRegSetting(HKEY, wchar_t *, float);
        static LSTATUS setRegSetting(HKEY, wchar_t *, bool);
        static int getRegSetting(HKEY, wchar_t *, int);
        static float getRegSetting(HKEY, wchar_t *, float);
        static bool getRegSetting(HKEY, wchar_t *, bool);

        Settings();
        void setDevWnd(HWND);
        HWND getDevWnd() { return devWnd; };
        void setFfbWnd(HWND);
        HWND getFfbWnd() { return ffbWnd; };
        void setMinWnd(sWins_t *);
        sWins_t *getMinWnd() { return minWnd; };
        void setMaxWnd(sWins_t *);
        sWins_t *getMaxWnd() { return maxWnd; };
        void setBumpsWnd(sWins_t *);
        sWins_t *getBumpsWnd() { return bumpsWnd; };
        void setDampingWnd(sWins_t *);
        sWins_t *getDampingWnd() { return dampingWnd; };
        void setSopWnd(sWins_t *);
        sWins_t *getSopWnd() { return sopWnd; };
        void setSopOffsetWnd(sWins_t *);
        sWins_t *getSopOffsetWnd() { return sopOffsetWnd; };
        void setUndersteerWnd(sWins_t *);
        sWins_t *getUndersteerWnd() { return understeerWnd; };
        void setUndersteerOffsetWnd(sWins_t *);
        sWins_t *getUndersteerOffsetWnd() { return understeerOffsetWnd; };
        void setUse360Wnd(HWND);
        HWND getUse360Wnd() { return use360Wnd; };
        void setCarSpecificWnd(HWND);
        HWND getCarSpecificWnd() { return carSpecificWnd; };
        void setReduceWhenParkedWnd(HWND);
        HWND getReduceWhenParkedWnd() { return reduceWhenParkedWnd; };
        void setRunOnStartupWnd(HWND);
        HWND getRunOnStartupWnd() { return runOnStartupWnd; };
        void setStartMinimisedWnd(HWND);
        HWND getStartMinimisedWnd() { return startMinimisedWnd; };
        void setDebugWnd(HWND);
        HWND getDebugWnd() { return debugWnd; };

        void clearFfbDevices();
        void addFfbDevice(GUID dev, const wchar_t *);
        void setFfbDevice(int);
        bool isFfbDevicePresent();
        GUID getFfbDevice() { return devGuid; };
        void setFfbType(int);
        int getFfbType() { return ffbType; };
        bool setMinForce(int, HWND);
        int getMinForce() { return minForce; };
        bool setMaxForce(int, HWND);
        int getMaxForce() { return maxForce; };
        float getScaleFactor() { return scaleFactor; };
        bool setBumpsFactor(float, HWND);
        float getBumpsFactor() { return bumpsFactor; };
        bool setDampingFactor(float, HWND);
        float getDampingFactor() { return dampingFactor; };
        bool setSopFactor(float, HWND);
        float getSopFactor() { return sopFactor; };
        bool setSopOffset(float, HWND);
        float getSopOffset() { return sopOffset; };
        bool setUndersteerFactor(float, HWND);
        float getUndersteerFactor() { return understeerFactor; };
        bool setUndersteerOffset(float, HWND);
        float getUndersteerOffset() { return understeerOffset; };
        void setUse360ForDirect(bool);
        bool getUse360ForDirect() { return use360ForDirect; };
        void setUseCarSpecific(bool, char *);
        bool getUseCarSpecific() { return useCarSpecific; };
        void setReduceWhenParked(bool);
        bool getReduceWhenParked() { return reduceWhenParked; };
        void setRunOnStartup(bool);
        bool getRunOnStartup() { return runOnStartup; };
        void setStartMinimised(bool);
        bool getStartMinimised() { return startMinimised; };
        void setDebug(bool);
        bool getDebug() { return debug; };
        float getBumpsSetting();
        int getMinForceSetting();
        float getSopOffsetSetting();
        float getUndersteerOffsetSetting();
        void writeCarSpecificSetting();
        void readRegSettings(char *);
        void readGenericSettings();
        void writeRegSettings();
        void writeGenericSettings();
        void readSettingsForCar(char *);
        void writeSettingsForCar(char *);
        PWSTR getLogPath();

    private:
        HWND devWnd, ffbWnd;
        sWins_t *minWnd, *maxWnd, *bumpsWnd, *dampingWnd, *sopWnd, *sopOffsetWnd, *understeerWnd, *understeerOffsetWnd;
        HWND use360Wnd, carSpecificWnd, reduceWhenParkedWnd;
        HWND runOnStartupWnd, startMinimisedWnd, debugWnd;
        int ffbType, ffdeviceIdx, minForce, maxForce;
        float scaleFactor, bumpsFactor, dampingFactor, sopFactor, sopOffset, understeerFactor, understeerOffset;
        bool use360ForDirect, useCarSpecific, debug;
        bool reduceWhenParked, runOnStartup, startMinimised;
        GUID devGuid = GUID_NULL, ffdevices[MAX_FFB_DEVICES];
        wchar_t strbuf[64];
        HANDLE debugHnd;

        wchar_t *ffbTypeString(int);
        PWSTR getIniPath();
        void writeWithNewline(std::ofstream &, char *);

};