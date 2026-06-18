//---------------------------------------------------------------------------
// SvcController.cpp - SCM-AH221 SQL Agent Service Implementation
//
// [What changed from ADS version]
//   - Removed: AdsComm.h, TAdsComm, ReadAdsItem(), ADS connect/disconnect
//   - Removed: EdgeDouble:: namespace references
//   - Added:   ADO (TADOConnection, TADOQuery) for SQL Server access
//   - Added:   ConnectSQL(), DisconnectSQL(), ExecuteQuery()
//   - Added:   PollDailyReport(), PollProductionReport(), etc.
//   - Added:   Time-tiered polling per table group
//   - Changed: LoadSettings() [ADS] -> [SQL] section
//   - Changed: LoadItemConfig() removed (items registered in code)
//   - Changed: Timer1Timer() SQL poll + change detect + send
//
// [v2 - STRING packet support]
//   - Added:   ProgramCode(ID14), EdgeCodeLH(ID15), EdgeCodeRH(ID16)
//   - Added:   BuildStringPacket() - type marker 0xFE, variable-length strings
//   - Changed: SendToESP32() - sends regular packet then string packet
//   - Changed: IsValueChanged() - STRING comparison via sStrValue
//   - Changed: PollProductionReport() - sStrPrevValue tracking
//   - Note:    ESP32 firmware must check data[3]==0xFE for string packet
//
// [Kept identical to ADS/GA3 version]
//   - TVaComm serial (Mycomm), InitSerialPort, CloseSerialPort
//   - Packet format [STX][LEN][CNT][ID][Q][VAL]...[CHK][ETX]
//   - Change detection, Heartbeat, ACK/NAK
//   - Log file rotation (60KB)
//   - WaitForResponse(), HandleSendFailure(), SendToESP32()
//   - CalcChecksum(), BuildPacket(), IsValueChanged()
//
// [ASCII Only]
//---------------------------------------------------------------------------
#include "SvcController.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma link "VaClasses"
#pragma link "VaComm"
#pragma resource "*.dfm"

TSCM_AH221Agent *SCM_AH221Agent;

// Log file management (same as GA3)
static int g_LogFileIndex = 0;
static bool g_bFirstRun = true;

//---------------------------------------------------------------------------
// Constructor
//---------------------------------------------------------------------------
__fastcall TSCM_AH221Agent::TSCM_AH221Agent(TComponent* Owner)
	: TService(Owner)
{
    this->OnStart = ServiceStart;
    this->OnStop  = ServiceStop;
    lstrcpy(gbuf, "[SCM-AH221 Service Log]\r\n");

    m_pADOConn = NULL;
    m_pADOQuery = NULL;
    m_bSqlConnected = false;
    m_ItemCount = 0;
    m_bCommOpened = false;
    m_bFirstSend = true;

    // Response/Retry
    m_nRetryCount = 0;
    m_nMaxRetries = 3;
    m_bWaitingResponse = false;

    // Heartbeat
    m_dwLastSendTick = 0;
    m_dwHeartbeatInterval = 5000;

    // No-change counter (log suppression)
    m_nNoChangeCount = 0;

    // Default settings
    m_sSqlServer   = ".\\SCMGROUPEXPRESS";
    m_sSqlDatabase = "DB_PROEDGE";
    m_sSqlUser     = "sa";
    m_sSqlPassword = "Passw0rd";
    m_sSqlProvider = "SQLOLEDB";
    m_nComPort = 3;
    m_nBaudRate = 115200;
    m_nTimeInterval = 5000;

    m_sLastHistoryDate = "";

    for (int i = 0; i < pgCOUNT; i++)
    {
        m_Groups[i].nCycleCount = 0;
        m_Groups[i].bDataReady = false;
    }
}

//---------------------------------------------------------------------------
TServiceController __fastcall TSCM_AH221Agent::GetServiceController(void)
{
    return (TServiceController) ServiceController;
}

void __stdcall ServiceController(unsigned CtrlCode)
{
    SCM_AH221Agent->Controller(CtrlCode);
}

