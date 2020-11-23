#include "settings.h"
#include <fstream>
#include <iostream>
#include <string>

HKEY Settings::getSettingsRegKey() {

    HKEY key;
    if (
        RegCreateKeyExW(
            HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key, nullptr
        )
    )
        return NULL;
    return key;
    
}

LSTATUS Settings::setRegSetting(HKEY key, wchar_t *name, int val) {
    
    DWORD sz = sizeof(int);
    return RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&val, sz);

}

LSTATUS Settings::setRegSetting(HKEY key, wchar_t *name, float val) {

    DWORD sz = sizeof(float);
    return RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&val, sz);

}


LSTATUS Settings::setRegSetting(HKEY key, wchar_t *name, bool val) {

    DWORD sz = sizeof(DWORD);
    DWORD dw = val ? 1 : 0;
    return RegSetValueExW(key, name, 0, REG_DWORD, (BYTE *)&dw, sz);

}

int Settings::getRegSetting(HKEY key, wchar_t *name, int def) {

    int val;
    DWORD sz = sizeof(int);

    if (RegGetValue(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &val, &sz))
        return def;
    else
        return val;

}

float Settings::getRegSetting(HKEY key, wchar_t *name, float def) {

    float val;
    DWORD sz = sizeof(float);

    if (RegGetValue(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &val, &sz))
        return def;
    else
        return val;

}

bool Settings::getRegSetting(HKEY key, wchar_t *name, bool def) {

    DWORD val, sz = sizeof(DWORD);

    if (RegGetValue(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &val, &sz))
        return def;
    else
        return val > 0;

}
    
Settings::Settings() {
    memset(ffdevices, 0, MAX_FFB_DEVICES * sizeof(GUID));
    ffdeviceIdx = 0;
    devGuid = GUID_NULL;
}

void Settings::setDevWnd(HWND wnd) { devWnd = wnd; }

void Settings::setFfbWnd(HWND wnd) { 
    ffbWnd = wnd; 
    for (int i = 0; i < FFBTYPE_UNKNOWN; i++)
        SendMessage(ffbWnd, CB_ADDSTRING, 0, LPARAM(ffbTypeString(i)));
}
        
