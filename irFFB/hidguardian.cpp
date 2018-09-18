#include "hidguardian.h"

HidGuardian *HidGuardian::instance = nullptr;
SERVICE_STATUS_HANDLE HidGuardian::svcStatusHandle;
SERVICE_STATUS HidGuardian::svcStatus;
HANDLE HidGuardian::svcStopEvent;

HidGuardian *HidGuardian::init(DWORD pid) {
    return new HidGuardian(pid);
}

HidGuardian::HidGuardian(DWORD pid) {

    instance = this;
    readSettings();
    refreshStatus();
    if (enabled && hgStatus == STATUS_ENABLED) {
        if (queryService() == SERVICE_STOPPED)
            startService();
        Sleep(500);
        whitelist(pid);
    }

}

ATOM HidGuardian::registerClass(HINSTANCE hInstance) {

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

    hInst = hInstance;

    return RegisterClassExW(&wcex);

}

void HidGuardian::createWindow(HINSTANCE hInst) {

    if (!classIsRegistered) {
        registerClass(hInst);
        classIsRegistered = true;
    }

    mainWnd = CreateWindowW(
        windowClass, windowClass, WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 180,
        NULL, NULL, hInst, NULL
    );

    if (!mainWnd)
        return;

    statusWnd = CreateWindowW(
        L"STATIC", L"Status: not installed",
        WS_CHILD | WS_VISIBLE,
        40, 30, 150, 40, mainWnd, NULL, hInst, NULL
    );

    svcWnd = CreateWindowW(
        L"STATIC", L"Service: not installed",
        WS_CHILD | WS_VISIBLE,
        220, 30, 300, 40, mainWnd, NULL, hInst, NULL
    );

    installWnd = CreateWindowW(
        L"BUTTON", L"Install",
        WS_CHILD | WS_VISIBLE,
        40, 70, 100, 30, mainWnd, NULL, hInst, NULL
    );

    enabledWnd = checkbox(mainWnd, L"Enable", 220, 56);

    ShowWindow(mainWnd, SW_SHOWNORMAL);
    UpdateWindow(mainWnd);
    refreshStatus();
    setSvcStatus(queryService());
    return;

}

LRESULT CALLBACK HidGuardian::wndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

    switch (message) {

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                if (HIWORD(wParam) == BN_CLICKED) {
                    if ((HWND)lParam == instance->installWnd)
                        instance->install();
                    else if ((HWND)lParam == instance->enabledWnd) {
                        bool oldValue = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        instance->setEnabled(!oldValue);
                    }
                }
                break;
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

        case WM_DESTROY:
            instance->writeSettings();
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);

    }

    return 0;

}

void HidGuardian::install() {

    BOOL wow64;
    wchar_t *infPath = nullptr;

    IsWow64Process(GetCurrentProcess(), &wow64);

    if (!isElevated()) {
        elevate();
        return;
    }

    if (hgStatus != STATUS_NOTINSTALLED) {
        text(L"HG: already installed");
        goto installSvc;
    }
   
    wchar_t *hidZip = download();

    if (hidZip == nullptr)
        return;

    wchar_t *hidFld = unzip(hidZip);

    if (hidFld == nullptr)
        goto cleanup;

    int infPathLen = wcslen(hidFld) + 64;
    infPath = new wchar_t[infPathLen];
    StringCchCopyW(infPath, infPathLen, hidFld);
    if (wow64)
        StringCchCatW(infPath, infPathLen, L"\\x64\\HidGuardian.inf");
    else
        StringCchCatW(infPath, infPathLen, L"\\x86\\HidGuardian.inf");

    if (!wow64) {
        text(L"HG: Installing...");
        if (!doInstall(infPath))
            goto del;
        else if (!updateDriver(infPath))
            goto del;
    }
    else {
                
        wchar_t *tmpFile = unpackInstaller64();

        if (tmpFile == nullptr)
            goto del;

        exInstaller64(tmpFile, infPath);
        DeleteFileW(tmpFile);
        delete[] tmpFile;

    }

    if (!installFilter())
        goto del;

    refreshStatus();

installSvc:
    if (queryService() == -1)
        if (!installService()) {
            text(L"HG: Failed to install service");
            goto del;
        }
    
    setStatus(hgStatus);

del:
    if (infPath != nullptr)
        delete[] infPath;
cleanup:
    cleanupInstall();

}

bool HidGuardian::isEnabled() {
    return hgStatus == STATUS_ENABLED;
}

void HidGuardian::setDevice(WORD vid, WORD pid) {

    pipeMsg msg;

    currentVid = vid;
    currentPid = pid;

    if (hgStatus != STATUS_ENABLED)
        return;

    msg.cmd = HG_CMD_DEVICE_ADD;
    StringCchPrintf(msg.id.hwid, 32, HG_HWID_FMT, vid, pid);
    UINT resp = sendSvcMsg(&msg);
    if (resp == HG_SVC_RET_SUCCESS)
        text(L"HG: *** Dev change - unplug/replug or reboot ***");
    else if (resp == HG_SVC_RET_ERROR)
        text(L"HG: Failed to set device");
    
}

void HidGuardian::removeDevice(WORD vid, WORD pid, bool warn) {

    pipeMsg msg;

    if (hgStatus != STATUS_ENABLED)
        return;

    msg.cmd = HG_CMD_DEVICE_DEL;
    StringCchPrintf(msg.id.hwid, 32, HG_HWID_FMT, vid, pid);
    if (sendSvcMsg(&msg) == HG_SVC_RET_ERROR) {
        text(L"HG: Failed to remove device");
        return;
    }
    if (warn)
        text(L"HG: *** Dev unmasked - unplug/replug or reboot ***");

}

