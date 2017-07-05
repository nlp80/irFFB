#include "settings.h"
#include <fstream>
#include <iostream>
#include <string>
    
Settings::Settings() {
    memset(ffdevices, 0, MAX_FFB_DEVICES * sizeof(GUID));
    ffdeviceIdx = 0;
    devGuid = GUID_NULL;
}

void Settings::setDevWnd(HWND wnd) { devWnd = wnd; }
HWND Settings::getDevWnd() { return devWnd; }

void Settings::setFfbWnd(HWND wnd) { 
    ffbWnd = wnd; 
    for (int i = 0; i < FFBTYPE_UNKNOWN; i++)
        SendMessage(ffbWnd, CB_ADDSTRING, 0, LPARAM(ffbTypeString(i)));
}
HWND Settings::getFfbWnd() { return ffbWnd; }
        
void Settings::setMinWnd(sWins_t *wnd) {
    minWnd = wnd; 
    SendMessage(minWnd->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
}
sWins_t *Settings::getMinWnd() { return minWnd; }
        
void Settings::setMaxWnd(sWins_t *wnd) { 
    maxWnd = wnd;
    SendMessage(maxWnd->trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(MIN_MAXFORCE, MAX_MAXFORCE));
}
sWins_t *Settings::getMaxWnd() { return maxWnd; }

void Settings::setBumpsWnd(sWins_t *wnd) { bumpsWnd = wnd; }
sWins_t *Settings::getBumpsWnd() { return bumpsWnd; }

void Settings::setLoadWnd(sWins_t *wnd) { loadWnd = wnd; }
sWins_t *Settings::getLoadWnd() { return loadWnd; }

void Settings::setYawWnd(sWins_t *wnd) { yawWnd = wnd; }
sWins_t *Settings::getYawWnd() { return yawWnd; }
        
void Settings::setExtraLongWnd(HWND wnd) { extraLongWnd = wnd; }
HWND Settings::getExtraLongWnd() { return extraLongWnd; }
void Settings::setUse360Wnd(HWND wnd) { use360Wnd = wnd; }
HWND Settings::getUse360Wnd() { return use360Wnd; }
void Settings::setReduceWhenParkedWnd(HWND wnd) { reduceWhenParkedWnd = wnd; }
HWND Settings::getReduceWhenParkedWnd() { return reduceWhenParkedWnd; }
void Settings::setCarSpecificWnd(HWND wnd) { carSpecificWnd = wnd; }
HWND Settings::getCarSpecificWnd() { return carSpecificWnd; }
void Settings::setRunOnStartupWnd(HWND wnd) { runOnStartupWnd = wnd; }
HWND Settings::getRunOnStartupWnd() { return runOnStartupWnd; }
void Settings::setStartMinimisedWnd(HWND wnd) { startMinimisedWnd = wnd; }
HWND Settings::getStartMinimisedWnd() { return startMinimisedWnd; }

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
        
GUID Settings::getFfbDevice() {
    return devGuid;
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
int Settings::getFfbType() { return ffbType; }

void Settings::setMinForce(int min) {
    if (min < 0 || min > 20)
        return;
    minForce = min * MINFORCE_MULTIPLIER;
    SendMessage(minWnd->trackbar, TBM_SETPOS, TRUE, min);
    swprintf_s(strbuf, L"Min force  [ %d ]", min);
    SendMessage(minWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
int Settings::getMinForce() { return minForce; }
 
void Settings::setMaxForce(int max) {
    if (max < MIN_MAXFORCE || max > MAX_MAXFORCE)
        return;
    maxForce = max;
    SendMessage(maxWnd->trackbar, TBM_SETPOS, TRUE, maxForce);
    swprintf_s(strbuf, L"Max force  [ %d Nm ]", max);
    SendMessage(maxWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
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
    SendMessage(bumpsWnd->trackbar, TBM_SETPOS, TRUE, factor);
    swprintf_s(strbuf, L"Suspension bumps  [ %d ]", factor);
    SendMessage(bumpsWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
float Settings::getBumpsFactor() { return bumpsFactor; }

void Settings::setLoadFactor(int factor) {
    if (factor < 0 || factor > 100)
        return;
    loadFactor = pow((float)factor, 2) * LOADFORCE_MULTIPLIER;
    SendMessage(loadWnd->trackbar, TBM_SETPOS, TRUE, factor);
    swprintf_s(strbuf, L"Suspension load  [ %d ]", factor);
    SendMessage(loadWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
float Settings::getLoadFactor() { return loadFactor; }

void Settings::setYawFactor(int factor) {
    if (factor < 0 || factor > 100)
        return;
    yawFactor = (float)factor;
    SendMessage(yawWnd->trackbar, TBM_SETPOS, TRUE, factor);
    swprintf_s(strbuf, L"SoP  [ %d ]", factor);
    SendMessage(yawWnd->value, WM_SETTEXT, NULL, LPARAM(strbuf));
}
float Settings::getYawFactor() { return yawFactor; }

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
bool Settings::getUseCarSpecific() { return useCarSpecific; }

void Settings::setReduceWhenParked(bool reduce) { 
    reduceWhenParked = reduce; 
    SendMessage(reduceWhenParkedWnd, BM_SETCHECK, reduce ? BST_CHECKED : BST_UNCHECKED, NULL);
}
bool Settings::getReduceWhenParked() { return reduceWhenParked; }

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
        if (RegDeleteValueW(regKey, L"irFFB"))
            text(L"Failed to remove startup registry value");

}
bool Settings::getRunOnStartup() { return runOnStartup; }

void Settings::setStartMinimised(bool minimised) {
    startMinimised = minimised;
    SendMessage(startMinimisedWnd, BM_SETCHECK, minimised ? BST_CHECKED : BST_UNCHECKED, NULL);
}
bool Settings::getStartMinimised() { return startMinimised; }

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
    DWORD specific = useCarSpecific;  

    RegCreateKeyEx(
        HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey))
        RegSetValueEx(regKey, L"useCarSpecific", 0, REG_DWORD, (BYTE *)&specific, sz);

}

void Settings::readRegSettings(char *car) {

    wchar_t dguid[GUIDSTRING_MAX];
    HKEY regKey;
    DWORD val;
    DWORD sz = sizeof(val);
    DWORD dgsz = sizeof(dguid);

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

        if (!RegGetValue(regKey, nullptr, L"device", RRF_RT_REG_SZ, nullptr, dguid, &dgsz))
            if (FAILED(IIDFromString(dguid, &devGuid)))
                devGuid = GUID_NULL;
        if (RegGetValue(regKey, nullptr, L"reduceWhenParked", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setReduceWhenParked(true);
        else
            setReduceWhenParked(val > 0);
        if (RegGetValue(regKey, nullptr, L"runOnStartup", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setRunOnStartup(false);
        else
            setRunOnStartup(val > 0);
        if (RegGetValue(regKey, nullptr, L"startMinimised", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setStartMinimised(false);
        else
            setStartMinimised(val > 0);
        if (RegGetValue(regKey, nullptr, L"useCarSpecific", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setUseCarSpecific(false, car);
        else
            setUseCarSpecific(val > 0, car);

    }
    else {
        setReduceWhenParked(true);
        setStartMinimised(false);
        setRunOnStartup(false);
        setUseCarSpecific(false, car);
    }

}

void Settings::readGenericSettings() {

    wchar_t dguid[GUIDSTRING_MAX];
    HKEY regKey;
    DWORD val;
    DWORD sz = sizeof(val);
    DWORD dgsz = sizeof(dguid);

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

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
        if (RegGetValue(regKey, nullptr, L"yawFactor", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setYawFactor(0);
        else
            setYawFactor(val);
        if (RegGetValue(regKey, nullptr, L"use360ForDirect", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setUse360ForDirect(true);
        else
            setUse360ForDirect(val > 0);
        if (RegGetValue(regKey, nullptr, L"extraLongLoad", RRF_RT_REG_DWORD, nullptr, &val, &sz))
            setExtraLongLoad(false);
        else
            setExtraLongLoad(val > 0);
       

    }
    else {
        setFfbType(FFBTYPE_DIRECT_FILTER);
        setMinForce(0);
        setMaxForce(45);
        setBumpsFactor(0);
        setLoadFactor(0);
        setYawFactor(0);
        setUse360ForDirect(true);
        setExtraLongLoad(false);
    }

}

void Settings::writeRegSettings() {

    wchar_t *guid;
    HKEY regKey;
    DWORD sz = sizeof(int);
    DWORD reduceParked = getReduceWhenParked();
    DWORD runOnStartup = getRunOnStartup();
    DWORD startMinimised = getStartMinimised();

    RegCreateKeyEx(
        HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

        if (SUCCEEDED(StringFromCLSID(devGuid, (LPOLESTR *)&guid))) {
            int len = (lstrlenW(guid) + 1) * sizeof(wchar_t);
            RegSetValueEx(regKey, L"device", 0, REG_SZ, (BYTE *)guid, len);
        }
        RegSetValueEx(regKey, L"reduceWhenParked", 0, REG_DWORD, (BYTE *)&reduceParked, sz);
        RegSetValueEx(regKey, L"runOnStartup", 0, REG_DWORD, (BYTE *)&runOnStartup, sz);
        RegSetValueEx(regKey, L"startMinimised", 0, REG_DWORD, (BYTE *)&startMinimised, sz);

    }

}

void Settings::writeGenericSettings() {

    HKEY regKey;
    DWORD sz = sizeof(int);
    DWORD bumps = getBumpsSetting();
    DWORD load = getLoadSetting();
    DWORD min = getMinForceSetting();
    DWORD use360 = getUse360ForDirect();
    DWORD extraLong = getExtraLongLoad();    
    DWORD yaw = (DWORD)yawFactor;

    RegCreateKeyEx(
        HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &regKey, nullptr
    );

    if (!RegOpenKeyEx(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_ALL_ACCESS, &regKey)) {

        RegSetValueEx(regKey, L"ffb", 0, REG_DWORD, (BYTE *)&ffbType, sz);
        RegSetValueEx(regKey, L"bumpsFactor", 0, REG_DWORD, (BYTE *)&bumps, sz);
        RegSetValueEx(regKey, L"loadFactor", 0, REG_DWORD, (BYTE *)&load, sz);
        RegSetValueEx(regKey, L"yawFactor", 0, REG_DWORD, (BYTE *)&yaw, sz);
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
    int type = 2, min = 0, max = 45, bumps = 0, load = 0, yaw = 0, extraLong = 0, use360 = 1;

    memset(carName, 0, sizeof(carName));

    while (std::getline(iniFile, line)) {
        if (
            sscanf_s(
                line.c_str(), INI_SCAN_FORMAT,
                carName, sizeof(carName),
                &type, &min, &max, &bumps, &load, &extraLong, &use360, &yaw
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
    setMinForce(min);
    setMaxForce(max);
    setBumpsFactor(bumps);
    setLoadFactor(load);
    setYawFactor(yaw);
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
    int type = 2, min = 0, max = 45, bumps = 0, load = 0, yaw = 0, extraLong = 0, use360 = 1;
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
                &type, &min, &max, &bumps, &load, &extraLong, &use360, &yaw
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
            getLoadSetting(), extraLongLoad, use360ForDirect, (int)yawFactor
        );
        writeWithNewline(tmpFile, buf);
        written = true;
    }

    if (written)
        goto MOVE;

    if (!iniPresent) {
        sprintf_s(buf, "car:ffbType:minForce:maxForce:bumps:load:incrLongEff:effUse360:sop\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "ffbType     | 0 = 360, 1 = 360I, 2 = 60DF_360, 3 = 60DF_720\r\n");
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
        sprintf_s(buf, "effUse360   | off = 0, on = 1\r\n");
        tmpFile.write(buf, strlen(buf));
        sprintf_s(buf, "sop         | min = 0, max = 100\r\n\r\n");
        tmpFile.write(buf, strlen(buf));
    }

    sprintf_s(
        buf, INI_PRINT_FORMAT,
        car, ffbType, getMinForceSetting(), maxForce, getBumpsSetting(),
        getLoadSetting(), extraLongLoad, use360ForDirect, (int)yawFactor
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

void Settings::writeWithNewline(std::ofstream &file, char *buf) {
    int len = strlen(buf);
    buf[len] = '\n';
    file.write(buf, len + 1);
}