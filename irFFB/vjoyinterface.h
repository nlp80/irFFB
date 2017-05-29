// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the VJOYINTERFACE_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// VJOYINTERFACE_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#pragma once

#define VJOYINTERFACE_API 

///////////////////////////// vJoy device (collection) status ////////////////////////////////////////////
#ifndef VJDSTAT
#define VJDSTAT
enum VjdStat  /* Declares an enumeration data type */
{
    VJD_STAT_OWN,	// The  vJoy Device is owned by this application.
    VJD_STAT_FREE,	// The  vJoy Device is NOT owned by any application (including this one).
    VJD_STAT_BUSY,	// The  vJoy Device is owned by another application. It cannot be acquired by this application.
    VJD_STAT_MISS,	// The  vJoy Device is missing. It either does not exist or the driver is down.
    VJD_STAT_UNKN	// Unknown
}; 

/* Error codes for some of the functions */
#define NO_HANDLE_BY_INDEX				 -1
#define BAD_PREPARSED_DATA				 -2
#define NO_CAPS				 			 -3
#define BAD_N_BTN_CAPS				 	 -4
#define BAD_CALLOC				 	 	 -5
#define BAD_BTN_CAPS				 	 -6
#define BAD_BTN_RANGE				 	 -7
#define BAD_N_VAL_CAPS				 	 -8
#define BAD_ID_RANGE				 	 -9
#define NO_SUCH_AXIS				 	 -10
#define BAD_DEV_STAT				 	 -11
#define NO_DEV_EXIST				 	 -12
#define NO_FILE_EXIST				 	 -13

/* Registry Constants */
#define REG_PARAM		L"SYSTEM\\CurrentControlSet\\services\\vjoy\\Parameters"
#define REG_PARAM_DEV0	L"SYSTEM\\CurrentControlSet\\services\\vjoy\\Parameters\\Device0"
#define REG_PARAM_DEV	L"SYSTEM\\CurrentControlSet\\services\\vjoy\\Parameters\\Device"
#define REG_DEVICE		L"Device"
#define REG_INIT		L"Init"
#define BTN_INIT		L"BTNS"

/* Compatibility definitions */
#define FFB_EFF_CONST 	FFB_EFF_REPORT
#define PFFB_EFF_CONST 	PFFB_EFF_REPORT
#define Ffb_h_Eff_Const Ffb_h_Eff_Report

#define FFB_DATA_MAX_SIZE 128

// Device Axis/POVs/Buttons
struct DEVCTRLS {
    BOOL Init;
    BOOL	Rudder;
    BOOL	Aileron;
    BOOL	AxisX;
    BOOL	AxisY;
    BOOL	AxisZ;
    BOOL	AxisXRot;
    BOOL	AxisYRot;
    BOOL	AxisZRot;
    BOOL	Slider;
    BOOL	Dial;
    BOOL	Wheel;
    BOOL	AxisVX;
    BOOL	AxisVY;
    BOOL	AxisVZ;
    BOOL	AxisVBRX;
    BOOL	AxisVBRY;
    BOOL	AxisVBRZ;
    INT		nButtons;	
    INT		nDescHats;
    INT		nContHats;
};

struct DeviceStat {
    HANDLE      h;				        // Handle to the PDO interface that represents the virtual device
    VjdStat     stat;				    // Status of the device
    JOYSTICK_POSITION_V2 position;		// Current Position of the device
    HDEVNOTIFY  hDeviceNotifyHandle;	// Device Notification Handle
    DEVCTRLS	DeviceControls;			// Structure Holding the data about the device's controls
    PVOID		pPreParsedData;	        // structure contains a top-level collection's preparsed data.
};