void HidGuardian::whitelist(DWORD pid) {

    pipeMsg msg;

    if (hgStatus != STATUS_ENABLED)
        return;

    msg.cmd = HG_CMD_WHITELIST_ADD;
    msg.id.pid = pid;
    if (sendSvcMsg(&msg) == HG_SVC_RET_ERROR)
        text(L"HG: Failed to add to whitelist");

}

void HidGuardian::unWhitelist(DWORD pid) {

    pipeMsg msg;

    msg.cmd = HG_CMD_WHITELIST_DEL;
    msg.id.pid = pid;
    sendSvcMsg(&msg);

}

void HidGuardian::stop(DWORD pid) {
    unWhitelist(pid);
    stopService();
}

int HidGuardian::getStatus() {
    return hgStatus;
}

void HidGuardian::setStatus(int s) {

    wchar_t buf[64];

    if (s < STATUS_NOTINSTALLED || s > STATUS_ENABLED)
        return;

    if (s == STATUS_ENABLED && queryService() == SERVICE_STOPPED) {
        hgStatus = s;
        if (startService()) {
            whitelist(GetCurrentProcessId());
            if (currentVid != 0)
                setDevice(currentVid, currentPid);
        }
        else
            text(L"HG: Service failed to start");
    }
    else if (s == STATUS_DISABLED && queryService() == SERVICE_RUNNING) {
        unWhitelist(GetCurrentProcessId());
        if (currentVid != 0)
            removeDevice(currentVid, currentPid, true);
        stopService();
        hgStatus = s;
    }

    
    StringCchPrintfW(buf, 64, L"Status: %s", statuses[hgStatus]);
    SendMessage(statusWnd, WM_SETTEXT, NULL, (LPARAM)buf);
    SendMessage(enabledWnd, BM_SETCHECK, s == STATUS_ENABLED ? BST_CHECKED : BST_UNCHECKED, NULL);
    EnableWindow(svcWnd, s > STATUS_NOTINSTALLED);
    EnableWindow(enabledWnd, s > STATUS_NOTINSTALLED);
    EnableWindow(installWnd, s == STATUS_NOTINSTALLED);

}

int HidGuardian::refreshStatus() {

    ULONG s, problem;
    GUID classGuid = GUID_DEVCLASS_SYSTEM;

    hgStatus = STATUS_NOTINSTALLED;

    HDEVINFO devInfo = SetupDiGetClassDevsW(&classGuid, NULL, NULL, DIGCF_PRESENT);

    if (devInfo == INVALID_HANDLE_VALUE)
        return hgStatus;

    SP_DEVINFO_DATA devInfoData = { 0 };
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    int idx = 0;
    bool found = false;
    wchar_t buf[64];
    DWORD type, required;

    for (idx = 0; SetupDiEnumDeviceInfo(devInfo, idx, &devInfoData); idx++) {
        if (
            !SetupDiGetDeviceRegistryPropertyW(
                devInfo, &devInfoData, SPDRP_HARDWAREID, &type,
                (PBYTE)buf, sizeof(buf), &required
            )
        )
            continue;

        if (wcscmp(buf, HG_HARDWARE_ID) == 0) {
            found = true;
            break;
        }

    }

    if (!found)
        goto release;

    if (CM_Get_DevNode_Status(&s, &problem, devInfoData.DevInst, 0) != CR_SUCCESS) {
        text(L"HG: Failed to get status");
        goto release;
    }

    if (s & DN_STARTED) {
        if (enabled)
            hgStatus = STATUS_ENABLED;
        else
            hgStatus = STATUS_DISABLED;
    }   

release:

    SetupDiDestroyDeviceInfoList(devInfo);
    setStatus(hgStatus);
    return hgStatus;

}

void HidGuardian::setEnabled(bool en) {

    enabled = en;

    if (en && hgStatus == STATUS_DISABLED)
        setStatus(STATUS_ENABLED);
    else if (!en && hgStatus == STATUS_ENABLED)
        setStatus(STATUS_DISABLED);

}

wchar_t *HidGuardian::unpackInstaller64() {

    HRSRC hrsrc = FindResourceW(hInst, (LPCWSTR)IDR_HIDG64, RT_RCDATA);

    if (hrsrc == NULL) {
        text(L"HG: Failed to locate 64 bit installer resource");
        return nullptr;
    }

    DWORD size = SizeofResource(hInst, hrsrc);
    HGLOBAL hGlb = LoadResource(hInst, hrsrc);

    if (hGlb == NULL) {
        text(L"HG: Failed to load 64 bit installer resource");
        return nullptr;
    }

    BYTE *rbuf = (BYTE *)LockResource(hGlb);

    if (rbuf == NULL) {
        text(L"HG: Failed to lock 64 bit installer resource");
        return nullptr;
    }

    wchar_t tmpPath[MAX_PATH];

    if (!GetTempPathW(MAX_PATH, tmpPath))
        return nullptr;

    wchar_t *tmpFile = new wchar_t[MAX_PATH];
    StringCchPrintf(tmpFile, MAX_PATH, L"%s%s", tmpPath, HG_INSTALLER_NAME);

    HANDLE file =
        CreateFile(
            tmpFile, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
        );

    if (file == INVALID_HANDLE_VALUE) {
        text(L"HG: Failed to create temp file at %s", tmpFile);
        goto delTmpFile;
    }

    DWORD written;
    if (!WriteFile(file, rbuf, size, &written, NULL)) {
        text(L"HG: Failed to write to temp file at %s", tmpFile);
        CloseHandle(file);
        goto delTmpFile;
    }

    CloseHandle(file);
    return tmpFile;

delTmpFile:
    delete[] tmpFile;
    return nullptr;

}

