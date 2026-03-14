//---------------------------------------------------------------------------
// AdsComm.cpp - ADS Communication Module Implementation
// BC++ Builder 6 - Dynamic DLL Loading
//---------------------------------------------------------------------------
#pragma hdrstop
#include "AdsComm.h"

//---------------------------------------------------------------------------
#pragma package(smart_init)

//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
TAdsComm::TAdsComm()
{
    m_hDll = NULL;
    m_nPort = 0;
    m_State = ADS_DISCONNECTED;
    m_LastError = "";
    memset(&m_Addr, 0, sizeof(m_Addr));

    // Initialize function pointers
    m_pfnAdsPortOpen = NULL;
    m_pfnAdsPortClose = NULL;
    m_pfnAdsGetLocalAddress = NULL;
    m_pfnAdsSyncReadReq = NULL;
    m_pfnAdsSyncWriteReq = NULL;
    m_pfnAdsSyncReadStateReq = NULL;
    m_pfnAdsSyncReadDeviceInfoReq = NULL;
    m_pfnAdsGetDllVersion = NULL;

    // Load DLL
    if (!LoadDll())
    {
        m_LastError = "TcAdsDll.dll load failed";
    }
}

//---------------------------------------------------------------------------
// Destructor
//---------------------------------------------------------------------------
TAdsComm::~TAdsComm()
{
    Disconnect();
    UnloadDll();
}

//---------------------------------------------------------------------------
// Load DLL
//---------------------------------------------------------------------------
bool TAdsComm::LoadDll()
{
    // Already loaded
    if (m_hDll != NULL)
        return true;

    // Load DLL
	m_hDll = LoadLibrary("TcAdsDll.dll");

	if (m_hDll == NULL)
	{
    	DWORD dwErr = ::GetLastError();
	    m_LastError = "TcAdsDll.dll load failed (WinErr="
                  + IntToStr((int)dwErr) + ")";
    	// 126 = 모듈을 찾을 수 없음 (파일 자체가 없음)
	    // 193 = 올바른 Win32 응용 프로그램이 아님 (32/64bit 불일치)
	    // 127 = 프로시저를 찾을 수 없음 (의존성 DLL 누락)
    	return false;
	}

    /*
    // Get function pointers
    m_pfnAdsPortOpen = (TFN_AdsPortOpen)GetProcAddress(m_hDll, "AdsPortOpen");
    m_pfnAdsPortClose = (TFN_AdsPortClose)GetProcAddress(m_hDll, "AdsPortClose");
    m_pfnAdsGetLocalAddress = (TFN_AdsGetLocalAddress)GetProcAddress(m_hDll, "AdsGetLocalAddress");
    m_pfnAdsSyncReadReq = (TFN_AdsSyncReadReq)GetProcAddress(m_hDll, "AdsSyncReadReq");
    m_pfnAdsSyncWriteReq = (TFN_AdsSyncWriteReq)GetProcAddress(m_hDll, "AdsSyncWriteReq");
    m_pfnAdsSyncReadStateReq = (TFN_AdsSyncReadStateReq)GetProcAddress(m_hDll, "AdsSyncReadStateReq");
    m_pfnAdsSyncReadDeviceInfoReq = (TFN_AdsSyncReadDeviceInfoReq)GetProcAddress(m_hDll, "AdsSyncReadDeviceInfoReq");
    m_pfnAdsGetDllVersion = (TFN_AdsGetDllVersion)GetProcAddress(m_hDll, "AdsGetDllVersion");
    */

	m_pfnAdsPortOpen = (TFN_AdsPortOpen)GetProcAddress(m_hDll, "_AdsPortOpen@0");
	m_pfnAdsPortClose = (TFN_AdsPortClose)GetProcAddress(m_hDll, "_AdsPortClose@0");
	m_pfnAdsGetLocalAddress = (TFN_AdsGetLocalAddress)GetProcAddress(m_hDll, "_AdsGetLocalAddress@4");
	m_pfnAdsSyncReadReq = (TFN_AdsSyncReadReq)GetProcAddress(m_hDll, "_AdsSyncReadReq@20");
	m_pfnAdsSyncWriteReq = (TFN_AdsSyncWriteReq)GetProcAddress(m_hDll, "_AdsSyncWriteReq@20");
	m_pfnAdsSyncReadStateReq = (TFN_AdsSyncReadStateReq)GetProcAddress(m_hDll, "_AdsSyncReadStateReq@12");
	m_pfnAdsSyncReadDeviceInfoReq = (TFN_AdsSyncReadDeviceInfoReq)GetProcAddress(m_hDll, "_AdsSyncReadDeviceInfoReq@12");
	m_pfnAdsGetDllVersion = (TFN_AdsGetDllVersion)GetProcAddress(m_hDll, "_AdsGetDllVersion@0");

    /*
    // === 디버그: 어떤 함수를 못 찾는지 확인 ===
	AnsiString sDebug = "GetProcAddress results: ";
	sDebug += AnsiString("PortOpen=") + IntToStr((int)(m_pfnAdsPortOpen != NULL));
	sDebug += " PortClose=" + IntToStr((int)(m_pfnAdsPortClose != NULL));
	sDebug += " ReadReq=" + IntToStr((int)(m_pfnAdsSyncReadReq != NULL));
	sDebug += " ReadState=" + IntToStr((int)(m_pfnAdsSyncReadStateReq != NULL));
	sDebug += " WriteReq=" + IntToStr((int)(m_pfnAdsSyncWriteReq != NULL));
	sDebug += " DevInfo=" + IntToStr((int)(m_pfnAdsSyncReadDeviceInfoReq != NULL));
	sDebug += " DllVer=" + IntToStr((int)(m_pfnAdsGetDllVersion != NULL));
	sDebug += " LocalAddr=" + IntToStr((int)(m_pfnAdsGetLocalAddress != NULL));
	OutputDebugString(sDebug.c_str());  // DebugView로 확인
	// 또는 MessageBox로 바로 확인:
	ShowMessage(sDebug);
    */

    // Check required functions
    if (!m_pfnAdsPortOpen || !m_pfnAdsPortClose ||
        !m_pfnAdsSyncReadReq || !m_pfnAdsSyncReadStateReq)
    {
        m_LastError = "DLL function not found";
        UnloadDll();
        return false;
    }

    return true;
}