//---------------------------------------------------------------------------
// LogMessage (identical to GA3/ADS version)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::LogMessage(String msg)
{
    HANDLE hFile;
    DWORD dwBytesWritten;
    DWORD dwFileSize;
    SYSTEMTIME st;
    String logFileName;

    String exePath = ExtractFilePath(ParamStr(0));
    String logBasePath = exePath + "logsave";

    while (true)
    {
        if (g_LogFileIndex == 0) logFileName = logBasePath + ".txt";
        else logFileName = logBasePath + "_" + IntToStr(g_LogFileIndex) + ".txt";

        hFile = CreateFile(
            logFileName.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (hFile == INVALID_HANDLE_VALUE) return;

        dwFileSize = GetFileSize(hFile, NULL);

        if (dwFileSize > 60000)
        {
            CloseHandle(hFile);
            g_LogFileIndex++;
            g_bFirstRun = false;
            continue;
        }
        break;
    }

    SetFilePointer(hFile, 0, NULL, FILE_END);

    if (g_bFirstRun)
    {
        if (dwFileSize > 0)
        {
            String blankLine = "\r\n";
            WriteFile(hFile, blankLine.c_str(), blankLine.Length(), &dwBytesWritten, NULL);
        }
        g_bFirstRun = false;
    }

    GetLocalTime(&st);

    String timeStr;
    timeStr.printf("[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    AnsiString finalMsg = timeStr + msg + "\r\n";
    WriteFile(hFile, finalMsg.c_str(), finalMsg.Length(), &dwBytesWritten, NULL);
    CloseHandle(hFile);
}

//---------------------------------------------------------------------------
// WriteStatusFile - overwrite single-line status for no-change cycles
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::WriteStatusFile(String msg)
{
    String exePath = ExtractFilePath(ParamStr(0));
    String statusPath = exePath + "logsave_status.txt";

    HANDLE hFile = CreateFile(
        statusPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    String timeStr;
    timeStr.printf("[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    AnsiString finalMsg = timeStr + msg + "\r\n";
    DWORD dwBytesWritten;
    WriteFile(hFile, finalMsg.c_str(), finalMsg.Length(), &dwBytesWritten, NULL);
    CloseHandle(hFile);
}

//---------------------------------------------------------------------------
// LoadSettings
//   [Changed]: [ADS] section -> [SQL] section
//   [Added]:   [PollIntervals] section
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::LoadSettings()
{
    String IniPath = ExtractFilePath(ParamStr(0)) + "oem_setting.ini";

    TIniFile *ini = new TIniFile(IniPath);
    try
    {
        // [SQL] section (replaces [ADS])
        m_sSqlServer   = ini->ReadString("SQL", "Server",   ".\\SCMGROUPEXPRESS");
        m_sSqlDatabase = ini->ReadString("SQL", "Database", "DB_PROEDGE");
        m_sSqlUser     = ini->ReadString("SQL", "User",     "sa");
        m_sSqlPassword = ini->ReadString("SQL", "Password", "Passw0rd");
        m_sSqlProvider = ini->ReadString("SQL", "Provider", "SQLOLEDB");

        m_sConnString = "Provider=" + m_sSqlProvider + ";"
                      + "Data Source=" + m_sSqlServer + ";"
                      + "Initial Catalog=" + m_sSqlDatabase + ";"
                      + "User ID=" + m_sSqlUser + ";"
                      + "Password=" + m_sSqlPassword + ";";

        // [Communication] section (same as GA3)
        String comStr = ini->ReadString("Communication", "COM_Port", "COM3");
        if (comStr.UpperCase().Pos("COM") == 1)
            m_nComPort = StrToIntDef(comStr.SubString(4, comStr.Length() - 3), 3);
        else
            m_nComPort = StrToIntDef(comStr, 3);

        m_nBaudRate = ini->ReadInteger("Communication", "BaudRate", 115200);

        // [Agent] section
        m_nTimeInterval = ini->ReadInteger("Agent", "TimeInterval", 5000);

        // [PollIntervals] section
        int defaults[pgCOUNT] = { 5, 5, 30, 60 };
        String names[pgCOUNT] = { "DailyReport", "ProductionReport",
                                   "ItemsDataHistory", "ProductionBatch" };
        int baseIntervalSec = m_nTimeInterval / 1000;
        if (baseIntervalSec < 1) baseIntervalSec = 1;

        for (int i = 0; i < pgCOUNT; i++)
        {
            m_Groups[i].sName = names[i];
            m_Groups[i].nIntervalSec = ini->ReadInteger("PollIntervals",
                names[i], defaults[i]);
            m_Groups[i].bEnabled = ini->ReadBool("PollIntervals",
                names[i] + "_Enabled", true);
            m_Groups[i].nCyclesNeeded = m_Groups[i].nIntervalSec / baseIntervalSec;
            if (m_Groups[i].nCyclesNeeded < 1) m_Groups[i].nCyclesNeeded = 1;
            m_Groups[i].nCycleCount = m_Groups[i].nCyclesNeeded; // poll first cycle
            m_Groups[i].bDataReady = false;
        }

        LogMessage("CFG: SQL=" + m_sSqlServer + "/" + m_sSqlDatabase
                 + " COM" + IntToStr(m_nComPort)
                 + " " + IntToStr(m_nBaudRate)
                 + " T:" + IntToStr(m_nTimeInterval));
    }
    __finally
    {
        delete ini;
    }
}

//---------------------------------------------------------------------------
// InitPollGroups (called after LoadSettings)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::InitPollGroups()
{
    for (int i = 0; i < pgCOUNT; i++)
    {
        LogMessage("  Poll " + m_Groups[i].sName
                 + ": " + IntToStr(m_Groups[i].nIntervalSec) + "s"
                 + " cyc=" + IntToStr(m_Groups[i].nCyclesNeeded)
                 + (m_Groups[i].bEnabled ? " ON" : " OFF"));
    }
}

//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::ShouldPollGroup(int grpIdx)
{
    if (!m_Groups[grpIdx].bEnabled) return false;
    m_Groups[grpIdx].nCycleCount++;
    if (m_Groups[grpIdx].nCycleCount >= m_Groups[grpIdx].nCyclesNeeded)
    {
        m_Groups[grpIdx].nCycleCount = 0;
        return true;
    }
    return false;
}

//---------------------------------------------------------------------------
// SQL Connection Management
//   [Replaces TAdsComm::Connect/Disconnect]
//   ADO needs CoInitialize (COM subsystem) just like OPC DA did in GA3.
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::ConnectSQL()
{
    try
    {
        if (m_pADOConn == NULL)
        {
            CoInitialize(NULL);
            m_pADOConn = new TADOConnection(NULL);
            m_pADOConn->LoginPrompt = false;
            m_pADOConn->ConnectionTimeout = 10;
            m_pADOConn->CommandTimeout = 15;
            m_pADOQuery = new TADOQuery(NULL);
            m_pADOQuery->Connection = m_pADOConn;
        }
        m_pADOConn->ConnectionString = m_sConnString;
        m_pADOConn->Connected = true;
        m_bSqlConnected = true;
        return true;
    }
    catch (Exception &e)
    {
        LogMessage("SQL CONN: " + e.Message);
        m_bSqlConnected = false;
        return false;
    }
}

void __fastcall TSCM_AH221Agent::DisconnectSQL()
{
    try
    {
        if (m_pADOQuery)
        {
            if (m_pADOQuery->Active) m_pADOQuery->Close();
            delete m_pADOQuery; m_pADOQuery = NULL;
        }
        if (m_pADOConn)
        {
            if (m_pADOConn->Connected) m_pADOConn->Connected = false;
            delete m_pADOConn; m_pADOConn = NULL;
        }
        CoUninitialize();
    }
    catch (Exception &e) { LogMessage("SQL DISC: " + e.Message); }
    m_bSqlConnected = false;
}

/*
bool __fastcall TSCM_AH221Agent::ExecuteQuery(String sql)
{
    try
    {
        if (!m_bSqlConnected) return false;
        m_pADOQuery->Close();
        m_pADOQuery->SQL->Clear();
        m_pADOQuery->SQL->Add(sql);
        m_pADOQuery->Open();
        return true;
    }
    catch (Exception &e)
    {
        LogMessage("SQL QRY: " + e.Message);
        m_bSqlConnected = false;
        try { m_pADOConn->Connected = false; } catch(...) {}
        return false;
    }
}
*/

bool __fastcall TSCM_AH221Agent::ExecuteQuery(String sql)
{
    try
    {
        if (!m_bSqlConnected) return false;
        m_pADOQuery->Close();
        m_pADOQuery->SQL->Clear();
        m_pADOQuery->SQL->Add(sql);
        m_pADOQuery->Open();
        return true;
    }
    catch (Exception &e)
    {
        LogMessage("SQL QRY: " + e.Message);

        // ���� ��ü�� ���� ��츸 �翬�� �÷��� ����
        // �÷��� ���� �� ���� ���� ������ ������ ����
        if (!m_pADOConn->Connected)
        {
            m_bSqlConnected = false;
        }
        return false;
    }
}

//---------------------------------------------------------------------------
// Per-group SQL poll functions
//   [Replaces ReadAdsItem()]
//   Each function queries one table and updates matching items in m_Items[].
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::PollDailyReport()
{
    try
    {
		String sql =
    		"SELECT TOP 1 DayDate, MacOn, MacInStart, MacInAlarm, "
    		"TrackInRun, MacStartAndFull "    // MacInManual -> MacStartAndFull
    		"FROM CustomTable6_DailyReport ORDER BY DayDate DESC";

        if (!ExecuteQuery(sql) || m_pADOQuery->RecordCount == 0) return false;

        for (int i = 0; i < m_ItemCount; i++)
        {
            if (m_Items[i].TableName != "CustomTable6_DailyReport") continue;
            m_Items[i].lPrevValue = m_Items[i].lValue;

            try
            {
                if (m_Items[i].DataType == "STRING")
                    m_Items[i].sStrValue = m_pADOQuery->FieldByName(m_Items[i].VarName)->AsString;
                else
                    m_Items[i].lValue = m_pADOQuery->FieldByName(m_Items[i].VarName)->AsInteger;
                m_Items[i].Quality = 0xC0;  // Good
            }
            catch (...) { m_Items[i].Quality = 0x00; }
        }

        if (HK_DEBUG)
            LogMessage("DR: MacOn=" + m_pADOQuery->FieldByName("MacOn")->AsString);
        return true;
    }
    catch (Exception &e)
    {
        LogMessage("POLL DR: " + e.Message);
        return false;
    }
}

bool __fastcall TSCM_AH221Agent::PollProductionReport()
{
    try
    {
        String sql =
            "SELECT TOP 1 Event, Quantity, L, W, T, "
            "ProgramCode, EdgeCodeLH, EdgeCodeRH "
            "FROM CustomTable5_ProductionReport ORDER BY Event DESC";

        if (!ExecuteQuery(sql) || m_pADOQuery->RecordCount == 0) return false;

        for (int i = 0; i < m_ItemCount; i++)
        {
            if (m_Items[i].TableName != "CustomTable5_ProductionReport") continue;
            m_Items[i].lPrevValue = m_Items[i].lValue;
            m_Items[i].sStrPrevValue = m_Items[i].sStrValue;

            try
			{
			    String raw = m_pADOQuery->FieldByName(m_Items[i].VarName)->AsString;
			    raw = raw.Trim();   // �յ� ���� ���

			    if (m_Items[i].DataType == "STRING")
    			{
        			m_Items[i].sStrValue = raw;
        			m_Items[i].Quality = 0xC0;
    			}
                else if (m_Items[i].DataType == "FLOAT")
    			{
        			// nvarchar "1644" �� float ��ȯ �� ��1000 (���� ������ ����)
			        bool ok = false;
			        double d = 0.0;
			        try { d = StrToFloat(raw); ok = true; }
        			catch (...) { ok = false; }

        			if (ok && !raw.IsEmpty())
                    {
            			m_Items[i].lValue = (long)(d * 1000);
            			m_Items[i].Quality = 0xC0;          // Good
        			}
                    else
                    {
            			m_Items[i].Quality = 0x00;          // ��¥ ��ȯ�Ұ�/�󰪸� Bad
        			}
			    }
			    else  // INT
    			{
        			bool ok = false;
        			int n = 0;
        			try { n = StrToInt(raw); ok = true; }
        			catch (...) { ok = false; }

        			if (ok && !raw.IsEmpty())
                    {
            			m_Items[i].lValue = n;
            			m_Items[i].Quality = 0xC0;
        			}
                    else
                    {
            			m_Items[i].Quality = 0x00;
        			}
    			}
			}
			catch (...) { m_Items[i].Quality = 0x00; }

            /*
            try
            {
                if (m_Items[i].DataType == "STRING")
                    m_Items[i].sStrValue = m_pADOQuery->FieldByName(m_Items[i].VarName)->AsString;
                else if (m_Items[i].DataType == "FLOAT")
                    m_Items[i].lValue = (long)(m_pADOQuery->FieldByName(m_Items[i].VarName)->AsFloat * 1000);
                else
                    m_Items[i].lValue = m_pADOQuery->FieldByName(m_Items[i].VarName)->AsInteger;
                m_Items[i].Quality = 0xC0;
            }
            catch (...) { m_Items[i].Quality = 0x00; }
            */
        }
        return true;
    }
    catch (Exception &e)
    {
        LogMessage("POLL PR: " + e.Message);
        return false;
    }
}

bool __fastcall TSCM_AH221Agent::PollItemsDataHistory()
{
    try
    {
		String sql;
		if (m_sLastHistoryDate.IsEmpty())
		    sql = "SELECT TOP 5 Date, NodeID, Value "
		          "FROM ItemsDataHistory ORDER BY Date DESC";
		else
		    sql = "SELECT TOP 20 Date, NodeID, Value "
		          "FROM ItemsDataHistory "
		          "WHERE Date > '" + m_sLastHistoryDate + "' "
		          "ORDER BY Date DESC";

        if (!ExecuteQuery(sql)) return false;
        if (m_pADOQuery->RecordCount == 0) return true;  // no new events

        // Update items with most recent row
        for (int i = 0; i < m_ItemCount; i++)
        {
            if (m_Items[i].TableName != "ItemsDataHistory") continue;
            m_Items[i].lPrevValue = m_Items[i].lValue;

            try
            {
				if (m_Items[i].VarName == "NodeID")      // ����: "ItemID"
				    m_Items[i].lValue = m_pADOQuery->FieldByName("NodeID")->AsInteger;
                else if (m_Items[i].VarName == "Value")
                    m_Items[i].lValue = StrToIntDef(m_pADOQuery->FieldByName("Value")->AsString, 0);
                m_Items[i].Quality = 0xC0;
            }
            catch (...) { m_Items[i].Quality = 0x00; }
        }

        TDateTime dt = m_pADOQuery->FieldByName("Date")->AsDateTime;
//      m_sLastHistoryDate = FormatDateTime("yyyy-mm-dd'T'hh:nn:ss.zzz", dt);
		Word wy, wmo, wd, wh, wmi, ws, wms;
		dt.DecodeDate(&wy, &wmo, &wd);
		dt.DecodeTime(&wh, &wmi, &ws, &wms);
		m_sLastHistoryDate.printf("%04d-%02d-%02dT%02d:%02d:%02d.%03d", wy, wmo, wd, wh, wmi, ws, wms);

        return true;
    }
    catch (Exception &e)
    {
        LogMessage("POLL HIS: " + e.Message);
        return false;
    }
}

bool __fastcall TSCM_AH221Agent::PollProductionBatch()
{
    try
    {
		String sql = "SELECT TOP 1 BatchCode, PanelCode, QuantityPhase1, "
					 "QuantityPhase2, Quantity "
					 "FROM CustomTable3_Production ORDER BY BatchCode DESC";

        if (!ExecuteQuery(sql) || m_pADOQuery->RecordCount == 0) return false;

        for (int i = 0; i < m_ItemCount; i++)
        {
            if (m_Items[i].TableName != "CustomTable3_Production") continue;
    		m_Items[i].lPrevValue    = m_Items[i].lValue;
		    m_Items[i].sStrPrevValue = m_Items[i].sStrValue;

    		try
		    {
		        String raw = m_pADOQuery->FieldByName(m_Items[i].VarName)->AsString;
		        raw = raw.Trim();

		        if (m_Items[i].DataType == "STRING")
        		{
		            m_Items[i].sStrValue = raw;        // BatchCode / PanelCode
		            m_Items[i].Quality = 0xC0;
		        }
		        else  // INT (Quantity)
    		    {
		            if (!raw.IsEmpty())
		            {
		                try { m_Items[i].lValue = StrToInt(raw); m_Items[i].Quality = 0xC0; }
		                catch (...) { m_Items[i].Quality = 0x00; }
		            }
		            else m_Items[i].Quality = 0x00;
        		}
    		}
		    catch (...) { m_Items[i].Quality = 0x00; }
        }
        return true;
    }
    catch (Exception &e)
    {
        LogMessage("POLL BAT: " + e.Message);
        return false;
    }
}

//---------------------------------------------------------------------------
// InitSerialPort (identical to ADS/GA3 version)
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::InitSerialPort(int portNum, int baudRate)
{
    try
    {
        if (Mycomm == NULL)
        {
            LogMessage("Error: Mycomm component is NULL");
            return false;
        }

        if (Mycomm->Active()) Mycomm->Close();

        Mycomm->PortNum = portNum;

        switch (baudRate)
        {
            case 9600:   Mycomm->Baudrate = br9600;   break;
            case 19200:  Mycomm->Baudrate = br19200;  break;
            case 38400:  Mycomm->Baudrate = br38400;  break;
            case 57600:  Mycomm->Baudrate = br57600;  break;
            case 115200: Mycomm->Baudrate = br115200; break;
            default:     Mycomm->Baudrate = br115200; break;
        }

        Mycomm->Databits = db8;
        Mycomm->Stopbits = sb1;
        Mycomm->Parity = paNone;

        Mycomm->Open();

        if (Mycomm->Active())
        {
            m_bCommOpened = true;
            LogMessage("Serial port COM" + IntToStr(portNum)
                     + " opened at " + IntToStr(baudRate) + " bps");
            return true;
        }
        else
        {
            LogMessage("Failed to open COM" + IntToStr(portNum));
            return false;
        }
    }
    catch (Exception &ex)
    {
        LogMessage("Serial port error: " + ex.Message);
        return false;
    }
}

//---------------------------------------------------------------------------
// CloseSerialPort (identical to GA3)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::CloseSerialPort()
{
    try
    {
        if (Mycomm && Mycomm->Active())
        {
            Mycomm->Close();
            m_bCommOpened = false;
            LogMessage("Serial port closed.");
        }
    }
    catch (Exception &ex)
    {
        LogMessage("Error closing serial port: " + ex.Message);
    }
}

//---------------------------------------------------------------------------
// CalcChecksum (identical to GA3)
//---------------------------------------------------------------------------
BYTE __fastcall TSCM_AH221Agent::CalcChecksum(BYTE* data, int len)
{
    BYTE checksum = 0;
    for (int i = 0; i < len; i++)
        checksum ^= data[i];
    return checksum;
}

//---------------------------------------------------------------------------
// IsValueChanged (same as ADS version + STRING support)
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::IsValueChanged(int index)
{
    if (index < 0 || index >= m_ItemCount) return false;

    if (m_Items[index].DataType == "STRING")
        return (m_Items[index].sStrValue != m_Items[index].sStrPrevValue);

    return (m_Items[index].lValue != m_Items[index].lPrevValue);
}

//---------------------------------------------------------------------------
// BuildPacket - identical format to GA3/ADS version
//   [STX][LEN_L][LEN_H][CNT][ID_L][ID_H][Q][V0][V1][V2][V3]...[CHK][ETX]
//---------------------------------------------------------------------------
int __fastcall TSCM_AH221Agent::BuildPacket(BYTE* buffer)
{
    int pos = 0;

    buffer[pos++] = PROTO_STX;

    int lenPos = pos;
    pos += 2;

    buffer[pos++] = (BYTE)m_ItemCount;

    for (int i = 0; i < m_ItemCount; i++)
    {
        WORD itemId = (WORD)m_Items[i].ItemID;
        buffer[pos++] = (BYTE)(itemId & 0xFF);
        buffer[pos++] = (BYTE)((itemId >> 8) & 0xFF);

        buffer[pos++] = (BYTE)m_Items[i].Quality;

        long value = m_Items[i].lValue;
        buffer[pos++] = (BYTE)(value & 0xFF);
        buffer[pos++] = (BYTE)((value >> 8) & 0xFF);
        buffer[pos++] = (BYTE)((value >> 16) & 0xFF);
        buffer[pos++] = (BYTE)((value >> 24) & 0xFF);
    }

    WORD dataLen = pos - 3;
    buffer[lenPos] = (BYTE)(dataLen & 0xFF);
    buffer[lenPos + 1] = (BYTE)((dataLen >> 8) & 0xFF);

    buffer[pos] = CalcChecksum(&buffer[1], pos - 1);
    pos++;

    buffer[pos++] = PROTO_ETX;

    return pos;
}

//---------------------------------------------------------------------------
// BuildStringPacket - STRING items only
//   [STX(0x02)][LEN_L][LEN_H][0xFE][CNT]
//   [ID_L][ID_H][Q][SLEN][char0][char1]...[charN]
//   ...(repeat per STRING item)
//   [CHK][ETX(0x03)]
//
//   0xFE = type marker to distinguish from regular packet
//          (regular packet has itemCount 1~50 at this position)
//   SLEN = string length in bytes (max 63)
//---------------------------------------------------------------------------
int __fastcall TSCM_AH221Agent::BuildStringPacket(BYTE* buffer)
{
    // Count STRING items
    int strCount = 0;
    for (int i = 0; i < m_ItemCount; i++)
    {
        if (m_Items[i].DataType == "STRING") strCount++;
    }
    if (strCount == 0) return 0;

    int pos = 0;

    buffer[pos++] = PROTO_STX;  // same STX as regular packet

    int lenPos = pos;
    pos += 2;  // placeholder for LEN

    buffer[pos++] = 0xFE;  // type marker: string packet
    buffer[pos++] = (BYTE)strCount;

    for (int i = 0; i < m_ItemCount; i++)
    {
        if (m_Items[i].DataType != "STRING") continue;

        WORD itemId = (WORD)m_Items[i].ItemID;
        buffer[pos++] = (BYTE)(itemId & 0xFF);
        buffer[pos++] = (BYTE)((itemId >> 8) & 0xFF);

        buffer[pos++] = (BYTE)m_Items[i].Quality;

        // String value (max 63 bytes, truncate if longer)
        AnsiString strVal = m_Items[i].sStrValue;
        int sLen = strVal.Length();
        if (sLen > 63) sLen = 63;

        buffer[pos++] = (BYTE)sLen;
        for (int j = 0; j < sLen; j++)
            buffer[pos++] = (BYTE)strVal[j + 1];  // AnsiString is 1-based
    }

    WORD dataLen = pos - 3;
    buffer[lenPos] = (BYTE)(dataLen & 0xFF);
    buffer[lenPos + 1] = (BYTE)((dataLen >> 8) & 0xFF);

    buffer[pos] = CalcChecksum(&buffer[1], pos - 1);
    pos++;

    buffer[pos++] = PROTO_ETX;

    return pos;
}

//---------------------------------------------------------------------------
// SendToESP32 (modified: sends regular + string packets)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::SendToESP32(int changeCount, int meaningfulCount, bool isHeartbeat)
{
    if (!m_bCommOpened || Mycomm == NULL || !Mycomm->Active())
    {
        LogMessage("E:COM not ready");
        return;
    }

    try
    {
        int packetLen = BuildPacket(m_SendBuffer);

        // Purge RX buffer
        while (Mycomm->ReadBufUsed() > 0)
        {
            BYTE dummy;
            Mycomm->ReadBuf(&dummy, 1);
        }

#if HK_DEBUG
        String hexDump = "TX: ";
        for (int i = 0; i < packetLen; i++)
            hexDump += IntToHex(m_SendBuffer[i], 2) + " ";
        LogMessage(hexDump);
#endif

        Mycomm->WriteBuf(m_SendBuffer, packetLen);

        // Compact log (same format as GA3)
        String logMsg = "D";
        if (isHeartbeat) logMsg += "(HB)";
        logMsg += ":" + IntToStr(m_ItemCount);
        if (changeCount > 0) logMsg += "(C:" + IntToStr(changeCount) + ")";
        logMsg += " TX:" + IntToStr(packetLen);

        if (WaitForResponse(RESP_TIMEOUT_MS))
        {
            logMsg += " OK";

            // --- Send STRING packet after regular packet ACK ---
            BYTE strBuffer[512];
            int strLen = BuildStringPacket(strBuffer);
            if (strLen > 0)
            {
                Sleep(50);

                // Purge RX buffer
                while (Mycomm->ReadBufUsed() > 0)
                {
                    BYTE dummy;
                    Mycomm->ReadBuf(&dummy, 1);
                }

                Mycomm->WriteBuf(strBuffer, strLen);
                logMsg += " STR:" + IntToStr(strLen);

                // ACK 대기 없이 전송만 (fire-and-forget)
                // ESP32 디버그 로그에서 수신 확인됨 — ACK 문제는 TVaComm RX 버퍼 이슈
                logMsg += " SENT";
            }

            for (int i = 0; i < m_ItemCount; i++)
            {
                m_Items[i].lPrevValue = m_Items[i].lValue;
                m_Items[i].sStrPrevValue = m_Items[i].sStrValue;
                m_Items[i].Changed = false;
            }
            m_nRetryCount = 0;
        }
        else
        {
            logMsg += " FAIL";
        }

        // [수정 2026-06-03] logsave.txt 는 '의미있는' 항목이 바뀔 때만 기록.
        //   MacOn 등 시간누적(초) 카운터는 머신이 켜져만 있어도 매초 증가해
        //   매 사이클 변화로 잡혔고, 그래서 logsave.txt 가 끝없이 커졌다.
        //   meaningfulCount = 시간누적 항목(bLogExempt) 제외한 변화 수.
        //   (MacOn 값 자체는 패킷으로 정상 전송됨 - 로그만 안 남길 뿐)
        if (meaningfulCount > 0 || logMsg.Pos("FAIL") > 0)
        {
            m_nNoChangeCount = 0;
            LogMessage(logMsg);
        }
        else
        {
            m_nNoChangeCount++;
            WriteStatusFile("NoChange:" + IntToStr(m_nNoChangeCount)
                          + " " + logMsg);
        }
    }
    catch (Exception &ex)
    {
        LogMessage("E:" + ex.Message);
    }
}

//---------------------------------------------------------------------------
// WaitForResponse (identical to ADS/GA3 version - TVaComm)
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::WaitForResponse(int timeoutMs)
{
    if (!m_bCommOpened || Mycomm == NULL || !Mycomm->Active())
        return false;

    BYTE respBuffer[5];
    int respIndex = 0;
    DWORD startTick = GetTickCount();

    m_bWaitingResponse = true;

    while (GetTickCount() - startTick < (DWORD)timeoutMs)
    {
        if (Mycomm->ReadBufUsed() > 0)
        {
            BYTE b;
            if (Mycomm->ReadBuf(&b, 1) == 1)
            {
                if (b == PROTO_STX && respIndex == 0)
                {
                    respBuffer[respIndex++] = b;
                }
                else if (respIndex > 0 && respIndex < 5)
                {
                    respBuffer[respIndex++] = b;

                    if (respIndex == 5)
                    {
                        m_bWaitingResponse = false;

                        if (respBuffer[4] != PROTO_ETX)
                            return false;

                        BYTE calcChk = respBuffer[1] ^ respBuffer[2];
                        if (calcChk != respBuffer[3])
                            return false;

                        BYTE cmd = respBuffer[1];
                        BYTE status = respBuffer[2];

                        return (cmd == RESP_CMD_ACK && status == RESP_STATUS_OK);
                    }
                }
            }
        }
        Sleep(10);
    }

    m_bWaitingResponse = false;
    return false;
}

//---------------------------------------------------------------------------
// HandleSendFailure (identical to GA3)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::HandleSendFailure()
{
    m_nRetryCount++;

    if (m_nRetryCount >= m_nMaxRetries)
    {
        LogMessage("Reconn...");

        CloseSerialPort();
        Sleep(1000);

        if (InitSerialPort(m_nComPort, m_nBaudRate))
        {
            LogMessage("COM OK");
            m_nRetryCount = 0;
        }
        else
        {
            LogMessage("COM FAIL");
        }
    }
}

//---------------------------------------------------------------------------
// ServiceStart
//
// [Difference from ADS version]
//   ADS: TAdsComm::Connect() + initial ADS reads
//   SQL: ConnectSQL() + register items + initial SQL poll
//   No AdsComm.h, no TcAdsDll.dll needed
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::ServiceStart(TService *Sender, bool &Started)
{
    if (Timer1) Timer1->Enabled = false;

    // ADO needs COM subsystem (same as GA3's OPC DA)
    // Moved to ConnectSQL() for cleaner lifecycle management

    LogMessage("SVC START");
    Started = true;

    try
    {
        // 0. Load INI
        LoadSettings();
        InitPollGroups();

        // 1. Register SQL data items
        //    [Replaces LoadItemConfig() CSV parsing]
        //    Items are defined here instead of CSV because SQL polling
        //    queries entire rows, not individual PLC addresses.
        m_ItemCount = 0;

        // --- DailyReport items (ID 1-6) ---
        m_Items[m_ItemCount].ItemID = 1;  m_Items[m_ItemCount].VarName = "MacOn";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable6_DailyReport";
        m_Items[m_ItemCount].Description = "Machine On (sec)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 2;  m_Items[m_ItemCount].VarName = "MacInStart";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable6_DailyReport";
        m_Items[m_ItemCount].Description = "Machine In Start (sec)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 3;  m_Items[m_ItemCount].VarName = "MacInAlarm";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable6_DailyReport";
        m_Items[m_ItemCount].Description = "Machine In Alarm (sec)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 4;  m_Items[m_ItemCount].VarName = "TrackInRun";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable6_DailyReport";
        m_Items[m_ItemCount].Description = "Track In Run (sec)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

//        m_Items[m_ItemCount].ItemID = 5;  m_Items[m_ItemCount].VarName = "MacInManual";
		m_Items[m_ItemCount].ItemID = 5;  m_Items[m_ItemCount].VarName = "MacStartAndFull";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable6_DailyReport";
//        m_Items[m_ItemCount].Description = "Machine In Manual (sec)";
		m_Items[m_ItemCount].Description = "Machine Start And Full (sec)";
 
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        // --- ProductionReport items (ID 10-16) ---
        m_Items[m_ItemCount].ItemID = 10; m_Items[m_ItemCount].VarName = "Quantity";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Panel count";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 11; m_Items[m_ItemCount].VarName = "L";
        m_Items[m_ItemCount].DataType = "FLOAT"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Length (mm*1000)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 12; m_Items[m_ItemCount].VarName = "W";
        m_Items[m_ItemCount].DataType = "FLOAT"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Width (mm*1000)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 13; m_Items[m_ItemCount].VarName = "T";
        m_Items[m_ItemCount].DataType = "FLOAT"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Thickness (mm*1000)";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        // --- ProductionReport STRING items (ID 14-16) ---
        m_Items[m_ItemCount].ItemID = 14; m_Items[m_ItemCount].VarName = "ProgramCode";
        m_Items[m_ItemCount].DataType = "STRING"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Program Code";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].sStrValue = ""; m_Items[m_ItemCount].sStrPrevValue = "";
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 15; m_Items[m_ItemCount].VarName = "EdgeCodeLH";
        m_Items[m_ItemCount].DataType = "STRING"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Edge Band Code LH";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].sStrValue = ""; m_Items[m_ItemCount].sStrPrevValue = "";
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 16; m_Items[m_ItemCount].VarName = "EdgeCodeRH";
        m_Items[m_ItemCount].DataType = "STRING"; m_Items[m_ItemCount].TableName = "CustomTable5_ProductionReport";
        m_Items[m_ItemCount].Description = "Edge Band Code RH";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].sStrValue = ""; m_Items[m_ItemCount].sStrPrevValue = "";
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        // --- ItemsDataHistory items (ID 30-31) ---
        m_Items[m_ItemCount].ItemID = 30; m_Items[m_ItemCount].VarName = "NodeID";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "ItemsDataHistory";
        m_Items[m_ItemCount].Description = "Latest event NodeID";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 31; m_Items[m_ItemCount].VarName = "Value";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "ItemsDataHistory";
        m_Items[m_ItemCount].Description = "Latest alarm Value";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        // --- ProductionBatch items (ID 20-24) ---
        m_Items[m_ItemCount].ItemID = 20; m_Items[m_ItemCount].VarName = "BatchCode";
        m_Items[m_ItemCount].DataType = "STRING"; m_Items[m_ItemCount].TableName = "CustomTable3_Production";
        m_Items[m_ItemCount].sStrValue = ""; m_Items[m_ItemCount].sStrPrevValue = "";   // �� �߰�
        m_Items[m_ItemCount].Description = "Batch code";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 21; m_Items[m_ItemCount].VarName = "PanelCode";
        m_Items[m_ItemCount].DataType = "STRING"; m_Items[m_ItemCount].TableName = "CustomTable3_Production";
        m_Items[m_ItemCount].sStrValue = ""; m_Items[m_ItemCount].sStrPrevValue = "";   // �� �߰�
        m_Items[m_ItemCount].Description = "Panel code";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        m_Items[m_ItemCount].ItemID = 22; m_Items[m_ItemCount].VarName = "Quantity";
        m_Items[m_ItemCount].DataType = "INT"; m_Items[m_ItemCount].TableName = "CustomTable3_Production";
        m_Items[m_ItemCount].Description = "Batch quantity";
        m_Items[m_ItemCount].lValue = 0; m_Items[m_ItemCount].lPrevValue = 0;
        m_Items[m_ItemCount].Quality = 0x00; m_Items[m_ItemCount].Changed = false;
        m_ItemCount++;

        LogMessage("Items: " + IntToStr(m_ItemCount));

        // [수정 2026-06-03] 시간누적(초 단위) 카운터를 로그 트리거에서 제외.
        //   머신이 켜져만 있어도 매초 증가 -> 매 사이클 '변화'로 잡혀 logsave.txt 폭증.
        //   전송(ESP32)은 그대로 유지하고, logsave.txt 기록은 의미있는 항목
        //   (생산수/치수/코드/알람) 변화 시에만 한다.
        for (int i = 0; i < m_ItemCount; i++)
        {
            m_Items[i].bLogExempt =
                (m_Items[i].VarName == "MacOn"           ||
                 m_Items[i].VarName == "MacInStart"      ||
                 m_Items[i].VarName == "MacInAlarm"      ||
                 m_Items[i].VarName == "TrackInRun"      ||
                 m_Items[i].VarName == "MacStartAndFull");
        }

        // 2. Serial port (TVaComm - same as GA3)
        if (!InitSerialPort(m_nComPort, m_nBaudRate))
        {
            LogMessage("COM FAIL");
        }

        // 3. SQL connect (replaces ADS connect)
        if (ConnectSQL())
        {
            LogMessage("SQL Connected: " + m_sSqlServer + "/" + m_sSqlDatabase);
        }
        else
        {
            LogMessage("SQL Connect FAIL - will retry");
        }

        // 4. Initial poll test
        if (m_bSqlConnected)
        {
            if (PollDailyReport())
                LogMessage("INIT DR OK");
            if (PollProductionReport())
                LogMessage("INIT PR OK");
        }

        m_bFirstSend = true;
        m_dwLastSendTick = 0;

        // 5. Start timer (same as GA3)
        if (Timer1)
        {
            Timer1->Interval = m_nTimeInterval;
            Timer1->Enabled = true;
            LogMessage("Timer1 enabled, Interval=" + IntToStr(Timer1->Interval));
        }
        else
        {
            LogMessage("ERROR: Timer1 is NULL!");
        }

        LogMessage("SVC READY");
    }
    catch (Exception &ex)
    {
        LogMessage("E:" + ex.Message);
    }
}

//---------------------------------------------------------------------------
// ServiceStop
//   [Difference from ADS version]
//   ADS: TAdsComm::Disconnect() + delete
//   SQL: DisconnectSQL() (includes CoUninitialize)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::ServiceStop(TService *Sender, bool &Stopped)
{
    LogMessage("SVC STOP");

    if (Timer1) Timer1->Enabled = false;

    CloseSerialPort();
    DisconnectSQL();

    Stopped = true;
    LogMessage("SVC END");
}

//---------------------------------------------------------------------------
// Timer1Timer - Main polling loop
//   [Difference from ADS version]
//   ADS: ReadAdsItem() for each item, then change detect, then send
//   SQL: Per-group polling with cycle counters, then change detect, then send
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::Timer1Timer(TObject *Sender)
{
    Timer1->Enabled = false;

    try
    {
        // SQL reconnect if needed (replaces ADS reconnect)
        if (!m_bSqlConnected)
        {
            LogMessage("SQL reconnecting...");
            if (ConnectSQL())
                LogMessage("SQL reconnected OK");
            else
            {
                Timer1->Enabled = true;
                return;
            }
        }

        if (m_bSqlConnected && m_ItemCount > 0)
        {
            // 1. Time-tiered SQL polling
            if (ShouldPollGroup(pgDailyReport))
                PollDailyReport();

            if (ShouldPollGroup(pgProductionReport))
                PollProductionReport();

            if (ShouldPollGroup(pgItemsDataHistory))
                PollItemsDataHistory();

            if (ShouldPollGroup(pgProductionBatch))
                PollProductionBatch();

            // 2. Change detection (identical to GA3/ADS)
            //    changeCount    : 전체 변화 (전송 판단용 - 기존 동작 유지)
            //    meaningfulCount: 시간누적 카운터(bLogExempt) 제외 (로그 판단용)
            int changeCount = 0;
            int meaningfulCount = 0;
            for (int i = 0; i < m_ItemCount; i++)
            {
                if (IsValueChanged(i))
                {
                    m_Items[i].Changed = true;
                    changeCount++;
                    if (!m_Items[i].bLogExempt) meaningfulCount++;
                }
            }
            bool hasChanges = (changeCount > 0);

            // 3. Heartbeat timeout (identical to GA3)
            DWORD dwNow = GetTickCount();
            bool heartbeatTimeout = false;

            if (m_dwLastSendTick == 0)
            {
                heartbeatTimeout = true;
            }
            else
            {
                DWORD elapsed;
                if (dwNow >= m_dwLastSendTick)
                    elapsed = dwNow - m_dwLastSendTick;
                else
                    elapsed = (0xFFFFFFFF - m_dwLastSendTick) + dwNow + 1;

                if (elapsed >= m_dwHeartbeatInterval)
                    heartbeatTimeout = true;
            }

            // 4. Send condition (identical to GA3)
            if (m_bFirstSend || hasChanges || heartbeatTimeout)
            {
                bool isHB = heartbeatTimeout && !hasChanges && !m_bFirstSend;

                SendToESP32(changeCount, meaningfulCount, isHB);
                m_dwLastSendTick = GetTickCount();

                m_bFirstSend = false;
            }
        }
    }
    catch (Exception &e)
    {
        LogMessage("E:" + e.Message);
    }

    Timer1->Enabled = true;
}

//---------------------------------------------------------------------------