bool HidGuardian::exInstaller64(wchar_t *path, wchar_t *infPath) {

    wchar_t cmdLine[MAX_PATH];
    StringCchCopyW(cmdLine, MAX_PATH, infPath);

    text(L"HG: Launching install helper...");

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = cmdLine;
    sei.nShow = SW_NORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        if (GetLastError() == ERROR_CANCELLED)
            text(L"HG: Install cancelled");
        else
            text(L"HG: ShellExecute failed: %d", GetLastError());
        return false;
    }

    DWORD exitcode = 0;

    WaitForSingleObject(sei.hProcess, INFINITE);
    GetExitCodeProcess(sei.hProcess, &exitcode);
    CloseHandle(sei.hProcess);

    if (exitcode != 0) {
        if (exitcode > INSTALLER_MAX_ERRORCODE)
            text(L"HG: Unknown error from installer");
        else {
            wchar_t msg[128];
            StringCchPrintfW(msg, 128, L"HG: %s", installerErrors[exitcode]);
            text(msg);
        }
        return false;
    }
    
    text(L"HG: Installed");
    return true;

}

wchar_t *HidGuardian::download() {
    
    DWORD size = 0, inBuf = 0, written = 0;
    wchar_t tmpPath[MAX_PATH];
    HANDLE file;
    BYTE *buf;
    wchar_t *ret = nullptr, *tmpFile = nullptr;

    HINTERNET session =
        WinHttpOpen(
            L"irFFB/1.4", 
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );

    if (session == NULL) {
        text(L"HG: Failed to open http session");
        return nullptr;
    }

    HINTERNET connect =
        WinHttpConnect(session, HG_DOWNLOAD_SITE, INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (connect == NULL) {
        text(L"HG: Failed to connect");
        return nullptr;
    }

    HINTERNET request =
        WinHttpOpenRequest(
            connect, L"GET",
            HG_DOWNLOAD_PATH,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );

    if (request == NULL) {
        text(L"HG: Failed to open request");
        return nullptr;
    }

    if (
        !WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0,
            0, NULL
        )
    ) {
        text(L"HG: Failed to send request");
        return nullptr;
    }

    if (
        !WinHttpReceiveResponse(request, NULL)
    ) {
        text(L"HG: Failed to receive response");
        return nullptr;
    }

    if (!GetTempPathW(MAX_PATH, tmpPath))
        return nullptr;

    tmpFile = new wchar_t[MAX_PATH];
    srand(GetTickCount());
    StringCchPrintf(tmpFile, MAX_PATH, L"%shidg%x.zip", tmpPath, rand());

    file = 
        CreateFile(
            tmpFile, GENERIC_WRITE, 0, NULL, 
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
        );

    if (file == INVALID_HANDLE_VALUE) {
        text(L"HG: Failed to create temp file at %s", tmpFile);
        return nullptr;
    }

    text(L"HG: Dowloading..");

    do {
        
        size = 0;
        
        if (!WinHttpQueryDataAvailable(request, &size))
            goto close;

        if (size == 0)
            break;

        buf = new BYTE[size + 1];

        if (buf == NULL)
            goto close;

        ZeroMemory(buf, size + 1);

        if (!WinHttpReadData(request, buf, size, &inBuf))
            goto close;

        WriteFile(file, buf, inBuf, &written, NULL);

        delete[] buf;

    } while (size > 0);

    zipPath = tmpFile;

close:
    CloseHandle(file);

    if (zipPath == nullptr && tmpFile != nullptr)
        delete[] tmpFile;

    return zipPath;

}

wchar_t *HidGuardian::unzip(wchar_t *zipPath) {

    IShellDispatch *isd = nullptr;

    VARIANT zipFile, folder, item, options;

    Folder *zipFileFolder = nullptr, *destFolder = nullptr;
    FolderItems *zipItems = nullptr;

    IDispatch *itemI;

    wchar_t tmpPath[MAX_PATH], *ret = nullptr, *tmpFile = nullptr;

    CoInitialize(NULL);
    if (
        CoCreateInstance(
            CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void **)&isd
        ) != S_OK
    ) {
        text(L"HG: Failed to cocreateinstance of shell server");
        return false;
    }

    zipFile.vt = VT_BSTR;
    zipFile.bstrVal = _bstr_t(zipPath);
    isd->NameSpace(zipFile, &zipFileFolder);

    if (!zipFileFolder) {
        text(L"HG: Failed to set folder NS to %s", zipPath);
        goto releaseIsd;
    }

    if (!GetTempPathW(MAX_PATH, tmpPath))
        goto releaseZipFolder;

    int len = wcslen(tmpPath) + 32;
    tmpFile = new wchar_t[len];
    StringCchPrintf(tmpFile, len, L"%shidg%x", tmpPath, rand());

    if (!CreateDirectoryW(tmpFile, NULL)) {
        text(L"HG: Failed to create dir at %s", tmpFile);
        goto releaseZipFolder;
    }

    folder.vt = VT_BSTR;
    folder.bstrVal = _bstr_t(tmpFile);
    isd->NameSpace(folder, &destFolder);

    if (!destFolder) {
        text(L"HG: Failed to set folder NS to %s", tmpFile);
        goto releaseZipFolder;
    }

    zipFileFolder->Items(&zipItems);

    if (!zipItems) {
        text(L"HG: Failed to enum zip items");
        goto releaseDestFolder;
    }

    zipItems->QueryInterface(IID_IDispatch, (void **)&itemI);

    item.vt = VT_DISPATCH;
    item.pdispVal = itemI;

    options.vt = VT_I4;
    options.lVal = 1024 | 512 | 16 | 4;

    text(L"HG: Decompressing...");

    if (destFolder->CopyHere(item, options) == S_OK)
        fldPath = tmpFile;

    itemI->Release();
    zipItems->Release();
