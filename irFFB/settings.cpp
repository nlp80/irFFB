#include "settings.h"
#include <fstream>
#include <iostream>
#include <string>
    
Settings::Settings() {}

void Settings::setDevWnd(HWND wnd) { devWnd = wnd; }
HWND Settings::getDevWnd() { return devWnd; }

void Settings::setFfbWnd(HWND wnd) { 
    ffbWnd = wnd; 
    for (int i = 0; i < FFBTYPE_UNKNOWN; i++)
        SendMessage(ffbWnd, CB_ADDSTRING, 0, LPARAM(ffbTypeString(i)));
}
HWND Settings::getFfbWnd() { return ffbWnd; }
        
void Settings::setMinWnd(HWND wnd) {
    minWnd = wnd; 
    SendMessage(minWnd, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
}
HWND Settings::getMinWnd() { return minWnd; }
        
void Settings::setMaxWnd(HWND wnd) { 
    maxWnd = wnd;
    SendMessage(maxWnd, TBM_SETRANGE, TRUE, MAKELPARAM(MIN_MAXFORCE, MAX_MAXFORCE));
}
HWND Settings::getMaxWnd() { return maxWnd; }

void Settings::setBumpsWnd(HWND wnd) { bumpsWnd = wnd; }
HWND Settings::getBumpsWnd() { return bumpsWnd; }

void Settings::setLoadWnd(HWND wnd) { loadWnd = wnd; }
HWND Settings::getLoadWnd() { return loadWnd; }
        
void Settings::setExtraLongWnd(HWND wnd) { extraLongWnd = wnd; }
HWND Settings::getExtraLongWnd() { return extraLongWnd; }
void Settings::setUse360Wnd(HWND wnd) { use360Wnd = wnd; }
HWND Settings::getUse360Wnd() { return use360Wnd; }

void Settings::setCarSpecificWnd(HWND wnd) { carSpecificWnd = wnd; }
HWND Settings::getCarSpecificWnd() { return carSpecificWnd; }

void Settings::addFfbDevice(GUID dev, const wchar_t *name) {
    
    if (ffdeviceIdx == MAX_FFB_DEVICES)
        return;
    ffdevices[ffdeviceIdx++] = dev;
    SendMessage((HWND)devWnd, CB_ADDSTRING, 0, LPARAM(name));
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
        
GUID Settings::getFfbDevice() {
    return devGuid;
}

void Settings::setFfbType(int type) {
    if (ffbType >= FFBTYPE_UNKNOWN)
        return;
    ffbType = type;
    SendMessage(ffbWnd, CB_SETCURSEL, ffbType, 0);
    if (ffbType != FFBTYPE_DIRECT_FILTER)
        EnableWindow(use360Wnd, false);
}
int Settings::getFfbType() { return ffbType; }

void Settings::setMinForce(int min) {
    if (min < 0 || min > 20)
        return;
    minForce = min * MINFORCE_MULTIPLIER;
    SendMessage(minWnd, TBM_SETPOS, TRUE, min);
    SendMessage(minWnd, TBM_SETPOSNOTIFY, 0, min);
}
int Settings::getMinForce() { return minForce; }
 
void Settings::setMaxForce(int max) {
    if (max < MIN_MAXFORCE || max > MAX_MAXFORCE)
        return;
    maxForce = max;
    SendMessage(maxWnd, TBM_SETPOS, TRUE, maxForce);
    SendMessage(maxWnd, TBM_SETPOSNOTIFY, 0, maxForce);
    scaleFactor = (float)DI_MAX / maxForce;
    irsdk_broadcastMsg(
        irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)maxForce
    );
}
int Settings::getMaxForce() { return maxForce; }

float Settings::getScaleFactor() { return scaleFactor; }

void Settings::setBumpsFactor(int factor) {
    if (factor < 0 || factor > 100)
        return;
    bumpsFactor = pow((float)factor, 2) * BUMPSFORCE_MULTIPLIER;
    SendMessage(bumpsWnd, TBM_SETPOS, TRUE, factor);
    SendMessage(bumpsWnd, TBM_SETPOSNOTIFY, 0, factor);
}
float Settings::getBumpsFactor() { return bumpsFactor; }

void Settings::setLoadFactor(int factor) {
    if (factor < 0 || factor > 100)
        return;
    loadFactor = pow((float)factor, 2) * LOADFORCE_MULTIPLIER;
    SendMessage(loadWnd, TBM_SETPOS, TRUE, factor);
    SendMessage(loadWnd, TBM_SETPOSNOTIFY, 0, factor);
}
float Settings::getLoadFactor() { return loadFactor; }

void Settings::setExtraLongLoad(bool set) {
    extraLongLoad = set;
    SendMessage(extraLongWnd, BM_SETCHECK, set ? BST_CHECKED : BST_UNCHECKED, NULL);
}
bool Settings::getExtraLongLoad() { return extraLongLoad; }

void Settings::setUse360ForDirect(bool set) {
    use360ForDirect = set;
    SendMessage(use360Wnd, BM_SETCHECK, set ? BST_CHECKED : BST_UNCHECKED, NULL);
}
bool Settings::getUse360ForDirect() { return use360ForDirect; }

void Settings::setUseCarSpecific(bool set, char *car) {

    if (!useCarSpecific && set) {
        writeRegSettings();
        if (car && car[0] != 0)
            readSettingsForCar(car);
        setCarStatus(car);
    }
    else if (useCarSpecific && !set) {
        if (car && car[0] != 0)
            writeSettingsForCar(car);
        readRegSettings(false, car);
        setCarStatus(nullptr);
    }

    useCarSpecific = set;
    SendMessage(carSpecificWnd, BM_SETCHECK, set ? BST_CHECKED : BST_UNCHECKED, NULL);
    writeCarSpecificSetting();

}
bool Settings::getUseCarSpecific() { return useCarSpecific; }

int Settings::getBumpsSetting() {
    return (int)(sqrt(bumpsFactor / BUMPSFORCE_MULTIPLIER));
}

int Settings::getLoadSetting() {
    return (int)(sqrt(loadFactor / LOADFORCE_MULTIPLIER));
}

int Settings::getMinForceSetting() {
    return minForce / MINFORCE_MULTIPLIER;
}

void Settings::writeCarSpecificSetting() {

    HKEY regKey;
    DWORD sz = sizeof(DWORD);

    RegCreateKeyEx(
        HKEY_CURRENT_USER, KEY_PATH, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PATH, 0, KEY_ALL_ACCESS, &regKey))
        RegSetValueEx(regKey, L"useCarSpecific", 0, REG_DWORD, (BYTE *)&useCarSpecific, sz);

}

