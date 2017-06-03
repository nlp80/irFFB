#pragma once
#include <ShlObj.h>
#include <Shlwapi.h>
#include <comdef.h>
#include <initguid.h>

#include "stdafx.h"
#include "irFFB.h"
#include "IUberwoorf.h"


#define MAX_JETSEAT_EFFECTS 16
#define GW_LEG_LEFT 1
#define GW_LEG_RIGHT 2
#define GW_LEG_BOTH 3
#define GW_SLIDE_LEFT 4
#define GW_SLIDE_RIGHT 5
#define GW_ALL_LEFT 6
#define GW_ALL_RIGHT 7
#define GW_ALL_BOTH 8
#define GW_BACK_LEFT 9
#define GW_BACK_RIGHT 10
#define GW_BACK_BOTH 11

#define LEGS 0
#define BACK 1
#define ALL  2

class JetSeat {

    public:
        static JetSeat *init();
        bool isEnabled();
        void createWindow(HINSTANCE);
        void startEngineEffect();
        void stopEngineEffect();
        void updateEngineEffect(float);
        void gearEffect();
        void fBumpEffect(float, float);
        void rBumpEffect(float, float);
        void yawEffect(float);
        
    private:
        JetSeat();
        ~JetSeat();
        bool initialise();
        bool open();
        bool initEffects();
        ATOM registerClass(HINSTANCE);
        void effectControls(wchar_t *, int, int, HWND *, sWins_t *, HINSTANCE);
        static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
        void setGearPlace(int);
        int  getGearPlace();
        void setEnginePlace(int);
        int  getEnginePlace();
        void setGearGain(float);
        void setEngineGain(float);
        void setBumpsGain(float);
        void setYawGain(float);
        void setEnabled(bool);
        int  effectLocation(int);
        int  effectSetting(int);
        int  createEffect(UINT);
        int  createSlideEffect(UINT);
        void createEngineEffect();
        void startEffect(int, float, int);
        void stopEffect(int);
        void readSettings();
        void writeSettings();
       
        static JetSeat *instance;

        wchar_t *windowClass = L"JetSeat Configuration";

        HWND mainWnd, gearPlaceWnd, enginePlaceWnd, enableWnd;
        sWins_t gearGainWnd, bumpsGainWnd, engineGainWnd, yawGainWnd;

        UINT effect_handles[MAX_JETSEAT_EFFECTS];
        wchar_t *effectPlaces[3] = { L"Legs", L"Back", L"All" };
        IUberwoorf *jetseat = nullptr;
        UINT engineEffect, engineVibL1, engineVibL2, engineVibR1, engineVibR2;
        UINT engineLeft = UW_BACK_LOW_LEFT, engineRight = UW_BACK_LOW_RIGHT;
        bool enabled = true, engineOn = false, classIsRegistered = false;

        int gearPlace, enginePlace, engineCounter = 0;
        float gearGain = 100, bumpsGain = 100, yawGain = 100, engineGain = 0;
        wchar_t strbuf[64];

};