releaseDestFolder:
    destFolder->Release();
releaseZipFolder:
    zipFileFolder->Release();
releaseIsd:
    isd->Release();

    if (fldPath == nullptr && tmpFile != nullptr)
        delete[] tmpFile;

    return fldPath;

}

bool HidGuardian::doInstall(wchar_t *infPath) {

    GUID classGuid;
    wchar_t hwIds[64], className[MAX_CLASS_NAME_LEN];
    HDEVINFO devInfo;
    SP_DEVINFO_DATA devInfoData;
    bool ret = false;

    ZeroMemory(hwIds, sizeof(hwIds));
    StringCchCopy(hwIds, 64, HG_HARDWARE_ID);

    if (!SetupDiGetINFClass(infPath, &classGuid, className, MAX_CLASS_NAME_LEN, 0)) {
        text(L"HG: Failed to get INF class from %s", infPath);
        return false;
    }

    devInfo = SetupDiCreateDeviceInfoList(&classGuid, NULL);
    if (devInfo == INVALID_HANDLE_VALUE) {
        text(L"HG: Failed to create DeviceInfoList");
        return false;
    }

    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    if (
        !SetupDiCreateDeviceInfoW(
            devInfo, className, &classGuid, NULL, NULL, DICD_GENERATE_ID, &devInfoData
        )
    ) {
        text(L"HG: Failed to create DeviceInfo");
        goto end;
    }

    if (
        !SetupDiSetDeviceRegistryProperty(
            devInfo, &devInfoData, SPDRP_HARDWAREID, (LPBYTE)hwIds, 
            (wcslen(hwIds) + 2) * sizeof(wchar_t)
        )
    ) {
        text(L"HG: Failed to set device registry property");
        goto end;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devInfo, &devInfoData)) {
        text(L"HG: Class installer failed");
        goto end;
    }

    text(L"HG: className is %s", className);
        
    ret = true;
   
end:
    SetupDiDestroyDeviceInfoList(devInfo);
    return ret;

}

bool HidGuardian::installFilter() {

    GUID classGuid;
    DWORD numGuids;
    bool ret = false;

    if (!SetupDiClassGuidsFromNameEx(L"HIDClass", &classGuid, 1, &numGuids, NULL, NULL)) {
        text(L"HG: Error getting HIDClass GUID");
        return false;
    }

    HKEY key = SetupDiOpenClassRegKeyExW(&classGuid, KEY_WRITE, DIOCR_INSTALLER, NULL, NULL);
    
    if (key == INVALID_HANDLE_VALUE) {
        text(L"HG: Failed to open class reg key");
        return false;
    }

    wchar_t *multiSz = getRegMultiSz(key, REGSTR_VAL_UPPERFILTERS);
    wchar_t *newMultiSz = prependToMultiSz(multiSz, HG_FILTER_NAME);

    if (newMultiSz != nullptr)
        ret = (
            RegSetValueExW(
                key, REGSTR_VAL_UPPERFILTERS, 0, REG_MULTI_SZ,
                (LPBYTE)newMultiSz, multiSzLen(newMultiSz)
            ) == NO_ERROR
        );
    else
        ret = true;

    if (multiSz != nullptr)
        delete[] multiSz;
    if (newMultiSz != nullptr)
        delete[] newMultiSz;
    RegCloseKey(key);
    return ret;

}

bool HidGuardian::updateDriver(wchar_t *infPath) {

    HMODULE newDevMod = LoadLibrary(L"newdev.dll");
    UpdateDriverProto updateFn;

    if (!newDevMod) {
        text(L"HG: Failed to LoadLibrary newdev.dll");
        return false;
    }

    updateFn = (UpdateDriverProto)GetProcAddress(newDevMod, "UpdateDriverForPlugAndPlayDevicesW");

    if (!updateFn) {
        text(L"HG: Failed to find UpdateDriverForPlugAndPlayDevicesW");
        return false;
    }

    if (!updateFn(NULL, HG_HARDWARE_ID, infPath, INSTALLFLAG_FORCE, NULL)) {
        text(L"HG: Failed to update HG driver");
        return false;
    }

    return true;

}

void HidGuardian::cleanupInstall() {

    if (zipPath != nullptr)
        DeleteFileW(zipPath);

    if (fldPath == nullptr)
        return;
    
    *(fldPath + wcslen(fldPath) + 1) = '\0';

    SHFILEOPSTRUCT sfo = { 0 };
    sfo.wFunc = FO_DELETE;
    sfo.pFrom = fldPath;
    sfo.fFlags = FOF_NO_UI;
    SHFileOperationW(&sfo);

}