void Settings::readRegSettings(bool readCarSpecific, char *car) {

    wchar_t dguid[GUIDSTRING_MAX];
    HKEY regKey;
    DWORD val;
    DWORD sz = sizeof(val);
    DWORD dgsz = sizeof(dguid);

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PATH, 0, KEY_ALL_ACCESS, &regKey)) {

        if (!RegGetValue(regKey, nullptr, L"device", RRF_RT_REG_SZ, nullptr, dguid, &dgsz))
            if (FAILED(IIDFromString(dguid, &devGuid)))
                devGuid = GUID_NULL;
        if (RegGetValue(regKey, nullptr, L"ffb", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setFfbType(FFBTYPE_DIRECT_FILTER);
        else
            setFfbType(val);
        if (RegGetValue(regKey, nullptr, L"maxForce", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setMaxForce(45);
        else
            setMaxForce(val);
        if (RegGetValue(regKey, nullptr, L"minForce", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setMinForce(0);
        else
            setMinForce(val);
        if (RegGetValue(regKey, nullptr, L"bumpsFactor", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setBumpsFactor(0);
        else
            setBumpsFactor(val);
        if (RegGetValue(regKey, nullptr, L"loadFactor", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setLoadFactor(0);
        else
            setLoadFactor(val);
        if (RegGetValue(regKey, nullptr, L"use360ForDirect", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setUse360ForDirect(true);
        else
            setUse360ForDirect(val > 0);
        if (RegGetValue(regKey, nullptr, L"extraLongLoad", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setExtraLongLoad(false);
        else
            setExtraLongLoad(val > 0);
        if (readCarSpecific) {
            if (RegGetValue(regKey, nullptr, L"useCarSpecific", RRF_RT_REG_DWORD, nullptr, &val, &sz))
                setUseCarSpecific(false, car);
            else
                setUseCarSpecific(val > 0, car);
        }

    }
    else {
        setFfbType(FFBTYPE_DIRECT_FILTER);
        setMinForce(0);
        setMaxForce(45);
        setBumpsFactor(0);
        setLoadFactor(0);
        setUse360ForDirect(true);
        setExtraLongLoad(false);
        setUseCarSpecific(false, car);
    }

}

void Settings::writeRegSettings() {

    wchar_t *guid;
    HKEY regKey;
    DWORD sz = sizeof(int);
    DWORD bumps = getBumpsSetting();
    DWORD load = getLoadSetting();
    DWORD min = getMinForceSetting();
    DWORD use360 = getUse360ForDirect();
    DWORD extraLong = getExtraLongLoad();

    RegCreateKeyEx(
        HKEY_CURRENT_USER, KEY_PATH, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, KEY_PATH, 0, KEY_ALL_ACCESS, &regKey)) {

        if (SUCCEEDED(StringFromCLSID(devGuid, (LPOLESTR *)&guid))) {
            int len = (lstrlenW(guid) + 1) * sizeof(wchar_t);
            RegSetValueEx(regKey, L"device", 0, REG_SZ, (BYTE *)guid, len);
        }
        RegSetValueEx(regKey, L"ffb", 0, REG_DWORD, (BYTE *)&ffbType, sz);
        RegSetValueEx(regKey, L"bumpsFactor", 0, REG_DWORD, (BYTE *)&bumps, sz);
        RegSetValueEx(regKey, L"loadFactor", 0, REG_DWORD, (BYTE *)&load, sz);
        RegSetValueEx(regKey, L"maxForce", 0, REG_DWORD, (BYTE *)&maxForce, sz);
        RegSetValueEx(regKey, L"minForce", 0, REG_DWORD, (BYTE *)&min, sz);
        RegSetValueEx(regKey, L"use360ForDirect", 0, REG_DWORD, (BYTE *)&use360, sz);
        RegSetValueEx(regKey, L"extraLongLoad", 0, REG_DWORD, (BYTE *)&extraLong, sz);

    }

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
    int type, min, max, bumps, load, extraLong, use360;

    memset(carName, 0, sizeof(carName));

    while (std::getline(iniFile, line)) {
        if (
            sscanf_s(
                line.c_str(), INI_SCAN_FORMAT,
                carName, sizeof(carName),
                &type, &min, &max, &bumps, &load, &extraLong, &use360
            ) != 8
        )
            continue;
        if (strcmp(carName, car) == 0)
            break;
    }

    if (strcmp(carName, car) != 0)
        goto DONE;

    text(L"Loading settings for car %s", car);

    setFfbType(type);
    setMinForce(min);
    setMaxForce(max);
    setBumpsFactor(bumps);
    setLoadFactor(load);
    setExtraLongLoad(extraLong > 0);
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
    int type, min, max, bumps, load, extraLong, use360;
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
                &type, &min, &max, &bumps, &load, &extraLong, &use360
                ) != 8
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
            getLoadSetting(), extraLongLoad, use360ForDirect
        );
        writeWithNewline(tmpFile, buf);
        written = true;
    }

    if (written)
        goto MOVE;

    if (!iniPresent) {
        sprintf_s(buf, "car:ffbType:minForce:maxForce:bumps:load:incrLongEff:effUse360\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "ffbType     | 0 = 360, 1 = 360I, 2 = 60DF\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "minForce    | min = 0, max = 20\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "maxForce    | min = %d, max = %d\r\n", MIN_MAXFORCE, MAX_MAXFORCE);
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "bumps       | min = 0, max = 100\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "load        | min = 0, max = 100\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "incrLongEff | off = 0, on = 1\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "effUse360   | off = 0, on = 1\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
    }

    sprintf_s(
        buf, INI_PRINT_FORMAT,
        car, ffbType, getMinForceSetting(), maxForce, getBumpsSetting(),
        getLoadSetting(), extraLongLoad, use360ForDirect
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
        case FFBTYPE_360HZ:         return L"360 Hz";
        case FFBTYPE_360HZ_INTERP:  return L"360 Hz interpolated";
        case FFBTYPE_DIRECT_FILTER: return L"60 Hz direct filtered";
        default:                    return L"Unknown FFB type";
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

void Settings::writeWithNewline(std::ofstream &file, char *buf) {
    int len = strlen(buf);
    buf[len] = '\n';
    file.write(buf, len + 1);
}