void Settings::setMinWnd(sWins_t *wnd) {
    minWnd = wnd; 
    SendMessage(minWnd->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
}
        
void Settings::setMaxWnd(sWins_t *wnd) { 
    maxWnd = wnd;
    SendMessage(maxWnd->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(MIN_MAXFORCE, 65));
}

void Settings::setBumpsWnd(sWins_t *wnd) { bumpsWnd = wnd; }
void Settings::setDampingWnd(sWins_t *wnd) { dampingWnd = wnd; }
void Settings::setSopWnd(sWins_t *wnd) { sopWnd = wnd; } 
void Settings::setSopOffsetWnd(sWins_t *wnd) { sopOffsetWnd = wnd; }
void Settings::setUndersteerWnd(sWins_t *wnd) { understeerWnd = wnd; }
void Settings::setUndersteerOffsetWnd(sWins_t *wnd) { understeerOffsetWnd = wnd; }
void Settings::setUse360Wnd(HWND wnd) { use360Wnd = wnd; }
void Settings::setReduceWhenParkedWnd(HWND wnd) { reduceWhenParkedWnd = wnd; }
void Settings::setCarSpecificWnd(HWND wnd) { carSpecificWnd = wnd; }
void Settings::setRunOnStartupWnd(HWND wnd) { runOnStartupWnd = wnd; }
void Settings::setStartMinimisedWnd(HWND wnd) { startMinimisedWnd = wnd; }
void Settings::setDebugWnd(HWND wnd) { debugWnd = wnd; }

void Settings::clearFfbDevices() {
    memset(ffdevices, 0, sizeof(ffdevices));
    ffdeviceIdx = 0;
    SendMessage(devWnd, CB_RESETCONTENT, 0, 0);
}

void Settings::addFfbDevice(GUID dev, const wchar_t *name) {
    
    if (ffdeviceIdx == MAX_FFB_DEVICES)
        return;
    ffdevices[ffdeviceIdx++] = dev;
    SendMessage(devWnd, CB_ADDSTRING, 0, LPARAM(name));
    if (devGuid == dev)
        setFfbDevice(ffdeviceIdx - 1);

}

void Settings::setFfbDevice(int idx) {
    if (idx >= ffdeviceIdx)
        return;
    SendMessage(devWnd, CB_SETCURSEL, idx, 0);
    devGuid = ffdevices[idx];
    initDirectInput();
}

bool Settings::isFfbDevicePresent() {
    for (int i = 0; i < ffdeviceIdx; i++)
        if (ffdevices[i] == devGuid)
            return true;
    return false;
}
        
void Settings::setFfbType(int type) {
    if (type >= FFBTYPE_UNKNOWN)
        return;
    ffbType = type;
    SendMessage(ffbWnd, CB_SETCURSEL, ffbType, 0);
    EnableWindow(
        use360Wnd,
        ffbType == FFBTYPE_DIRECT_FILTER || ffbType == FFBTYPE_DIRECT_FILTER_720
    );    
}

bool Settings::setMinForce(int min, HWND wnd) {
    if (min < 0.0f || min > 20.0f)
        return false;
    minForce = min * MINFORCE_MULTIPLIER;
    if (wnd != minWnd->trackbar)
        SendMessage(minWnd->trackbar, TBM_SETPOS, TRUE, min);
    if (wnd != minWnd->value) {
        swprintf_s(strbuf, L"%d", min);
        SendMessage(minWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    return true;
}
 
bool Settings::setMaxForce(int max, HWND wnd) {
    if (max < MIN_MAXFORCE || max > MAX_MAXFORCE)
        return false;
    maxForce = max;
    if (wnd != maxWnd->trackbar)
        SendMessage(maxWnd->trackbar, TBM_SETPOS, TRUE, maxForce);
    if (wnd != maxWnd->value) {
        swprintf_s(strbuf, L"%d", max);
        SendMessage(maxWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    scaleFactor = (float)DI_MAX / maxForce;
    irsdk_broadcastMsg(
        irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)maxForce
    );
    return true;
}

bool Settings::setBumpsFactor(float factor, HWND wnd) {
    if (factor < 0.0f || factor > 100.0f)
        return false;
    bumpsFactor = pow((float)factor, 2) * BUMPSFORCE_MULTIPLIER;
    if (wnd != bumpsWnd->trackbar)
        SendMessage(bumpsWnd->trackbar, TBM_SETPOS, TRUE, (int)factor);
    if (wnd != bumpsWnd->value) {
        swprintf_s(strbuf, L"%.1f", factor);
        SendMessage(bumpsWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    return true;
}

bool Settings::setDampingFactor(float factor, HWND wnd) {
    if (factor < 0.0f || factor > 100.0f)
        return false;
    dampingFactor = factor;
    if (wnd != dampingWnd->trackbar)
        SendMessage(dampingWnd->trackbar, TBM_SETPOS, TRUE, (int)factor);
    if (wnd != dampingWnd->value) {
        swprintf_s(strbuf, L"%.1f", factor);
        SendMessage(dampingWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    return true;
}

bool Settings::setSopFactor(float factor, HWND wnd) {
    if (factor < 0.0f || factor > 100.0f)
        return false;
    sopFactor = factor;
    if (wnd != sopWnd->trackbar)
        SendMessage(sopWnd->trackbar, TBM_SETPOS, TRUE, (int)factor);
    if (wnd != sopWnd->value) {
        swprintf_s(strbuf, L"%.1f", factor);
        SendMessage(sopWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    EnableWindow(sopOffsetWnd->trackbar, factor != 0);
    return true;
}

bool Settings::setSopOffset(float offset, HWND wnd) {
    if (offset < 0.0f || offset > 100.0f)
        return false;
    sopOffset = offset / 572.958f;
    if (wnd != sopOffsetWnd->trackbar)
        SendMessage(sopOffsetWnd->trackbar, TBM_SETPOS, TRUE, (int)offset);
    if (wnd != sopOffsetWnd->value) {
        swprintf_s(strbuf, L"%.1f", offset);
        SendMessage(sopOffsetWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    return true;
}

bool Settings::setUndersteerFactor(float factor, HWND wnd) {
    if (factor < 0.0f || factor > 100.0f)
        return false;
    understeerFactor = factor;
    if (wnd != understeerWnd->trackbar)
        SendMessage(understeerWnd->trackbar, TBM_SETPOS, TRUE, (int)factor);
    if (wnd != understeerWnd->value) {
        swprintf_s(strbuf, L"%.1f", factor);
        SendMessage(understeerWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    return true;
}

bool Settings::setUndersteerOffset(float offset, HWND wnd) {
    if (offset < 0.0f || offset > 100.0f)
        return false;
    understeerOffset = offset / 250.0f;
    if (wnd != understeerOffsetWnd->trackbar)
        SendMessage(understeerOffsetWnd->trackbar, TBM_SETPOS, TRUE, (int)offset);
    if (wnd != understeerOffsetWnd->value) {
        swprintf_s(strbuf, L"%.1f", offset);
        SendMessage(understeerOffsetWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
    }
    return true;
}

void Settings::setUse360ForDirect(bool set) {
    use360ForDirect = set;
    SendMessage(use360Wnd, BM_SETCHECK, set ? BST_CHECKED : BST_UNCHECKED, NULL);
}

void Settings::setUseCarSpecific(bool set, char *car) {

    if (!useCarSpecific && set) {
        writeGenericSettings();
        if (car && car[0] != 0)
            readSettingsForCar(car);
        setCarStatus(car);
    }
    else if (useCarSpecific && !set) {
        if (car && car[0] != 0)
            writeSettingsForCar(car);
        readGenericSettings();
        setCarStatus(nullptr);
    }

    useCarSpecific = set;
    SendMessage(carSpecificWnd, BM_SETCHECK, set ? BST_CHECKED : BST_UNCHECKED, NULL);
    writeCarSpecificSetting();

}

void Settings::setReduceWhenParked(bool reduce) { 
    reduceWhenParked = reduce; 
    SendMessage(reduceWhenParkedWnd, BM_SETCHECK, reduce ? BST_CHECKED : BST_UNCHECKED, NULL);
}

void Settings::setRunOnStartup(bool run) { 

    HKEY regKey;
    wchar_t module[MAX_PATH];

    runOnStartup = run;
    SendMessage(runOnStartupWnd, BM_SETCHECK, run ? BST_CHECKED : BST_UNCHECKED, NULL);

    DWORD len = GetModuleFileNameW(NULL, module, MAX_PATH);
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_ON_STARTUP_KEY, 0, KEY_ALL_ACCESS, &regKey)) {
        text(L"Failed to open startup registry key");
        return;
    }

    if (run) {
        if (RegSetValueExW(regKey, L"irFFB", 0, REG_SZ, (BYTE *)&module, ++len * sizeof(wchar_t)))
            text(L"Failed to create startup registry value");
    }
    else
        RegDeleteValueW(regKey, L"irFFB");

    RegCloseKey(regKey);

}

void Settings::setStartMinimised(bool minimised) {
    startMinimised = minimised;
    SendMessage(startMinimisedWnd, BM_SETCHECK, minimised ? BST_CHECKED : BST_UNCHECKED, NULL);
}

void Settings::setDebug(bool enabled) {
    debug = enabled;
    SendMessage(debugWnd, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, NULL);
}

float Settings::getBumpsSetting() {
    return sqrt(bumpsFactor / BUMPSFORCE_MULTIPLIER);
}

int Settings::getMinForceSetting() {
    return minForce / MINFORCE_MULTIPLIER;
}

float Settings::getSopOffsetSetting() {
    return sopOffset * 572.958f;
}

float Settings::getUndersteerOffsetSetting() {
    return understeerOffset * 250.0f;
}

void Settings::writeCarSpecificSetting() { 
    HKEY key = getSettingsRegKey();
    if (key == NULL)
        return;
    setRegSetting(key, L"useCarSpecific", useCarSpecific);
    RegCloseKey(key);
}

void Settings::readRegSettings(char *car) {

    wchar_t dguid[GUIDSTRING_MAX];
    DWORD dgsz = sizeof(dguid);
    HKEY key = getSettingsRegKey();

    if (key == NULL) {
        setReduceWhenParked(true);
        setStartMinimised(false);
        setRunOnStartup(false);
        setUseCarSpecific(false, car);
        return;
    }

        
    if (!RegGetValue(key, nullptr, L"device", RRF_RT_REG_SZ, nullptr, dguid, &dgsz))
        if (FAILED(IIDFromString(dguid, &devGuid)))
            devGuid = GUID_NULL;

    setReduceWhenParked(getRegSetting(key, L"reduceWhenParked", true));
    setRunOnStartup(getRegSetting(key, L"runOnStartup", false));
    setStartMinimised(getRegSetting(key, L"startMinimised", false));
    setUseCarSpecific(getRegSetting(key, L"useCarSpecific", false), car);

    RegCloseKey(key);
    
}

void Settings::readGenericSettings() {

    wchar_t dguid[GUIDSTRING_MAX];
    DWORD dgsz = sizeof(dguid);
    HKEY key = getSettingsRegKey();

    if (key == NULL) {
        setFfbType(FFBTYPE_DIRECT_FILTER);
        setMinForce(0, (HWND)-1);
        setMaxForce(45, (HWND)-1);
        setBumpsFactor(0.0f, (HWND)-1);
        setDampingFactor(0.0f, (HWND)-1);
        setSopFactor(0.0f, (HWND)-1);
        setSopOffset(0.0f, (HWND)-1);
        setUndersteerFactor(0.0f, (HWND)-1);
        setUndersteerOffset(0.0f, (HWND)-1);
        setUse360ForDirect(true);
        return;
    }

    setFfbType(getRegSetting(key, L"ffb", FFBTYPE_DIRECT_FILTER));
    setMaxForce(getRegSetting(key, L"maxForce", 45), (HWND)-1);
    setMinForce(getRegSetting(key, L"minForce", 0), (HWND)-1);
    setBumpsFactor(getRegSetting(key, L"bumpsFactor", 0.0f), (HWND)-1);
    setDampingFactor(getRegSetting(key, L"dampingFactor", 0.0f), (HWND)-1);
    setSopFactor(getRegSetting(key, L"yawFactor", 0.0f), (HWND)-1);
    setSopOffset(getRegSetting(key, L"yawOffset", 0.0f), (HWND)-1);
    setUndersteerFactor(getRegSetting(key, L"understeerFactor", 0.0f), (HWND)-1);
    setUndersteerOffset(getRegSetting(key, L"understeerOffset", 0.0f), (HWND)-1);
    setUse360ForDirect(getRegSetting(key, L"use360ForDirect", true));

    RegCloseKey(key);

}

void Settings::writeRegSettings() {

    wchar_t *guid;
    int len;
    HKEY key = getSettingsRegKey();

    if (key == NULL)
        return;

    if (SUCCEEDED(StringFromCLSID(devGuid, (LPOLESTR *)&guid))) {
        len = (lstrlenW(guid) + 1) * sizeof(wchar_t);
        RegSetValueEx(key, L"device", 0, REG_SZ, (BYTE *)guid, len);
    }
    
    setRegSetting(key, L"reduceWhenParked", getReduceWhenParked());
    setRegSetting(key, L"runOnStartup", getRunOnStartup());
    setRegSetting(key, L"startMinimised", getStartMinimised());

    RegCloseKey(key);

}

void Settings::writeGenericSettings() {

    HKEY key = getSettingsRegKey();

    if (key == NULL)
        return;

    setRegSetting(key, L"ffb", ffbType);
    setRegSetting(key, L"bumpsFactor", getBumpsSetting());
    setRegSetting(key, L"dampingFactor", dampingFactor);
    setRegSetting(key, L"yawFactor", sopFactor);
    setRegSetting(key, L"yawOffset", getSopOffsetSetting());
    setRegSetting(key, L"maxForce", maxForce);
    setRegSetting(key, L"minForce", getMinForceSetting());
    setRegSetting(key, L"use360ForDirect", use360ForDirect);
	setRegSetting(key, L"understeerFactor", understeerFactor);
	setRegSetting(key, L"understeerOffset", understeerOffset);

    RegCloseKey(key);

}

void Settings::readSettingsForCar(char *car) {

    PWSTR path = getIniPath();

    if (path == nullptr) {
        text(L"Failed to locate documents folder, can't read ini");
        return;
    }

    std::ifstream iniFile(path);
    std::string line;

    char carName[MAX_CAR_NAME];
    int type = 2, min = 0, max = 45, longLoad = 1, use360 = 1;
    float bumps = 0.0f, damping = 0.0f, yaw = 0.0f, yawOffset = 0.0f;
    float understeer = 0.0f, understeerOffset = 0.0f;

    memset(carName, 0, sizeof(carName));

    while (std::getline(iniFile, line)) {
        if (
            sscanf_s(
                line.c_str(), INI_SCAN_FORMAT,
                carName, sizeof(carName),
                &type, &min, &max, &bumps, &damping, &longLoad, &use360, &yaw,
                &yawOffset, &understeer, &understeerOffset
            ) < 8
        )
            continue;
        if (strcmp(carName, car) == 0)
            break;
    }

    if (strcmp(carName, car) != 0)
        goto DONE;

    text(L"Loading settings for car %s", car);

    setFfbType(type);
    setMinForce(min, (HWND)-1);
    setMaxForce(max, (HWND)-1);
    setBumpsFactor(bumps, (HWND)-1);
    setDampingFactor(damping, (HWND)-1);
    setSopFactor(yaw, (HWND)-1);
    setSopOffset(yawOffset, (HWND)-1);
    setUndersteerFactor(understeer, (HWND)-1);
    setUndersteerOffset(understeerOffset, (HWND)-1);
    setUse360ForDirect(use360 > 0);

DONE:
    iniFile.close();
    delete[] path;

}

void Settings::writeSettingsForCar(char *car) {

    PWSTR path = getIniPath();

    if (path == nullptr) {
        text(L"Failed to locate documents folder, can't write ini");
        return;
    }

    PWSTR tmpPath = new wchar_t[lstrlen(path) + 5];
    lstrcpy(tmpPath, path);
    lstrcat(tmpPath, L".tmp");

    std::ifstream iniFile(path);
    std::ofstream tmpFile(tmpPath);
    std::string line;

    char carName[MAX_CAR_NAME], buf[256];
    int type = 2, min = 0, max = 45, longLoad = 1, use360 = 1;
    float bumps = 0.0f, damping = 0.0f, yaw = 0.0f, yawOffset = 0.0f;
    float understeer = 0.0f, understeerOffset = 0.0f;
    bool written = false, iniPresent = iniFile.good();

    text(L"Writing settings for car %s", car);

    memset(carName, 0, sizeof(carName));

    while (std::getline(iniFile, line)) {

        if (line.length() > 255)
            continue;
        if (
            sscanf_s(
                line.c_str(), INI_SCAN_FORMAT,
                carName, sizeof(carName),
                &type, &min, &max, &bumps, &damping, &longLoad, &use360,
                &yaw, &yawOffset, &understeer, &understeerOffset
            ) < 8
        ) {
            strcpy_s(buf, line.c_str());
            writeWithNewline(tmpFile, buf);
            continue;
        }
        if (strcmp(carName, car) != 0) {
            strcpy_s(buf, line.c_str());
            writeWithNewline(tmpFile, buf);
            continue;
        }
        sprintf_s(
            buf, INI_PRINT_FORMAT,
            car, ffbType, getMinForceSetting(), maxForce, getBumpsSetting(),
            dampingFactor, 1, use360ForDirect, sopFactor, getSopOffsetSetting(),
            understeerFactor, getUndersteerOffsetSetting()
        );
        writeWithNewline(tmpFile, buf);
        written = true;
    }

    if (written)
        goto MOVE;

    if (!iniPresent) {
        sprintf_s(buf, "car:ffbType:minForce:maxForce:bumps:damping:notUsed:effUse360:sop:sopOffset:understeer:understeerOffset\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "ffbType          | 0 = 360, 1 = 360I, 2 = 60DF_360, 3 = 60DF_720\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "minForce         | min = 0, max = 20\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "maxForce         | min = %d, max = %d\r\n", MIN_MAXFORCE, MAX_MAXFORCE);
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "bumps            | min = 0, max = 100\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "damping          | min = 0, max = 100\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "notUsed          | N/A\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "effUse360        | off = 0, on = 1\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "sop              | min = 0, max = 100\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "sopOffset        | min = 0, max = 100\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "understeer       | min = 0, max = 100\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "understeerOffset | min = 0, max = 100\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
    }

    sprintf_s(
        buf, INI_PRINT_FORMAT,
        car, ffbType, getMinForceSetting(), maxForce, getBumpsSetting(),
        dampingFactor, 1, use360ForDirect, sopFactor, getSopOffsetSetting(),
        understeerFactor, getUndersteerOffsetSetting()
    );
    writeWithNewline(tmpFile, buf);

MOVE:
    iniFile.close();
    tmpFile.close();

    if (!MoveFileEx(tmpPath, path, MOVEFILE_REPLACE_EXISTING))
        text(L"Failed to update ini file, error %d", GetLastError());

    delete[] path;
    delete[] tmpPath;

}

wchar_t *Settings::ffbTypeString(int type) {
    switch (type) {
        case FFBTYPE_360HZ:             return L"360 Hz";
        case FFBTYPE_360HZ_INTERP:      return L"360 Hz interpolated";
        case FFBTYPE_DIRECT_FILTER:     return L"60 Hz direct filtered 360";
        case FFBTYPE_DIRECT_FILTER_720: return L"60 Hz direct filtered 720";
        default:                        return L"Unknown FFB type";
    }
}

PWSTR Settings::getIniPath() {

    PWSTR docsPath;
    wchar_t *path;

    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &docsPath) != S_OK)
        return nullptr;

    path = new wchar_t[lstrlen(docsPath) + lstrlen(INI_PATH) + 1];

    lstrcpyW(path, docsPath);
    lstrcatW(path, INI_PATH);
    CoTaskMemFree(docsPath);

    return path;

}

PWSTR Settings::getLogPath() {

    PWSTR docsPath;
    wchar_t buf[64];
    wchar_t *path;
    SYSTEMTIME lt;

    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &docsPath) != S_OK)
        return nullptr;

    GetLocalTime(&lt);
    
    lstrcpyW(buf, L"\\irFFB-");
    int len = wcslen(buf) * sizeof(wchar_t);
    StringCbPrintf(
        buf + wcslen(buf), sizeof(buf) - len, L"%d-%02d-%02d-%02d-%02d-%02d.log",
        lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond
    );

    path = new wchar_t[lstrlen(docsPath) + lstrlen(buf) + 1];

    lstrcpyW(path, docsPath);
    lstrcatW(path, buf);
    CoTaskMemFree(docsPath);

    return path;

}

void Settings::writeWithNewline(std::ofstream &file, char *buf) {
    int len = strlen(buf);
    buf[len] = '\n';
    file.write(buf, len + 1);
}