bool HidGuardian::installService() {

    SC_HANDLE scm, svc;
    wchar_t path[MAX_PATH];
    SERVICE_DESCRIPTIONW desc = { L"irFFB HidGuardian service" };
    PSECURITY_DESCRIPTOR sd;
    bool ret = false;

    wchar_t *sddl =
        L"D:"
        L"(A;;CCLCSWRPWPDTLOCRRC;;;SY)"           // default permissions for local system
        L"(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)"   // default permissions for administrators
        L"(A;;CCLCSWLOCRRC;;;AU)"                 // default permissions for authenticated users
        L"(A;;CCLCSWRPWPDTLOCRRC;;;PU)"           // default permissions for power users
        L"(A;;RPWP;;;IU)";                        // allow interactive users to start and stop

    StringCchCopy(path, MAX_PATH, L"\"");

    if (!GetModuleFileNameW(NULL, path + 1, MAX_PATH - 1)) {
        text(L"HG: Service install - failed to locate module");
        return ret;
    }

    StringCchCatW(path, MAX_PATH, L"\" service");

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (scm == nullptr) {
        text(L"HG: Failed to open service manager");
        return ret;
    }

    svc =
        CreateServiceW(
            scm, SVCNAME, L"irFFB HG Service", SERVICE_ALL_ACCESS, 
            SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, 
            SERVICE_ERROR_NORMAL, path,
            NULL, NULL, NULL, NULL, NULL
        );

    if (svc == nullptr) {
        text(L"HG: Failed to create service");
        goto closeScm;
    }

    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, NULL)) {
        text(L"HG: Failed to convert security descriptor");
        goto closeSvc;
    }

    if (!SetServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, sd)) {
        text(L"HG: Failed to set service DACLs");
        goto freeSd;
    }
        
    ret = true;

    text(L"HG: irFFB HG service installed");

freeSd:
    LocalFree(sd);
closeSvc:
    CloseServiceHandle(svc);
closeScm:
    CloseServiceHandle(scm);
    setSvcStatus(queryService());
    return ret;

}

int HidGuardian::queryService() {

    SC_HANDLE scm, svc;
    SERVICE_STATUS_PROCESS status;
    DWORD needed;
    int ret = -1;

    scm = OpenSCManagerW(NULL, NULL, STANDARD_RIGHTS_READ);

    if (scm == nullptr) {
        text(L"HG: Failed to open service manager");
        return ret;
    }

    svc = OpenServiceW(scm, SVCNAME, SERVICE_QUERY_STATUS);

    if (svc == nullptr) {

        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
            goto closeScm;
        else {
            text(L"HG: Failed to open service");
            goto closeScm;
        }

    }

    if (
        !QueryServiceStatusEx(
            svc, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &needed
        )
    ) {
        text(L"HG: Failed to query service");
        goto closeSvc;
    }

    ret = status.dwCurrentState;

closeSvc:
    CloseServiceHandle(svc);
closeScm:
    CloseServiceHandle(scm);
    return ret;

}

bool HidGuardian::startService() {

    SC_HANDLE scm, svc;
    LPCWSTR arg = L"service";
    bool ret = false;

    if (queryService() != SERVICE_STOPPED)
        return true;

    scm = OpenSCManagerW(NULL, NULL, STANDARD_RIGHTS_READ|STANDARD_RIGHTS_EXECUTE);

    if (scm == nullptr) {
        text(L"HG: Failed to open service manager");
        return false;
    }

    svc = OpenServiceW(scm, SVCNAME, SERVICE_START);

    if (svc == nullptr) {

        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
            goto closeScm;
        else {
            text(L"HG: Failed to open service");
            goto closeScm;
        }

    }

    if (!StartService(svc, 1, &arg))
        text(L"HG: Failed to start service: %d", GetLastError());
    else {
        for (int i = 0; i < 10; i++) {
            if (queryService() == SERVICE_RUNNING) {
                ret = true;
                break;
            }
            Sleep(200);
        }
        Sleep(500);
    }

    setSvcStatus(queryService());
    
    CloseServiceHandle(svc);
closeScm:
    CloseServiceHandle(scm);
    return ret;

}

void HidGuardian::stopService() {

    SC_HANDLE scm, svc;
    SERVICE_STATUS status;
    wchar_t *arg = L"service";

    if (queryService() != SERVICE_RUNNING)
        return;

    scm = OpenSCManagerW(NULL, NULL, STANDARD_RIGHTS_READ|STANDARD_RIGHTS_EXECUTE);

    if (scm == nullptr) {
        text(L"HG: Failed to open service manager");
        return;
    }

    svc = OpenServiceW(scm, SVCNAME, SERVICE_STOP);

    if (svc == nullptr) {

        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST)
            goto closeScm;
        else {
            text(L"HG: Failed to open service");
            goto closeScm;
        }

    }

    Sleep(500);

    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status))
        text(L"HG: Failed to stop service");
    else {
        for (int i = 0; i < 10; i++) {
            if (queryService() == SERVICE_STOPPED)
                break;
            Sleep(500);
        }
    }

    setSvcStatus(queryService());

    CloseServiceHandle(svc);
closeScm:
    CloseServiceHandle(scm);

}

void HidGuardian::setSvcStatus(int status) {

    wchar_t buf[64];
    wchar_t *sTxt;

    serviceStatus = status;

    switch (status) {
        case -1: sTxt = L"Not installed";               break;
        case SERVICE_STOP_PENDING: sTxt = L"Stopping";  break;
        case SERVICE_STOPPED: sTxt = L"Stopped";        break;
        case SERVICE_START_PENDING: sTxt = L"Starting"; break;
        case SERVICE_RUNNING: sTxt = L"Running";        break;
        default: sTxt = L"Unknown";
    }

    StringCchPrintfW(buf, 64, L"Service: %s", sTxt);
    SendMessage(svcWnd, WM_SETTEXT, NULL, (LPARAM)buf);

}