struct DEV_INFO {
    BYTE	DeviceID;		// Device ID: Valid values are 1-16
    BYTE	nImplemented;	// Number of implemented device: Valid values are 1-16
    BYTE	isImplemented;	// Is this device implemented?
    BYTE	MaxDevices;		// Maximum number of devices that may be implemented (16)
    BYTE	DriverFFB;		// Does this driver support FFB (False)
    BYTE	DeviceFFB;		// Does this device support FFB (False)
} ;

enum FFBEType // FFB Effect Type
{

    // Effect Type
    ET_NONE		=	0,	  //    No Force
    ET_CONST	=	1,    //    Constant Force
    ET_RAMP		=	2,    //    Ramp
    ET_SQR		=	3,    //    Square
    ET_SINE		=	4,    //    Sine
    ET_TRNGL	=	5,    //    Triangle
    ET_STUP		=	6,    //    Sawtooth Up
    ET_STDN		=	7,    //    Sawtooth Down
    ET_SPRNG	=	8,    //    Spring
    ET_DMPR		=	9,    //    Damper
    ET_INRT		=	10,   //    Inertia
    ET_FRCTN	=	11,   //    Friction
    ET_CSTM		=	12,   //    Custom Force Data
};

enum FFBPType // FFB Packet Type
{
    // Write
    PT_EFFREP	=  HID_ID_EFFREP,	// Usage Set Effect Report
    PT_ENVREP	=  HID_ID_ENVREP,	// Usage Set Envelope Report
    PT_CONDREP	=  HID_ID_CONDREP,	// Usage Set Condition Report
    PT_PRIDREP	=  HID_ID_PRIDREP,	// Usage Set Periodic Report
    PT_CONSTREP	=  HID_ID_CONSTREP,	// Usage Set Constant Force Report
    PT_RAMPREP	=  HID_ID_RAMPREP,	// Usage Set Ramp Force Report
    PT_CSTMREP	=  HID_ID_CSTMREP,	// Usage Custom Force Data Report
    PT_SMPLREP	=  HID_ID_SMPLREP,	// Usage Download Force Sample
    PT_EFOPREP	=  HID_ID_EFOPREP,	// Usage Effect Operation Report
    PT_BLKFRREP	=  HID_ID_BLKFRREP,	// Usage PID Block Free Report
    PT_CTRLREP	=  HID_ID_CTRLREP,	// Usage PID Device Control
    PT_GAINREP	=  HID_ID_GAINREP,	// Usage Device Gain Report
    PT_SETCREP	=  HID_ID_SETCREP,	// Usage Set Custom Force Report

    // Feature
    PT_NEWEFREP	=  HID_ID_NEWEFREP+0x10,	// Usage Create New Effect Report
    PT_BLKLDREP	=  HID_ID_BLKLDREP+0x10,	// Usage Block Load Report
    PT_POOLREP	=  HID_ID_POOLREP+0x10,		// Usage PID Pool Report
};

enum FFBOP
{
    EFF_START	= 1, // EFFECT START
    EFF_SOLO	= 2, // EFFECT SOLO START
    EFF_STOP	= 3, // EFFECT STOP
};

enum FFB_CTRL
{
    CTRL_ENACT		= 1,	// Enable all device actuators.
    CTRL_DISACT		= 2,	// Disable all the device actuators.
    CTRL_STOPALL	= 3,	// Stop All Effects­ Issues a stop on every running effect.
    CTRL_DEVRST		= 4,	// Device Reset– Clears any device paused condition, enables all actuators and clears all effects from memory.
    CTRL_DEVPAUSE	= 5,	// Device Pause– The all effects on the device are paused at the current time step.
    CTRL_DEVCONT	= 6,	// Device Continue– The all effects that running when the device was paused are restarted from their last time step.
};

enum FFB_EFFECTS {
    Constant	= 0x0001,
    Ramp		= 0x0002,
    Square		= 0x0004,
    Sine		= 0x0008,
    Triangle	= 0x0010,
    Sawtooth_Up = 0x0020,
    Sawtooth_Dn = 0x0040,
    Spring		= 0x0080,
    Damper		= 0x0100,
    Inertia		= 0x0200,
    Friction	= 0x0400,
    Custom		= 0x0800,
};

