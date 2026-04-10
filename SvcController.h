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
#include <ADODB.hpp>        // ADO: replaces AdsComm.h

// Protocol constants (shared with ESP32 - same as GA3)
#define PROTO_STX       0x02
#define PROTO_ETX       0x03
#define MAX_SQL_ITEMS   500

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
// SQL Item Info Structure
//
// [Replaces TAdsItemInfo]
//   ADS version: IGroup/IOffset for PLC memory addressing
//   SQL version: data comes from DB_PROEDGE tables, already processed
//   by ProEdge Server. Same lValue/lPrevValue for packet building.
//---------------------------------------------------------------------------
struct TSqlItemInfo
{
    int         ItemID;         // Unique ID for packet protocol
    String      VarName;        // Column name (for logging)
    String      DataType;       // INT, FLOAT, STRING
    String      Description;    // Human-readable description
    String      TableName;      // Source table in DB_PROEDGE
    long        lValue;         // Current value (as long for packet)
    long        lPrevValue;     // Previous value (for change detection)
    String      sStrValue;      // String value (for STRING type)
    String      sStrPrevValue;  // Previous string value (for change detection)
    int         Quality;        // 0xC0=Good, 0x00=Bad (OPC DA compatible)
    bool        Changed;        // Changed since last successful send
};

//---------------------------------------------------------------------------
// Polling Group IDs (for time-tiered SQL polling)
//---------------------------------------------------------------------------
enum TPollGroup {
    pgDailyReport = 0,      // Group A: daily OEE totals (5s)
    pgProductionReport,     // Group B: per-panel production (5s)
    pgItemsDataHistory,     // Group C: alarm/state history (30s)
    pgProductionBatch,      // Group D: batch summary (60s)
    pgCOUNT                 // sentinel (=4)
};

struct TPollGroupConfig
{
    String  sName;          // INI key / log label
    int     nIntervalSec;   // Poll interval in seconds
    int     nCycleCount;    // Cycles elapsed since last poll
    int     nCyclesNeeded;  // nIntervalSec / (BaseInterval/1000)
    bool    bEnabled;       // From INI
    bool    bDataReady;     // New data polled this cycle
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
    // === SQL Server (ADO) - replaces TAdsComm* m_pAds ===
    TADOConnection *m_pADOConn;
    TADOQuery      *m_pADOQuery;
    String          m_sConnString;
    bool            m_bSqlConnected;

    // === INI Settings ===
    String      m_sSqlServer;       // replaces m_sAmsNetId
    String      m_sSqlDatabase;     // replaces m_nAdsPort
    String      m_sSqlUser;
    String      m_sSqlPassword;
    String      m_sSqlProvider;
    int         m_nComPort;
    int         m_nBaudRate;
    int         m_nTimeInterval;    // Base timer interval (ms)

    // === Item Array (SQL version) ===
    TSqlItemInfo m_Items[MAX_SQL_ITEMS];
    int          m_ItemCount;

    // === Polling Groups ===
    TPollGroupConfig m_Groups[pgCOUNT];
    void __fastcall InitPollGroups();
    bool __fastcall ShouldPollGroup(int grpIdx);

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

    // === History tracking ===
    String      m_sLastHistoryDate;

    // --- Logging (same as GA3) ---
    void __fastcall LogMessage(String msg);

    // --- Settings ---
    void __fastcall LoadSettings();

    // --- SQL Connection ---
    bool __fastcall ConnectSQL();
    void __fastcall DisconnectSQL();
    bool __fastcall ExecuteQuery(String sql);

    // --- Per-group poll functions ---
    bool __fastcall PollDailyReport();
    bool __fastcall PollProductionReport();
    bool __fastcall PollItemsDataHistory();
    bool __fastcall PollProductionBatch();

    // --- Serial Port (TVaComm - same as GA3) ---
    bool __fastcall InitSerialPort(int portNum, int baudRate);
    void __fastcall CloseSerialPort();

    // --- Packet Protocol (same as GA3) ---
    BYTE __fastcall CalcChecksum(BYTE* data, int len);
    int  __fastcall BuildPacket(BYTE* buffer);
    int  __fastcall BuildStringPacket(BYTE* buffer);
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