UINT HidGuardian::sendSvcMsg(pipeMsg *msg) {

    DWORD written, mode = PIPE_READMODE_MESSAGE;
    UINT ret = HG_SVC_RET_ERROR;
    UINT resp;
    
    if (msg == nullptr) {
        text(L"HG: null service pipe msg");
        return HG_SVC_RET_ERROR;
    }

    if (serviceStatus != SERVICE_RUNNING)
        return HG_SVC_RET_ERROR;
        
    DWORD size =
        (
            msg->cmd == HG_CMD_WHITELIST_ADD ||
            msg->cmd == HG_CMD_WHITELIST_DEL
        ) ?
           PIPE_MSG_SIZE_PID : PIPE_MSG_SIZE_HWID;

    HANDLE pipe =
        CreateFileW(
            HG_SVC_PIPE,
            GENERIC_READ|GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

    if (pipe == INVALID_HANDLE_VALUE) {
        text(L"HG: Error opening service pipe: %d", GetLastError());
        return HG_SVC_RET_ERROR;
    }

    if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
        text(L"HG: Error setting service pipe mode");
        goto closePipe;
    }

    if (!WriteFile(pipe, msg, size, &written, NULL)) {
        text(L"HG: Error writing to service pipe");
        goto closePipe;
    }

    for (int i = 0; i < 10; i++) {

        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &written, NULL)) {
            text(L"HG: Error peeking service pipe");
            goto closePipe;
        }

        if (written >= sizeof(resp))
            break;

        Sleep(50);
        
    }

    if (written < sizeof(resp)) {
        text(L"HG: Timed out reading from service pipe");
        goto closePipe;
    }

    if (!ReadFile(pipe, &resp, sizeof(resp), &written, NULL)) {
        text(L"HG: Error reading from service pipe");
        goto closePipe;
    }

    ret = resp;

closePipe:
    CloseHandle(pipe);
    return ret;

}

bool HidGuardian::isElevated() {

    HANDLE token;
    TOKEN_ELEVATION elevation;
    DWORD retSize = sizeof(TOKEN_ELEVATION);
    bool ret = false;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (
            GetTokenInformation(
                token, TokenElevation, &elevation, sizeof(elevation), &retSize
            )
        ) {
            if (elevation.TokenIsElevated)
                ret = true;
        }
        CloseHandle(token);
    }

    return ret;

}

void HidGuardian::elevate() {

    wchar_t modPath[MAX_PATH];

    if (!GetModuleFileNameW(NULL, modPath, MAX_PATH))
        return;

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(SHELLEXECUTEINFOW));
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.lpVerb = L"runas";
    sei.lpFile = modPath;
    sei.lpParameters = L"instHG";
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExW(&sei)) {
        if (GetLastError() == ERROR_CANCELLED)
            text(L"HG: Installation cancelled");
        else
            text(L"HG: ShellExecute failed");
    }
    else
        exit(0);

}

void HidGuardian::readSettings() {

    HKEY key = Settings::getSettingsRegKey();

    if (key == NULL) {
        enabled = true;
        return;
    }

    enabled = Settings::getRegSetting(key, L"hgEnabled", true);
    RegCloseKey(key);

}

void HidGuardian::writeSettings() {

    HKEY key = Settings::getSettingsRegKey();

    if (key == NULL)
        return;

    Settings::setRegSetting(key, L"hgEnabled", enabled);
    RegCloseKey(key);

}

// Static reg helpers

wchar_t *HidGuardian::getRegMultiSz(HKEY key, LPCTSTR val) {

    wchar_t *buf = new wchar_t[1024];
    LONG ret;
    DWORD type, len = 1022 * sizeof(wchar_t);

    ret = RegQueryValueEx(key, val, NULL, &type, (PBYTE)buf, &len);
    while (ret != NO_ERROR) {
        if (GetLastError() != ERROR_MORE_DATA)
            goto fail;
        if (type != REG_MULTI_SZ)
            goto fail;
        delete[] buf;
        buf = new wchar_t[len / sizeof(wchar_t) + 2];
        ret = RegQueryValueEx(key, val, NULL, &type, (PBYTE)buf, &len);
    }

    buf[len / sizeof(wchar_t)] = '\0';
    buf[len / sizeof(wchar_t) + 1] = '\0';

    return buf;
    
fail:
    delete[] buf;
    return nullptr;

}

wchar_t *HidGuardian::prependToMultiSz(wchar_t *multiSz, wchar_t *value) {

    wchar_t *ptr, *scan, *buf = nullptr;
    size_t len, bufLen;

    if (multiSz == NULL) {
        bufLen = wcslen(value) + 2;
        buf = new wchar_t[bufLen];
        StringCchCopyW(buf, bufLen, value);
        ptr = buf + bufLen - 1;
    }
    else {

        for (scan = multiSz; scan[0]; scan += wcslen(scan) + 1)
            if (!wcscmp(value, scan))
                return nullptr;

        bufLen = scan - multiSz + wcslen(value) + 2;
        buf = new wchar_t[bufLen];
        ptr = buf;

        StringCchCopyW(ptr, bufLen, value);
        len = wcslen(value) + 1;
        ptr += len;
        bufLen -= len;

        for (scan = multiSz; scan[0]; scan += len) {
            StringCchCopy(ptr, bufLen, scan);
            len = wcslen(ptr) + 1;
            ptr += len;
            bufLen -= len;
        }

    }

    *ptr = '\0';

    return buf;

}

