//---------------------------------------------------------------------------
// SvcController.cpp - SCM-AH221 ADS Agent Service Implementation
//
// [What changed from GA3 SvcController.cpp]
//   - Removed: OPC DA code (CoInitialize, CoOPCServer, OPCGroups, AddItem,
//              pItem->Read, VARIANT handling, GetQualityCode, VariantToLong,
//              VariantToString, HasAnyChanges)
//   - Added:   TAdsComm for PLC communication
//   - Added:   ReadAdsItem() for typed ADS reads
//   - Changed: LoadSettings() adds [ADS] section
//   - Changed: LoadItemConfig() CSV 6-column format (+ IGroup/IOffset)
//   - Changed: BuildPacket() uses lValue directly (no VARIANT)
//   - Changed: ServiceStart() ADS connect instead of OPC
//   - Changed: ServiceStop() ADS disconnect instead of OPC
//
// [Kept identical to GA3]
//   - TVaComm serial communication (Mycomm)
//   - Packet format [STX][LEN][CNT][ID][Q][VAL]...[CHK][ETX]
//   - Change detection, Heartbeat, ACK/NAK
//   - Log file rotation (60KB)
//   - WaitForResponse(), HandleSendFailure(), SendToESP32()
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

    m_pAds = NULL;
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

    // Default settings (overridden by INI)
    m_sAmsNetId = EdgeDouble::AMS_NET_ID;
    m_nAdsPort = EdgeDouble::ADS_PORT;
    m_nComPort = 3;
    m_nBaudRate = 115200;
    m_nTimeInterval = 5000;
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
// LogMessage (identical to GA3)
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
// LoadSettings
//   [Added vs GA3]: [ADS] section with AmsNetId and AdsPort
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::LoadSettings()
{
    String IniPath = ExtractFilePath(ParamStr(0)) + "oem_setting.ini";

    TIniFile *ini = new TIniFile(IniPath);
    try
    {
        // [ADS] section (new for AH221)
        m_sAmsNetId = ini->ReadString("ADS", "AmsNetId", EdgeDouble::AMS_NET_ID);
        m_nAdsPort = ini->ReadInteger("ADS", "AdsPort", EdgeDouble::ADS_PORT);

        // [Communication] section (same as GA3)
        String comStr = ini->ReadString("Communication", "COM_Port", "COM3");
        if (comStr.UpperCase().Pos("COM") == 1)
            m_nComPort = StrToIntDef(comStr.SubString(4, comStr.Length() - 3), 3);
        else
            m_nComPort = StrToIntDef(comStr, 3);

        m_nBaudRate = ini->ReadInteger("Communication", "BaudRate", 115200);

        // [Agent] section (same as GA3)
        m_nTimeInterval = ini->ReadInteger("Agent", "TimeInterval", 5000);

        LogMessage("CFG: ADS=" + m_sAmsNetId + ":" + IntToStr(m_nAdsPort)
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
// LoadItemConfig
//   [Changed vs GA3]: 6-column CSV (ItemID,VarName,DataType,IGroup,IOffset,Desc)
//   GA3 was 4-column:  (ItemID,TagName,DataType,Description)
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::LoadItemConfig(String filename)
{
    TStringList *lines = new TStringList();
    m_ItemCount = 0;

    try
    {
        if (!FileExists(filename))
        {
            LogMessage("Config file not found: " + filename);
            delete lines;
            return false;
        }

        lines->LoadFromFile(filename);
        LogMessage("Loading config: " + filename + " (" + IntToStr(lines->Count) + " lines)");

        for (int i = 1; i < lines->Count && m_ItemCount < MAX_ADS_ITEMS; i++)
        {
            String line = lines->Strings[i].Trim();
            if (line.IsEmpty() || line[1] == '#')
                continue;

            // Manual CSV parsing (same approach as GA3)
            String cols[6];
            int colIndex = 0;
            String temp = "";

            for (int j = 1; j <= line.Length(); j++)
            {
                if (line[j] == ',')
                {
                    if (colIndex < 6) cols[colIndex] = temp.Trim();
                    temp = "";
                    colIndex++;
                }
                else
                {
                    temp += line[j];
                }
            }
            if (colIndex < 6) cols[colIndex] = temp.Trim();

            // Need at least: ItemID(0), DataType(2), IGroup(3), IOffset(4)
            if (cols[0].IsEmpty() || cols[2].IsEmpty() || cols[3].IsEmpty() || cols[4].IsEmpty())
                continue;

            m_Items[m_ItemCount].ItemID = StrToIntDef(cols[0], 0);
            m_Items[m_ItemCount].VarName = cols[1];
            m_Items[m_ItemCount].DataType = cols[2].UpperCase();

            // Parse IGroup: "0x4020" or "16448"
            String sIG = cols[3];
            if (sIG.SubString(1, 2).LowerCase() == "0x")
                m_Items[m_ItemCount].IGroup = StrToInt("$" + sIG.SubString(3, sIG.Length() - 2));
            else
                m_Items[m_ItemCount].IGroup = StrToIntDef(sIG, 0x4020);

            m_Items[m_ItemCount].IOffset = StrToIntDef(cols[4], 0);
            m_Items[m_ItemCount].Description = cols[5];

            m_Items[m_ItemCount].lValue = 0;
            m_Items[m_ItemCount].lPrevValue = 0;
            m_Items[m_ItemCount].Quality = 0;
            m_Items[m_ItemCount].Changed = false;

            LogMessage("  Item[" + IntToStr(m_ItemCount) + "]: ID="
                     + IntToStr(m_Items[m_ItemCount].ItemID)
                     + " " + m_Items[m_ItemCount].VarName
                     + " " + m_Items[m_ItemCount].DataType
                     + " IG=" + IntToHex((int)m_Items[m_ItemCount].IGroup, 4)
                     + " IO=" + IntToStr((int)m_Items[m_ItemCount].IOffset));

            m_ItemCount++;
        }
        LogMessage("Loaded " + IntToStr(m_ItemCount) + " items from config.");
    }
    catch (Exception &ex)
    {
        LogMessage("Error loading config: " + ex.Message);
        delete lines;
        return false;
    }

    delete lines;
    return (m_ItemCount > 0);
}

//---------------------------------------------------------------------------
// InitSerialPort (identical to GA3 - TVaComm)
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
// ReadAdsItem - Read one PLC variable via ADS
//
// [Replaces GA3's OPCItem->Read() + VariantToLong() pipeline]
//   GA3: pItem->Read(2, &varValue, &varQuality, &varTimestamp)
//        then VariantToLong(varValue) to convert VARIANT -> long
//   AH221: m_pAds->ReadInt(IGroup, IOffset, nVal)
//          direct typed read, no VARIANT involved
//
//   REAL/LREAL x1000 for fixed-point: same as GA3's VariantToLong VT_R4/R8
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::ReadAdsItem(int index)
{
    if (index < 0 || index >= m_ItemCount) return false;
    if (!m_pAds || !m_pAds->IsConnected()) return false;

    TAdsItemInfo &item = m_Items[index];
    String dt = item.DataType;
    bool ok = false;

    if (dt == "BOOL")
    {
        bool bVal = false;
        ok = m_pAds->ReadBool(item.IGroup, item.IOffset, bVal);
        if (ok) item.lValue = bVal ? 1 : 0;
    }
    else if (dt == "BYTE")
    {
        unsigned char bVal = 0;
        ok = m_pAds->ReadByte(item.IGroup, item.IOffset, bVal);
        if (ok) item.lValue = (long)bVal;
    }
    else if (dt == "INT")
    {
        short nVal = 0;
        ok = m_pAds->ReadInt(item.IGroup, item.IOffset, nVal);
        if (ok) item.lValue = (long)nVal;
    }
    else if (dt == "UINT")
    {
        unsigned short nVal = 0;
        ok = m_pAds->ReadUInt(item.IGroup, item.IOffset, nVal);
        if (ok) item.lValue = (long)nVal;
    }
    else if (dt == "DINT")
    {
        long nVal = 0;
        ok = m_pAds->ReadDInt(item.IGroup, item.IOffset, nVal);
        if (ok) item.lValue = nVal;
    }
    else if (dt == "REAL")
    {
        float fVal = 0.0f;
        ok = m_pAds->ReadReal(item.IGroup, item.IOffset, fVal);
        if (ok) item.lValue = (long)(fVal * 1000);
    }
    else if (dt == "LREAL")
    {
        double dVal = 0.0;
        ok = m_pAds->ReadLReal(item.IGroup, item.IOffset, dVal);
        if (ok) item.lValue = (long)(dVal * 1000);
    }
    else
    {
        // Unknown type - try as DINT (4 bytes)
        long nVal = 0;
        ok = m_pAds->ReadDInt(item.IGroup, item.IOffset, nVal);
        if (ok) item.lValue = nVal;
    }

    //item.Quality = ok ? 0 : 9;
	// 변경 후 - OPC DA 호환 + ESP32/Node-RED 파이프라인 일관성 유지
	item.Quality = ok ? 0xC0 : 0x00;

    return ok;
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
// IsValueChanged (simplified - no VARIANT needed)
//---------------------------------------------------------------------------
bool __fastcall TSCM_AH221Agent::IsValueChanged(int index)
{
    if (index < 0 || index >= m_ItemCount) return false;
    return (m_Items[index].lValue != m_Items[index].lPrevValue);
}

//---------------------------------------------------------------------------
// BuildPacket - identical format to GA3
//   [STX][LEN_L][LEN_H][CNT][ID_L][ID_H][Q][V0][V1][V2][V3]...[CHK][ETX]
//
// [Difference from GA3]
//   GA3: GetQualityCode(m_Items[i].Quality) decodes OPC bitmask
//   AH221: Quality already 0=Good or 9=Error from ReadAdsItem()
//
//   GA3: VariantToLong(m_Items[i].varValue)
//   AH221: m_Items[i].lValue directly
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
// SendToESP32 (identical to GA3 - TVaComm version)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::SendToESP32(int changeCount, bool isHeartbeat)
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
            for (int i = 0; i < m_ItemCount; i++)
            {
                m_Items[i].lPrevValue = m_Items[i].lValue;
                m_Items[i].Changed = false;
            }
            m_nRetryCount = 0;
        }
        else
        {
            logMsg += " FAIL";
//            HandleSendFailure();
        }

        LogMessage(logMsg);
    }
    catch (Exception &ex)
    {
        LogMessage("E:" + ex.Message);
//        HandleSendFailure();
    }
}

//---------------------------------------------------------------------------
// WaitForResponse (identical to GA3 - TVaComm version)
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
// [GA3 vs AH221 startup]
//   GA3:                              AH221:
//   1. CoInitialize(NULL)             1. (not needed - no COM)
//   2. LoadSettings()                 2. LoadSettings()
//   3. LoadItemConfig() 4-col         3. LoadItemConfig() 6-col
//   4. InitSerialPort()               4. InitSerialPort() <- same
//   5. OPCServer->Connect()           5. TAdsComm::Connect()
//   6. Create OPC Group               6. (not needed)
//   7. AddItem for each tag           7. (not needed)
//   8. Sleep(2000)                    8. Sleep(1000)
//   9. Initial OPC Read               9. Initial ADS Read
//  10. Start Timer                   10. Start Timer <- same
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::ServiceStart(TService *Sender, bool &Started)
{
    if (Timer1) Timer1->Enabled = false;

    // No CoInitialize needed (ADS does not use COM subsystem)

    LogMessage("SVC START");
    Started = true;

    try
    {
        // 0. Load INI
        LoadSettings();

        // 1. Load CSV config
        String exePath = ExtractFilePath(ParamStr(0));
        String configFile = exePath + "oem_param.csv";

        if (!LoadItemConfig(configFile))
        {
            // Default: known EdgeDbl PLC variables (tested)
            LogMessage("CFG: default items");

            m_ItemCount = 5;
            m_Items[0].ItemID = 1; m_Items[0].VarName = "cMaxPann";
            m_Items[0].DataType = "INT"; m_Items[0].IGroup = 0x4020; m_Items[0].IOffset = 584;
            m_Items[1].ItemID = 2; m_Items[1].VarName = "cMaxPrg";
            m_Items[1].DataType = "INT"; m_Items[1].IGroup = 0x4020; m_Items[1].IOffset = 588;
            m_Items[2].ItemID = 3; m_Items[2].VarName = "cMaxAlm";
            m_Items[2].DataType = "INT"; m_Items[2].IGroup = 0x4020; m_Items[2].IOffset = 590;
            m_Items[3].ItemID = 4; m_Items[3].VarName = "cMaxWrn";
            m_Items[3].DataType = "INT"; m_Items[3].IGroup = 0x4020; m_Items[3].IOffset = 592;
            m_Items[4].ItemID = 5; m_Items[4].VarName = "cMaxMsg";
            m_Items[4].DataType = "INT"; m_Items[4].IGroup = 0x4020; m_Items[4].IOffset = 594;

            for (int i = 0; i < m_ItemCount; i++)
            {
                m_Items[i].Description = "";
                m_Items[i].lValue = 0;
                m_Items[i].lPrevValue = 0;
                m_Items[i].Quality = 0;
                m_Items[i].Changed = false;
            }
        }

        // 2. Serial port (TVaComm - same as GA3)
        if (!InitSerialPort(m_nComPort, m_nBaudRate))
        {
            LogMessage("COM FAIL");
        }

        // 3. ADS connect (replaces GA3 steps 3-5: OPC connect + group + items)
        m_pAds = new TAdsComm();

        if (!m_pAds->IsDllLoaded())
        {
            LogMessage("ADS DLL FAIL: " + m_pAds->GetLastError());
        }
        else
        {
            LogMessage("ADS DLL loaded OK");

            if (m_pAds->Connect(m_sAmsNetId, m_nAdsPort))
            {
                LogMessage("ADS Connected: " + m_sAmsNetId + ":" + IntToStr(m_nAdsPort));

                AnsiString sDevName;
                unsigned short nVersion;
                if (m_pAds->GetDeviceInfo(sDevName, nVersion))
                {
                    LogMessage("ADS Device: " + sDevName + " v" + IntToStr(nVersion));
                }
            }
            else
            {
                LogMessage("ADS Connect FAIL: " + m_pAds->GetLastError());
            }
        }

        // 4. Wait for PLC stabilize (GA3 used 2000ms for OPC warmup,
        //    ADS is stateless so 1000ms is enough)
        Sleep(1000);

        // 5. Initial read test
        if (m_pAds && m_pAds->IsConnected())
        {
            int readOk = 0;
            for (int i = 0; i < m_ItemCount; i++)
            {
                if (ReadAdsItem(i))
                {
                    m_Items[i].lPrevValue = m_Items[i].lValue;
                    readOk++;
                    LogMessage("INIT RD[" + IntToStr(i) + "] "
                             + m_Items[i].VarName + " = "
                             + IntToStr(m_Items[i].lValue));
                }
                else
                {
                    LogMessage("INIT RD ERR[" + IntToStr(i) + "] "
                             + m_Items[i].VarName + ": "
                             + m_pAds->GetLastError());
                }
            }
            LogMessage("INIT: " + IntToStr(readOk) + "/" + IntToStr(m_ItemCount) + " OK");
        }

        m_bFirstSend = true;
        m_dwLastSendTick = 0;

        // 6. Start timer (same as GA3)
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
//   [Difference from GA3]
//   GA3: VariantClear each item, OPCServer->Disconnect(), CoUninitialize()
//   AH221: TAdsComm::Disconnect() + delete, no VARIANT/COM cleanup
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::ServiceStop(TService *Sender, bool &Stopped)
{
    LogMessage("SVC STOP");

    if (Timer1) Timer1->Enabled = false;

    CloseSerialPort();

    try
    {
        if (m_pAds)
        {
            m_pAds->Disconnect();
            delete m_pAds;
            m_pAds = NULL;
        }
    }
    catch (Exception &ex)
    {
        LogMessage("E:" + ex.Message);
    }

    // No CoUninitialize needed

    Stopped = true;
    LogMessage("SVC END");
}

//---------------------------------------------------------------------------
// Timer1Timer - Main polling loop
//   Same flow as GA3, only the read method changed (ADS vs OPC)
//---------------------------------------------------------------------------
void __fastcall TSCM_AH221Agent::Timer1Timer(TObject *Sender)
{
    Timer1->Enabled = false;

    try
    {
        if (m_pAds != NULL && m_pAds->IsConnected() && m_ItemCount > 0)
        {
            // 1. Read all ADS items
            for (int i = 0; i < m_ItemCount; i++)
            {
                ReadAdsItem(i);
            }

            // 2. Change detection (identical to GA3)
            int changeCount = 0;
            for (int i = 0; i < m_ItemCount; i++)
            {
                if (IsValueChanged(i))
                {
                    m_Items[i].Changed = true;
                    changeCount++;
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

                SendToESP32(changeCount, isHB);
                m_dwLastSendTick = GetTickCount();

                m_bFirstSend = false;
            }
        }
        else if (m_pAds != NULL && !m_pAds->IsConnected())
        {
            // ADS disconnected - attempt reconnect
            LogMessage("ADS reconnecting...");
            if (m_pAds->Connect(m_sAmsNetId, m_nAdsPort))
            {
                LogMessage("ADS reconnected OK");
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
