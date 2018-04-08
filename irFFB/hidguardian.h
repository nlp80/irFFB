#pragma once

#include "stdafx.h"
#include "irFFB.h"

#include <winhttp.h>
#include <cfgmgr32.h>
#include <comdef.h>
#include <RegStr.h>
#include <newdev.h>
#include <devguid.h>
#include <sddl.h>

#define STATUS_NOTINSTALLED 0
#define STATUS_DISABLED 1
#define STATUS_ENABLED 2

#define HG_DOWNLOAD_SITE L"github.com"
#define HG_DOWNLOAD_PATH L"/nlp80/irFFB/raw/master/HidGuardian_signed_Win7-10_x86_x64_latest.zip"
#define HG_HARDWARE_ID L"Root\\HidGuardian"
#define HG_FILTER_NAME L"HidGuardian"
#define HG_INSTALLER_NAME L"irFFB_hg64.exe"
#define HG_PARAMS_KEY L"SYSTEM\\CurrentControlSet\\Services\\HidGuardian\\Parameters"
#define HG_WHITELIST_SUBKEY L"Whitelist"
#define HG_DEVICES_VALUE_NAME L"AffectedDevices"
#define HG_HWID_FMT L"HID\\VID_%04hx&PID_%04hx"
#define HG_SVC_PIPE L"\\\\.\\pipe\\irFFB"

#define HG_CMD_WHITELIST_ADD 1
#define HG_CMD_WHITELIST_DEL 2
#define HG_CMD_DEVICE_ADD    3
#define HG_CMD_DEVICE_DEL    4

#define HG_SVC_RET_ERROR   0
#define HG_SVC_RET_SUCCESS 1
#define HG_SVC_RET_EXISTS  2

typedef BOOL(WINAPI *UpdateDriverProto)(HWND, LPCTSTR, LPCTSTR, DWORD, PBOOL);

#define INSTALLER_MAX_ERRORCODE 11

static wchar_t *installerErrors[] = {
    L"Success",
    L"Missing INF path argument",
    L"Failed to get class from INF",
    L"Failed to create devInfo list",
    L"Failed to create devInfo",
    L"Failed to set dev reg property",
    L"Failed to call class installer",
    L"Failed to open class reg key",
    L"Failed to set filter reg value",
    L"Failed to loadlibrary newdev.dll",
    L"Failed to locate UpdateDriverForPlugAndPlayDevices",
    L"UpdateDriverForPlugAndPlayDevices failed"
};

static wchar_t *statuses[] = { 
    L"Not installed", L"Disabled", L"Enabled" 
};

#pragma pack(push, 1)
typedef struct {
    UINT cmd;
    union {
        UINT pid;
        wchar_t hwid[32];
    } id;
} pipeMsg;
#pragma pack(pop)

#define PIPE_MSG_SIZE_PID 2 * sizeof(UINT)
#define PIPE_MSG_SIZE_HWID sizeof(pipeMsg)

class HidGuardian {

public:
    static HidGuardian *init(DWORD);
    ATOM registerClass(HINSTANCE);
    void createWindow(HINSTANCE);
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void install(void);
    bool isEnabled(void);
    void setDevice(WORD, WORD);
    void removeDevice(WORD, WORD, bool);
    void whitelist(DWORD);
    void unWhitelist(DWORD);
    void stop(DWORD);

    static void WINAPI SvcMain(DWORD, LPTSTR);
    static void svcReportError(wchar_t *);

private:
    HidGuardian(DWORD);
    int getStatus(void);
    void setStatus(int);
    int refreshStatus(void);
    void setEnabled(bool);
    
    wchar_t *unpackInstaller64();
    bool exInstaller64(wchar_t *, wchar_t *);
    wchar_t *download(void);
    wchar_t *unzip(wchar_t *);
    bool doInstall(wchar_t *);
    bool installFilter(void);
    bool updateDriver(wchar_t *);
    void cleanupInstall(void);
    bool installService(void);
    int queryService(void);
    bool startService(void);
    void stopService(void);
    void setSvcStatus(int);
    bool sendSvcMsg(pipeMsg *);
    bool isElevated(void);
    void elevate(void);
    void readSettings(void);
    void writeSettings(void);

    static wchar_t *getRegMultiSz(HKEY, LPCTSTR);
    static wchar_t *prependToMultiSz(wchar_t *, wchar_t *);
    static wchar_t *removeFromMultiSz(wchar_t *, wchar_t *);
    static size_t multiSzLen(wchar_t *);

    static void WINAPI svcCtrlHandler(DWORD);
    static void svcReportStatus(DWORD, DWORD, DWORD);
    static DWORD WINAPI pipeServerThread(LPVOID);
    static DWORD WINAPI pipeWorkerThread(LPVOID);
    static UINT svcAddDevice(wchar_t *);
    static UINT svcDelDevice(wchar_t *);
    static UINT svcAddWlist(UINT);
    static UINT svcDelWlist(UINT);
    
    static HidGuardian *instance;

    static SERVICE_STATUS_HANDLE svcStatusHandle;
    static SERVICE_STATUS svcStatus;
    static HANDLE svcStopEvent;

    wchar_t *windowClass = L"HidGuardian";
    wchar_t *zipPath = nullptr;
    wchar_t *fldPath = nullptr;
    int status = STATUS_NOTINSTALLED;
    bool classIsRegistered = false;
    bool enabled = true;
    WORD currentVid = 0, currentPid = 0;

    HINSTANCE hInst = nullptr;
    HWND mainWnd, statusWnd, svcWnd, installWnd, enabledWnd;

};