wchar_t *HidGuardian::removeFromMultiSz(wchar_t *multiSz, wchar_t *value) {

    wchar_t *buf, *ptr, *scan;
    bool matched = false;
    size_t len, oldlen = multiSzLen(multiSz);

    if (!oldlen)
        return nullptr;

    ptr = buf = new wchar_t[oldlen];

    for (scan = multiSz; scan[0]; scan += len) {

        len = wcslen(scan) + 1;

        if (wcscmp(value, scan)) {
            StringCchCopy(ptr, oldlen, scan);
            oldlen -= len;
            ptr += len;
        }
        else
            matched = true;

    }

    if (!matched) {
        delete[] buf;
        return nullptr;
    }

    *ptr = '\0';
    return buf;

}
 
size_t HidGuardian::multiSzLen(wchar_t *multiSz) {

    wchar_t *scan;

    if (multiSz == nullptr)
        return 0;

    for (scan = multiSz; scan[0]; scan += wcslen(scan) + 1) {};
    return (++scan - multiSz) * sizeof(wchar_t);

}

// Service stuff

void WINAPI HidGuardian::SvcMain(DWORD argc, LPTSTR argv) {

    svcStatusHandle = RegisterServiceCtrlHandlerW(SVCNAME, svcCtrlHandler);

    if (svcStatusHandle == INVALID_HANDLE_VALUE) {
        svcReportError(L"Failed to register ctrl handler");
        return;
    }

    svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    svcStatus.dwServiceSpecificExitCode = 0;
    svcReportStatus(SERVICE_START_PENDING, NO_ERROR, 0);

    svcStopEvent = CreateEventW(NULL, true, false, L"irFFBsvcStopEvent");
    if (svcStopEvent == INVALID_HANDLE_VALUE) {
        svcReportError(L"Failed to create service stop event");
        svcReportStatus(SERVICE_STOPPED, SERVICE_ERROR_CRITICAL, 0);
        return;
    }

    if (!CreateThread(NULL, 0, pipeServerThread, NULL, 0, NULL)) {
        svcReportError(L"Failed to start pipe server thread");
        svcReportStatus(SERVICE_STOPPED, SERVICE_ERROR_CRITICAL, 0);
        return;
    }

    svcReportStatus(SERVICE_RUNNING, NO_ERROR, 0);
    WaitForSingleObject(svcStopEvent, INFINITE);
    svcReportStatus(SERVICE_STOPPED, NO_ERROR, 0);

}

void WINAPI HidGuardian::svcCtrlHandler(DWORD ctrl) {

    if (ctrl != SERVICE_CONTROL_STOP)
        return;
     
    svcReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
    SetEvent(svcStopEvent);

}

void HidGuardian::svcReportStatus(DWORD status, DWORD exitCode, DWORD waitHint) {

    svcStatus.dwCurrentState = status;
    svcStatus.dwWin32ExitCode = exitCode;
    svcStatus.dwWaitHint = waitHint;
    svcStatus.dwControlsAccepted = (status == SERVICE_START_PENDING ? 0 : SERVICE_ACCEPT_STOP);

    SetServiceStatus(svcStatusHandle, &svcStatus);

}

void HidGuardian::svcReportError(wchar_t *msg) {

    HANDLE evtSrc = RegisterEventSourceW(NULL, SVCNAME);
    LPCTSTR str[] = { SVCNAME, msg };

    ReportEventW(evtSrc, EVENTLOG_ERROR_TYPE, 0, 0xC0020001L, NULL, 2, 0, str, NULL);
    DeregisterEventSource(evtSrc);

}

UINT HidGuardian::svcAddDevice(wchar_t *dev) {

    HKEY key;
    UINT ret = HG_SVC_RET_ERROR;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, HG_PARAMS_KEY, 0, KEY_ALL_ACCESS, &key)) {
        svcReportError(L"Failed to open HG parameters key");
        return ret;
    }

    wchar_t *devices = getRegMultiSz(key, HG_DEVICES_VALUE_NAME);
    wchar_t *newDevices = prependToMultiSz(devices, dev);

    if (newDevices != nullptr) {
        if (RegSetValueExW(key, HG_DEVICES_VALUE_NAME, 0, REG_MULTI_SZ, (BYTE *)newDevices, multiSzLen(newDevices)))
            svcReportError(L"Failed to add device");
        else
            ret = HG_SVC_RET_SUCCESS;
    }
    else
        ret = HG_SVC_RET_EXISTS;

    if (devices != nullptr)
        delete[] devices;
    if (newDevices != nullptr)
        delete[] newDevices;

    RegCloseKey(key);
    return ret;

}

UINT HidGuardian::svcDelDevice(wchar_t *dev) {

    HKEY key;
    UINT ret = HG_SVC_RET_ERROR;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, HG_PARAMS_KEY, 0, KEY_ALL_ACCESS, &key)) {
        svcReportError(L"Failed to open HG parameters key");
        return ret;
    }

    wchar_t *devices = getRegMultiSz(key, HG_DEVICES_VALUE_NAME);

    if (devices == nullptr)
        goto closeKey;

    wchar_t *newDevices = removeFromMultiSz(devices, dev);

    if (newDevices == nullptr)
        goto done;

    if (RegSetValueExW(key, HG_DEVICES_VALUE_NAME, 0, REG_MULTI_SZ, (BYTE *)newDevices, multiSzLen(newDevices))) {
        svcReportError(L"Failed to remove device");
        goto done;
    }

    ret = HG_SVC_RET_SUCCESS;