//---------------------------------------------------------------------------
// Unload DLL
//---------------------------------------------------------------------------
void TAdsComm::UnloadDll()
{
    if (m_hDll != NULL)
    {
        FreeLibrary(m_hDll);
        m_hDll = NULL;
    }

    m_pfnAdsPortOpen = NULL;
    m_pfnAdsPortClose = NULL;
    m_pfnAdsGetLocalAddress = NULL;
    m_pfnAdsSyncReadReq = NULL;
    m_pfnAdsSyncWriteReq = NULL;
    m_pfnAdsSyncReadStateReq = NULL;
    m_pfnAdsSyncReadDeviceInfoReq = NULL;
    m_pfnAdsGetDllVersion = NULL;
}

//---------------------------------------------------------------------------
// Get ADS Error Text
//---------------------------------------------------------------------------
AnsiString TAdsComm::GetAdsErrorText(long nErr)
{
    switch(nErr)
    {
        case 0:     return "OK";
        case 1:     return "Internal error";
        case 2:     return "No runtime";
        case 3:     return "Allocation locked";
        case 4:     return "Insert mailbox error";
        case 5:     return "Wrong HMSG";
        case 6:     return "Target port not found";
        case 7:     return "Target machine not found";
        case 8:     return "Unknown command ID";
        case 9:     return "Bad task ID";
        case 10:    return "No IO";
        case 11:    return "Unknown AMS command";
        case 12:    return "Win32 error";
        case 13:    return "Port not connected";
        case 14:    return "Invalid AMS length";
        case 15:    return "Invalid AMS Net ID";
        case 16:    return "Low installation level";
        case 17:    return "No debug available";
        case 18:    return "Port disabled";
        case 19:    return "Port already connected";
        case 20:    return "AMS Sync Win32 error";
        case 21:    return "AMS Sync timeout";
        case 22:    return "AMS Sync AMS error";
        case 23:    return "AMS Sync no index map";
        case 24:    return "Invalid AMS port";
        case 25:    return "No memory";
        case 26:    return "TCP send error";
        case 27:    return "Host unreachable";
        case 0x700: return "Device error";
        case 0x701: return "Service not supported";
        case 0x702: return "Invalid index group";
        case 0x703: return "Invalid index offset";
        case 0x704: return "Reading/writing not permitted";
        case 0x705: return "Parameter size not correct";
        case 0x706: return "Invalid parameter value";
        case 0x707: return "Device not ready";
        case 0x708: return "Device busy";
        case 0x709: return "Invalid context";
        case 0x70A: return "Out of memory";
        case 0x70B: return "Invalid parameter value";
        case 0x70C: return "Not found";
        case 0x70D: return "Syntax error";
        case 0x70E: return "Objects do not match";
        case 0x70F: return "Object already exists";
        case 0x710: return "Symbol not found";
        case 0x711: return "Symbol version invalid";
        case 0x712: return "Server is in invalid state";
        case 0x713: return "AdsTransMode not supported";
        case 0x714: return "Notification handle is invalid";
        case 0x715: return "Notification client not registered";
        case 0x716: return "No more notification handles";
        case 0x717: return "Size for watch too big";
        case 0x718: return "Device not initialized";
        case 0x719: return "Device has a timeout";
        case 0x71A: return "Query interface failed";
        case 0x71B: return "Wrong interface required";
        case 0x71C: return "Class ID is invalid";
        case 0x71D: return "Object ID is invalid";
        case 0x71E: return "Request is pending";
        case 0x71F: return "Request is aborted";
        case 0x720: return "Signal warning";
        case 0x721: return "Invalid array index";
        case 0x722: return "Symbol not active";
        case 0x723: return "Access denied";
        default:    return "Unknown error (" + IntToStr(nErr) + ")";
    }
}

