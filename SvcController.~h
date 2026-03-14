//---------------------------------------------------------------------------
#ifndef SvcControllerH
#define SvcControllerH
//---------------------------------------------------------------------------
#include <SysUtils.hpp>
#include <Classes.hpp>
#include <SvcMgr.hpp>
#include <vcl.h>
#include "VaClasses.hpp"
#include "VaComm.hpp"
#include <ExtCtrls.hpp>
#include <IniFiles.hpp>

// ADS Communication Module (verified on equipment PC)
#include "AdsComm.h"

// Protocol constants (shared with ESP32 - same as GA3)
#define PROTO_STX       0x02
#define PROTO_ETX       0x03
#define MAX_ADS_ITEMS   500

// Response codes (ESP32 -> Agent - same as GA3)
#define RESP_CMD_ACK    0x01
#define RESP_CMD_NAK    0x02
#define RESP_STATUS_OK  0x00
#define RESP_STATUS_CHK 0x01
#define RESP_STATUS_LEN 0x02
#define RESP_STATUS_TMO 0x03
#define RESP_TIMEOUT_MS 5000

#define HK_DEBUG        0       // 1=verbose hex dump in log

//---------------------------------------------------------------------------
// ADS Item Info Structure
//
// [Replaces GA3's TOPCItemInfo]
//   GA3: OPCItem* pointer, VARIANT varValue/varPrevValue
//   AH221: IGroup/IOffset addressing, long lValue/lPrevValue
//   No COM/VARIANT overhead.
//---------------------------------------------------------------------------
struct TAdsItemInfo
{
    int         ItemID;         // Unique ID for packet protocol
    String      VarName;        // PLC variable name (for logging)
    String      DataType;       // BOOL, BYTE, INT, UINT, DINT, REAL, LREAL
    String      Description;    // Human-readable description
    unsigned long IGroup;       // ADS Index Group (typically 0x4020)
    unsigned long IOffset;      // ADS Index Offset (byte offset)
    long        lValue;         // Current value (as long for packet)
    long        lPrevValue;     // Previous value (for change detection)
    int         Quality;        // 0=Good, 9=Error
    bool        Changed;        // Changed since last successful send
};

//---------------------------------------------------------------------------
class TSCM_AH221Agent : public TService
{
__published:
    TVaComm *Mycomm;
    TTimer *Timer1;

	void __fastcall Timer1Timer(TObject *Sender);
    void __fastcall ServiceStart(TService *Sender, bool &Started);
    void __fastcall ServiceStop(TService *Sender, bool &Stopped);

private:
    // === ADS Communication (replaces OPC server/group/items) ===
    TAdsComm*   m_pAds;

    // === INI Settings ===
    String      m_sAmsNetId;        // AMS Net ID
    int         m_nAdsPort;         // ADS Port (default: 801)
    int         m_nComPort;         // COM port number
    int         m_nBaudRate;        // Baud rate
    int         m_nTimeInterval;    // Timer interval (ms)

    // === Item Array (ADS version) ===
    TAdsItemInfo m_Items[MAX_ADS_ITEMS];
    int          m_ItemCount;

    // === Serial Communication (TVaComm - same as GA3) ===
    bool        m_bCommOpened;
    BYTE        m_SendBuffer[4096];
    bool        m_bFirstSend;

    // === Response/Retry ===
    int         m_nRetryCount;
    int         m_nMaxRetries;
    bool        m_bWaitingResponse;
    DWORD       m_dwLastSendTick;
    DWORD       m_dwHeartbeatInterval;

    // === Log ===
    TCHAR       gbuf[65535];

    // --- Logging ---
    void __fastcall LogMessage(String msg);

    // --- Settings ---
    void __fastcall LoadSettings();

    // --- CSV Config (6-column ADS format) ---
    bool __fastcall LoadItemConfig(String filename);

    // --- Serial Port (TVaComm - same as GA3) ---
    bool __fastcall InitSerialPort(int portNum, int baudRate);
    void __fastcall CloseSerialPort();

    // --- ADS Data Read (replaces OPC Read) ---
    bool __fastcall ReadAdsItem(int index);

    // --- Packet Protocol (same as GA3) ---
    BYTE __fastcall CalcChecksum(BYTE* data, int len);
    int  __fastcall BuildPacket(BYTE* buffer);
    void __fastcall SendToESP32(int changeCount = 0, bool isHeartbeat = false);

    // --- Change Detection ---
    bool __fastcall IsValueChanged(int index);

    // --- Response Handling (same as GA3) ---
    bool __fastcall WaitForResponse(int timeoutMs);
    void __fastcall HandleSendFailure();

public:
    __fastcall TSCM_AH221Agent(TComponent* Owner);
    TServiceController __fastcall GetServiceController(void);

    friend void __stdcall ServiceController(unsigned CtrlCode);
};
//---------------------------------------------------------------------------
extern PACKAGE TSCM_AH221Agent *SCM_AH221Agent;
//---------------------------------------------------------------------------
#endif