done:
    if (devices != nullptr)
        delete[] devices;
    if (newDevices != nullptr)
        delete[] newDevices;
closeKey:
    RegCloseKey(key);
    return ret;

}

UINT HidGuardian::svcAddWlist(UINT pid) {

    HKEY key, wlKey, pidKey;
    wchar_t name[MAX_PATH];
    DWORD nameLen = MAX_PATH;
    int idx = 0;
    UINT ret = HG_SVC_RET_ERROR;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, HG_PARAMS_KEY, 0, KEY_ALL_ACCESS, &key)) {
        svcReportError(L"Failed to oepn HG parameters key");
        return ret;
    }

    if (
        RegCreateKeyExW(
            key, HG_WHITELIST_SUBKEY, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, NULL, &wlKey, NULL
        )
    ) {
        svcReportError(L"Failed to create/open whitelist key");
        RegCloseKey(key);
        return ret;
    }

    while (ret == ERROR_SUCCESS) {
        nameLen = MAX_PATH;
        ret = RegEnumKeyEx(wlKey, idx++, name, &nameLen, 0, NULL, NULL, NULL);
        if (ret != ERROR_SUCCESS)
            break;
        if (_wtoi(name) == pid) {
            ret = HG_SVC_RET_EXISTS;
            goto done;
        }
    }

    if (_itow_s(pid, name, 10)) {
        svcReportError(L"Invalid whitelist pid");
        goto done;
    }

    if (
        RegCreateKeyExW(
            wlKey, name, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, NULL, &pidKey, NULL
        )
    ) { 
        svcReportError(L"Failed to create whitelist subkey");
        goto done;
    }

    RegCloseKey(pidKey);
    ret = HG_SVC_RET_SUCCESS;

done:
    RegCloseKey(wlKey);
    RegCloseKey(key);
    return ret;

}

UINT HidGuardian::svcDelWlist(UINT pid) {

    HKEY key;
    UINT ret = HG_SVC_RET_ERROR;
    wchar_t str[MAX_PATH];

    StringCchCopy(str, MAX_PATH, HG_PARAMS_KEY);
    StringCchCat(str, MAX_PATH, L"\\");
    StringCchCat(str, MAX_PATH, HG_WHITELIST_SUBKEY);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, str, 0, KEY_ALL_ACCESS, &key)) {
        svcReportError(L"Failed to open HG whitelist key");
        return ret;
    }

    if (_itow_s(pid, str, 10)) {
        svcReportError(L"Invalid whitelist pid");
        goto done;
    }

    if (!RegDeleteKeyExW(key, str, KEY_ALL_ACCESS, 0))
        ret = HG_SVC_RET_SUCCESS;

done:
    RegCloseKey(key);
    return ret;

}


DWORD WINAPI HidGuardian::pipeServerThread(LPVOID arg) {

    HANDLE pipe;
    SECURITY_ATTRIBUTES sa;
    wchar_t *sddl =
        L"D:"
        L"(A;;GARCSDWDWOFA;;;SY)"   // full control for localsystem
        L"(A;;GARCSDWDWOFA;;;BA)"   // full control for admins
        L"(A;;GRRCFR;;;AU)"         // read for authenticated users
        L"(A;;GARCFA;;;IU)";        // rwx for interactive users

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = false;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL)) {
        svcReportError(L"Error converting pipe security descriptor");
        return 1;
    }

    while (true) {

        pipe =
            CreateNamedPipeW(
                HG_SVC_PIPE,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                sizeof(UINT),
                sizeof(pipeMsg),
                0,
                &sa
            );

        if (pipe == INVALID_HANDLE_VALUE) {
            svcReportError(L"Error creating named pipe server");
            return 1;
        }

        if (!ConnectNamedPipe(pipe, NULL))
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                svcReportError(L"Error connecting named pipe");
                CloseHandle(pipe);
                continue;
            }

        if (!CreateThread(NULL, 0, pipeWorkerThread, (LPVOID)pipe, 0, NULL)) {
            svcReportError(L"Error creating pipe worker thread");
            CloseHandle(pipe);
        }

    }

    return 0;

}

DWORD WINAPI HidGuardian::pipeWorkerThread(LPVOID arg) {

    HANDLE pipe = (HANDLE)arg;
    pipeMsg msg;
    DWORD len;
    UINT resp = HG_SVC_RET_ERROR;

    if (pipe == NULL) {
        svcReportError(L"Pipe worker thread got NULL pipe");
        return 1;
    }

    while (true) {
        
        if (!ReadFile(pipe, &msg, sizeof(msg), &len, NULL))
            break;
        
        if (len != PIPE_MSG_SIZE_PID && len != PIPE_MSG_SIZE_HWID)
            break;

        switch (msg.cmd) {

            case HG_CMD_WHITELIST_ADD:
                resp = svcAddWlist(msg.id.pid);
                break;
            case HG_CMD_WHITELIST_DEL:
                resp = svcDelWlist(msg.id.pid);
                break;
            case HG_CMD_DEVICE_ADD:
                resp = svcAddDevice(msg.id.hwid);
                break;
            case HG_CMD_DEVICE_DEL:
                resp = svcDelDevice(msg.id.hwid);
                break;
            default:
                svcReportError(L"Pipe worker, received unknown cmd");
        }

        if (!WriteFile(pipe, &resp, sizeof(resp), &len, NULL))
            break;

    }

    return 0;

}


