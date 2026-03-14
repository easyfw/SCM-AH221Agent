//---------------------------------------------------------------------------
// AdsComm.h - ADS Communication Module for SCM-EdgeDouble
// BC++ Builder 6 - Dynamic DLL Loading
//---------------------------------------------------------------------------
#ifndef AdsCommH
#define AdsCommH

#include <vcl.h>
#include <windows.h>
#include <stdio.h>

//---------------------------------------------------------------------------
// ADS Type Definitions
//---------------------------------------------------------------------------
#pragma pack(push, 1)

// AMS Net ID Structure
typedef struct {
    unsigned char b[6];
} AmsNetId;

// AMS Address Structure
typedef struct {
    AmsNetId netId;
    unsigned short port;
} AmsAddr;

// ADS Version Structure
typedef struct {
    unsigned char version;
    unsigned char revision;
    unsigned short build;
} AdsVersion;

#pragma pack(pop)

//---------------------------------------------------------------------------
// ADS Function Pointer Types
//---------------------------------------------------------------------------
typedef long (__stdcall *TFN_AdsPortOpen)(void);
typedef long (__stdcall *TFN_AdsPortClose)(void);
typedef long (__stdcall *TFN_AdsGetLocalAddress)(AmsAddr* pAddr);

typedef long (__stdcall *TFN_AdsSyncReadReq)(
    AmsAddr* pAddr,
    unsigned long indexGroup,
    unsigned long indexOffset,
    unsigned long length,
    void* pData
);

typedef long (__stdcall *TFN_AdsSyncWriteReq)(
    AmsAddr* pAddr,
    unsigned long indexGroup,
    unsigned long indexOffset,
    unsigned long length,
    void* pData
);

typedef long (__stdcall *TFN_AdsSyncReadStateReq)(
    AmsAddr* pAddr,
    unsigned short* pAdsState,
    unsigned short* pDeviceState
);

typedef long (__stdcall *TFN_AdsSyncReadDeviceInfoReq)(
    AmsAddr* pAddr,
    char* pDevName,
    AdsVersion* pVersion
);

typedef long (__stdcall *TFN_AdsGetDllVersion)(void);

//---------------------------------------------------------------------------
// ADS Connection State
//---------------------------------------------------------------------------
enum TAdsConnState {
    ADS_DISCONNECTED = 0,
    ADS_CONNECTED,
    ADS_ERROR
};

//---------------------------------------------------------------------------
// TAdsComm Class - ADS Communication Wrapper (Dynamic Loading)
//---------------------------------------------------------------------------
class TAdsComm
{
private:
    // DLL Handle
    HMODULE m_hDll;

    // Function Pointers
    TFN_AdsPortOpen         m_pfnAdsPortOpen;
    TFN_AdsPortClose        m_pfnAdsPortClose;
    TFN_AdsGetLocalAddress  m_pfnAdsGetLocalAddress;
    TFN_AdsSyncReadReq      m_pfnAdsSyncReadReq;
    TFN_AdsSyncWriteReq     m_pfnAdsSyncWriteReq;
    TFN_AdsSyncReadStateReq m_pfnAdsSyncReadStateReq;
    TFN_AdsSyncReadDeviceInfoReq m_pfnAdsSyncReadDeviceInfoReq;
    TFN_AdsGetDllVersion    m_pfnAdsGetDllVersion;

    // Connection Info
    long m_nPort;
    AmsAddr m_Addr;
    TAdsConnState m_State;
    AnsiString m_LastError;

    bool LoadDll();
    void UnloadDll();
    AnsiString GetAdsErrorText(long nErr);

public:
    TAdsComm();
    ~TAdsComm();

    // DLL Load Status
    bool IsDllLoaded() { return m_hDll != NULL; }

    // Connection Management
    bool Connect(const AnsiString& AmsNetId, int Port = 801);
    void Disconnect();
    bool IsConnected() { return m_State == ADS_CONNECTED; }
    TAdsConnState GetState() { return m_State; }
    AnsiString GetLastError() { return m_LastError; }

    // Data Read
    bool ReadBool(unsigned long IGroup, unsigned long IOffset, bool& Value);
    bool ReadByte(unsigned long IGroup, unsigned long IOffset, unsigned char& Value);
    bool ReadInt(unsigned long IGroup, unsigned long IOffset, short& Value);
    bool ReadUInt(unsigned long IGroup, unsigned long IOffset, unsigned short& Value);
    bool ReadDInt(unsigned long IGroup, unsigned long IOffset, long& Value);
    bool ReadReal(unsigned long IGroup, unsigned long IOffset, float& Value);
    bool ReadLReal(unsigned long IGroup, unsigned long IOffset, double& Value);
    bool ReadString(unsigned long IGroup, unsigned long IOffset, int MaxLen, AnsiString& Value);

    // Data Write
    bool WriteBool(unsigned long IGroup, unsigned long IOffset, bool Value);
    bool WriteInt(unsigned long IGroup, unsigned long IOffset, short Value);
    bool WriteDInt(unsigned long IGroup, unsigned long IOffset, long Value);
    bool WriteReal(unsigned long IGroup, unsigned long IOffset, float Value);

    // Raw Data Read/Write
    bool ReadRaw(unsigned long IGroup, unsigned long IOffset, void* pData, unsigned long Size);
    bool WriteRaw(unsigned long IGroup, unsigned long IOffset, void* pData, unsigned long Size);

    // Device Info
    bool GetDeviceInfo(AnsiString& DeviceName, unsigned short& Version);
    bool GetState(unsigned short& AdsState, unsigned short& DeviceState);
};

//---------------------------------------------------------------------------
// SCM-EdgeDouble Constants
//---------------------------------------------------------------------------
namespace EdgeDouble
{
    // AMS Net ID
    const char* const AMS_NET_ID = "10.130.131.2.1.1";
    const int ADS_PORT = 801;

    // Index Group (PLC Memory Area)
    const unsigned long IG_PLC_MEM = 0x4020;  // 16448

    // Index Offsets
    const unsigned long IO_cMaxPann = 584;
    const unsigned long IO_cMaxPrg = 588;
    const unsigned long IO_cMaxAlm = 590;
    const unsigned long IO_cMaxWrn = 592;
    const unsigned long IO_cMaxMsg = 594;
    const unsigned long IO_cMaxDataPrg = 596;
    const unsigned long IO_cMaxGapArray = 598;
    const unsigned long IO_cMaxNRot = 600;
    const unsigned long IO_cMaxGrp = 602;
    const unsigned long IO_cMaxData = 604;
    const unsigned long IO_cMaxDataProg = 606;
    const unsigned long IO_cMaxDataProgValue = 608;
    const unsigned long IO_cMaxStation = 612;
    const unsigned long IO_cMaxTerminals = 626;
}

#endif