#pragma pack(push,1)
typedef struct _FFB_DATA {
    ULONG	size;
    ULONG	cmd;
    UCHAR	data[FFB_DATA_MAX_SIZE];
} FFB_DATA, * PFFB_DATA;
#pragma pack(pop)

typedef struct _FFB_EFF_CONSTANT { 
    BYTE EffectBlockIndex; 
    LONG Magnitude; 			    // Constant force magnitude: 	-10000 - 10000
} FFB_EFF_CONSTANT, *PFFB_EFF_CONSTANT;

typedef struct _FFB_EFF_RAMP {
    BYTE		EffectBlockIndex;
    LONG 		Start;              // The Normalized magnitude at the start of the effect (-10000 - 10000)
    LONG 		End;                // The Normalized magnitude at the end of the effect	(-10000 - 10000)
} FFB_EFF_RAMP, *PFFB_EFF_RAMP;

//typedef struct _FFB_EFF_CONST {
typedef struct _FFB_EFF_REPORT {
    BYTE		EffectBlockIndex;
    FFBEType	EffectType;
    WORD		Duration;           // Value in milliseconds. 0xFFFF means infinite
    WORD		TrigerRpt;
    WORD		SamplePrd;
    BYTE		Gain;
    BYTE		TrigerBtn;
    BOOL		Polar;              // How to interpret force direction Polar (0-360°) or Cartesian (X,Y)
    union
    {
        BYTE	Direction;          // Polar direction: (0x00-0xFF correspond to 0-360°)
        BYTE	DirX;               // X direction: Positive values are To the right of the center (X); Negative are Two's complement
    };
    BYTE		DirY;               // Y direction: Positive values are below the center (Y); Negative are Two's complement
} FFB_EFF_REPORT, *PFFB_EFF_REPORT;
//} FFB_EFF_CONST, *PFFB_EFF_CONST;

typedef struct _FFB_EFF_OP {
    BYTE		EffectBlockIndex;
    FFBOP		EffectOp;
    BYTE		LoopCount;
} FFB_EFF_OP, *PFFB_EFF_OP;

typedef struct _FFB_EFF_PERIOD {
    BYTE		EffectBlockIndex;
    DWORD		Magnitude;			// Range: 0 - 10000
    LONG 		Offset;				// Range: –10000 - 10000
    DWORD 		Phase;				// Range: 0 - 35999
    DWORD 		Period;				// Range: 0 - 32767
} FFB_EFF_PERIOD, *PFFB_EFF_PERIOD;

typedef struct _FFB_EFF_COND {
    BYTE		EffectBlockIndex;
    BOOL		isY;
    LONG 		CenterPointOffset;  // CP Offset:  Range -­10000 ­- 10000
    LONG 		PosCoeff;           // Positive Coefficient: Range -­10000 ­- 10000
    LONG 		NegCoeff;           // Negative Coefficient: Range -­10000 ­- 10000
    DWORD 		PosSatur;           // Positive Saturation: Range 0 – 10000
    DWORD 		NegSatur;           // Negative Saturation: Range 0 – 10000
    LONG 		DeadBand;           // Dead Band: : Range 0 – 1000
} FFB_EFF_COND, *PFFB_EFF_COND;

typedef struct _FFB_EFF_ENVLP {
    BYTE		EffectBlockIndex;
    DWORD 		AttackLevel;        // The Normalized magnitude of the stating point: 0 - 10000
    DWORD 		FadeLevel;	        // The Normalized magnitude of the stopping point: 0 - 10000
    DWORD 		AttackTime;	        // Time of the attack: 0 - 4294967295
    DWORD 		FadeTime;	        // Time of the fading: 0 - 4294967295
} FFB_EFF_ENVLP, *PFFB_EFF_ENVLP;