//---------------------------------------------------------------------------
// Connect to ADS
//---------------------------------------------------------------------------
bool TAdsComm::Connect(const AnsiString& AmsNetId, int Port)
{
    // Check DLL
    if (!m_hDll)
    {
        m_LastError = "DLL not loaded";
        m_State = ADS_ERROR;
        return false;
    }

    // Disconnect if already connected
    if (m_nPort != 0)
        Disconnect();

    // Open ADS port
    m_nPort = m_pfnAdsPortOpen();
    if (m_nPort == 0)
    {
        m_LastError = "AdsPortOpen failed";
        m_State = ADS_ERROR;
        return false;
    }

    // Parse AMS Net ID (e.g., "10.130.131.2.1.1")
    int b[6] = {0};
    if (sscanf(AmsNetId.c_str(), "%d.%d.%d.%d.%d.%d",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
    {
        m_LastError = "Invalid AMS Net ID format";
        m_pfnAdsPortClose();
        m_nPort = 0;
        m_State = ADS_ERROR;
        return false;
    }

    // Set address
    m_Addr.netId.b[0] = (unsigned char)b[0];
    m_Addr.netId.b[1] = (unsigned char)b[1];
    m_Addr.netId.b[2] = (unsigned char)b[2];
    m_Addr.netId.b[3] = (unsigned char)b[3];
    m_Addr.netId.b[4] = (unsigned char)b[4];
    m_Addr.netId.b[5] = (unsigned char)b[5];
    m_Addr.port = (unsigned short)Port;

    // Test connection - Read device state
    unsigned short nAdsState, nDeviceState;
    long nErr = m_pfnAdsSyncReadStateReq(&m_Addr, &nAdsState, &nDeviceState);
    if (nErr)
    {
        m_LastError = "Connection test failed: " + GetAdsErrorText(nErr);
        m_pfnAdsPortClose();
        m_nPort = 0;
        m_State = ADS_ERROR;
        return false;
    }

    m_State = ADS_CONNECTED;
    m_LastError = "";
    return true;
}

//---------------------------------------------------------------------------
// Disconnect from ADS
//---------------------------------------------------------------------------
void TAdsComm::Disconnect()
{
    if (m_nPort != 0 && m_pfnAdsPortClose)
    {
        m_pfnAdsPortClose();
        m_nPort = 0;
    }
    m_State = ADS_DISCONNECTED;
}

//---------------------------------------------------------------------------
// Read BOOL
//---------------------------------------------------------------------------
bool TAdsComm::ReadBool(unsigned long IGroup, unsigned long IOffset, bool& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    unsigned char bVal = 0;
    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(bVal), &bVal);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }

    Value = (bVal != 0);
    return true;
}

