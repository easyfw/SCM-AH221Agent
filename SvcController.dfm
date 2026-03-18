object SCM_AH221Agent: TSCM_AH221Agent
  OldCreateOrder = False
  DisplayName = 'SCM_AH221 ADS Agent Service'
  Left = 390
  Top = 421
  Height = 150
  Width = 215
  object Mycomm: TVaComm
    FlowControl.OutCtsFlow = False
    FlowControl.OutDsrFlow = False
    FlowControl.ControlDtr = dtrDisabled
    FlowControl.ControlRts = rtsDisabled
    FlowControl.XonXoffOut = False
    FlowControl.XonXoffIn = False
    FlowControl.DsrSensitivity = False
    FlowControl.TxContinueOnXoff = False
    DeviceName = 'COM%d'
    Version = '1.3.5.0'
    Left = 24
    Top = 32
  end
  object Timer1: TTimer
    OnTimer = Timer1Timer
    Left = 104
    Top = 40
  end
end