#endif

extern "C" {

    ///////////////////////////// vJoy device (collection) Control interface /////////////////////////////////
    /*
        These functions allow writing feeders and other applications that interface with vJoy
        It is assumed that only one vJoy top-device (= Raw PDO) exists.
        This top-level device can have up to 16 siblings (=top-level Reports/collections)
        Each sibling is refered to as a "vJoy Device" and is attributed a unique Report ID (Range: 1-16).

        Naming convetion:
            VJD = vJoy Device
            rID = Report ID
    */
#pragma warning( push )
#pragma warning( disable : 4995 )
    /////	General driver data
    VJOYINTERFACE_API SHORT __cdecl GetvJoyVersion(void);
    VJOYINTERFACE_API BOOL	__cdecl vJoyEnabled(void);
    VJOYINTERFACE_API BOOL	__cdecl	DriverMatch(WORD * DllVer, WORD * DrvVer);
    VJOYINTERFACE_API BOOL	__cdecl	vJoyFfbCap(BOOL * Supported);	// Is this version of vJoy capable of FFB?
    VJOYINTERFACE_API BOOL	__cdecl	GetvJoyMaxDevices(int * n);	// What is the maximum possible number of vJoy devices
    VJOYINTERFACE_API BOOL	__cdecl	GetNumberExistingVJD(int * n);	// What is the number of vJoy devices currently enabled


    /////	vJoy Device properties
    VJOYINTERFACE_API int	__cdecl  GetVJDButtonNumber(UINT rID);	// Get the number of buttons defined in the specified VDJ
    VJOYINTERFACE_API int	__cdecl  GetVJDDiscPovNumber(UINT rID);	// Get the number of descrete-type POV hats defined in the specified VDJ
    VJOYINTERFACE_API int	__cdecl  GetVJDContPovNumber(UINT rID);	// Get the number of descrete-type POV hats defined in the specified VDJ
    VJOYINTERFACE_API BOOL	__cdecl  GetVJDAxisExist(UINT rID, UINT Axis); // Test if given axis defined in the specified VDJ
    VJOYINTERFACE_API BOOL	__cdecl  GetVJDAxisMax(UINT rID, UINT Axis, LONG * Max); // Get logical Maximum value for a given axis defined in the specified VDJ
    VJOYINTERFACE_API BOOL	__cdecl  GetVJDAxisMin(UINT rID, UINT Axis, LONG * Min); // Get logical Minimum value for a given axis defined in the specified VDJ
    VJOYINTERFACE_API enum VjdStat	__cdecl	GetVJDStatus(UINT rID);			// Get the status of the specified vJoy Device.
    // Added in 2.1.6
    VJOYINTERFACE_API BOOL	__cdecl	isVJDExists(UINT rID);					// TRUE if the specified vJoy Device exists																			

    /////	Write access to vJoy Device - Basic
    VJOYINTERFACE_API BOOL	__cdecl	AcquireVJD(UINT rID, HANDLE fEvent, FFB_DATA *pkt); // Acquire the specified vJoy Device.
    VJOYINTERFACE_API VOID	__cdecl	RelinquishVJD(UINT rID);			 // Relinquish the specified vJoy Device.
    VJOYINTERFACE_API BOOL	__cdecl	UpdateVJD(UINT rID, PVOID pData);	 // Update the position data of the specified vJoy Device.

    //// Reset functions
    VJOYINTERFACE_API BOOL	__cdecl	ResetVJD(UINT rID);			// Reset all controls to predefined values in the specified VDJ

#pragma region FFB Function prototypes
        // Added in 2.1.6
    VJOYINTERFACE_API BOOL	__cdecl	IsDeviceFfb(UINT rID);
    VJOYINTERFACE_API BOOL	__cdecl	IsDeviceFfbEffect(UINT rID, UINT Effect);
#pragma endregion

#pragma warning( pop )

} // Namespace vJoyNS