//---------------------------------------------------------------------------
// Read BYTE
//---------------------------------------------------------------------------
bool TAdsComm::ReadByte(unsigned long IGroup, unsigned long IOffset, unsigned char& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read INT (16bit)
//---------------------------------------------------------------------------
bool TAdsComm::ReadInt(unsigned long IGroup, unsigned long IOffset, short& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read UINT (16bit unsigned)
//---------------------------------------------------------------------------
bool TAdsComm::ReadUInt(unsigned long IGroup, unsigned long IOffset, unsigned short& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read DINT (32bit)
//---------------------------------------------------------------------------
bool TAdsComm::ReadDInt(unsigned long IGroup, unsigned long IOffset, long& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read REAL (32bit float)
//---------------------------------------------------------------------------
bool TAdsComm::ReadReal(unsigned long IGroup, unsigned long IOffset, float& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read LREAL (64bit double)
//---------------------------------------------------------------------------
bool TAdsComm::ReadLReal(unsigned long IGroup, unsigned long IOffset, double& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read STRING
//---------------------------------------------------------------------------
bool TAdsComm::ReadString(unsigned long IGroup, unsigned long IOffset, int MaxLen, AnsiString& Value)
{
    if (!m_pfnAdsSyncReadReq) return false;

    char* buffer = new char[MaxLen + 1];
    memset(buffer, 0, MaxLen + 1);

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, MaxLen, buffer);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        delete[] buffer;
        return false;
    }

    buffer[MaxLen] = '\0';
    Value = AnsiString(buffer);
    delete[] buffer;
    return true;
}

//---------------------------------------------------------------------------
// Write BOOL
//---------------------------------------------------------------------------
bool TAdsComm::WriteBool(unsigned long IGroup, unsigned long IOffset, bool Value)
{
    if (!m_pfnAdsSyncWriteReq) return false;

    unsigned char bVal = Value ? 1 : 0;
    long nErr = m_pfnAdsSyncWriteReq(&m_Addr, IGroup, IOffset, sizeof(bVal), &bVal);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Write INT
//---------------------------------------------------------------------------
bool TAdsComm::WriteInt(unsigned long IGroup, unsigned long IOffset, short Value)
{
    if (!m_pfnAdsSyncWriteReq) return false;

    long nErr = m_pfnAdsSyncWriteReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Write DINT
//---------------------------------------------------------------------------
bool TAdsComm::WriteDInt(unsigned long IGroup, unsigned long IOffset, long Value)
{
    if (!m_pfnAdsSyncWriteReq) return false;

    long nErr = m_pfnAdsSyncWriteReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Write REAL
//---------------------------------------------------------------------------
bool TAdsComm::WriteReal(unsigned long IGroup, unsigned long IOffset, float Value)
{
    if (!m_pfnAdsSyncWriteReq) return false;

    long nErr = m_pfnAdsSyncWriteReq(&m_Addr, IGroup, IOffset, sizeof(Value), &Value);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Read Raw Data
//---------------------------------------------------------------------------
bool TAdsComm::ReadRaw(unsigned long IGroup, unsigned long IOffset, void* pData, unsigned long Size)
{
    if (!m_pfnAdsSyncReadReq) return false;

    long nErr = m_pfnAdsSyncReadReq(&m_Addr, IGroup, IOffset, Size, pData);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Write Raw Data
//---------------------------------------------------------------------------
bool TAdsComm::WriteRaw(unsigned long IGroup, unsigned long IOffset, void* pData, unsigned long Size)
{
    if (!m_pfnAdsSyncWriteReq) return false;

    long nErr = m_pfnAdsSyncWriteReq(&m_Addr, IGroup, IOffset, Size, pData);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}

//---------------------------------------------------------------------------
// Get Device Info
//---------------------------------------------------------------------------
bool TAdsComm::GetDeviceInfo(AnsiString& DeviceName, unsigned short& Version)
{
    if (!m_pfnAdsSyncReadDeviceInfoReq) return false;

    char szDevName[50] = {0};
    AdsVersion adsVersion;
    memset(&adsVersion, 0, sizeof(adsVersion));

    long nErr = m_pfnAdsSyncReadDeviceInfoReq(&m_Addr, szDevName, &adsVersion);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }

    DeviceName = AnsiString(szDevName);
    Version = adsVersion.version;
    return true;
}

//---------------------------------------------------------------------------
// Get Device State
//---------------------------------------------------------------------------
bool TAdsComm::GetState(unsigned short& AdsState, unsigned short& DeviceState)
{
    if (!m_pfnAdsSyncReadStateReq) return false;

    long nErr = m_pfnAdsSyncReadStateReq(&m_Addr, &AdsState, &DeviceState);
    if (nErr)
    {
        m_LastError = GetAdsErrorText(nErr);
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------
