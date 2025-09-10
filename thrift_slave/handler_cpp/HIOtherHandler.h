#ifndef _HIOTHERHANDLER_H_
#define _HIOTHERHANDLER_H_

#include "HIOther.h"

using namespace  ::H2103_Host_Invoke;

class HIOtherHandler : virtual public HIOtherIf {
public:
    HIOtherHandler();

    ::EXE_STATE::type ExecuteScriptAsync(const std::string& strFileNmae, const int32_t iUserData);

    ::EXE_STATE::type UpgradeSlaveProgramAsync(const  ::SLAVE_PROGRAM_T& tSlaveprogram, const int32_t iUserData);

    void GetVersion(std::string& _return, const int32_t iType);

    ::EXE_STATE::type SetInstrumentNo(const std::string& strInstrumentNo);

    void GetInstrumentNo(std::string& _return);

    ::EXE_STATE::type SetSystemTime(const  ::DATE_TIME_T& tDateTime);
	
	void GetSystemTime( ::DATE_TIME_T& _return);

    ::EXE_STATE::type SetBootStrategy(const std::vector< ::BOOT_PARAM_T> & lstcBootParams, const std::vector<std::string> & lstMAC);

    ::EXE_STATE::type HeartbeatAsync(const int32_t iUserData);
    
    ::EXE_STATE::type ThriftMotorParaSet(const  ::THRIFT_MOTOR_PARA_T& tMotorPara);

    void ThriftMotorParaGet( ::THRIFT_MOTOR_PARA_T& _return, const int32_t iMotorID);

    ::EXE_STATE::type ThriftMotorPosSet(const int32_t iMotorID, const int32_t iPos, const int32_t iStep);

    void ThriftMotorPosGet(std::vector<int32_t> & _return, const int32_t iMotorID);

    ::EXE_STATE::type ThriftMotorReset(const int32_t iMotorID, const int32_t iIsFirst);

    ::EXE_STATE::type ThriftMotorMove(const int32_t iMotorID, const int32_t iStep);

    ::EXE_STATE::type ThriftMotorMoveTo(const int32_t iMotorID, const int32_t iStep);

    void ThriftReadBarcode(std::string& _return, const int32_t iReaderID);

    int32_t ThriftLiquidDetect(const int32_t iNeedleID);
    ::EXE_STATE::type RotatingReagentBin(const int32_t iReagentPos);

    ::EXE_STATE::type ThriftRackMoveIn();

    ::EXE_STATE::type ThriftRackMoveOutHorizontal();

    ::EXE_STATE::type EngineerDebugPosSet(const  ::ENGINEER_DEBUG_MODULE_PARA_T& tModulePara);

    void EngineerDebugPosGet(std::vector< ::ENGINEER_DEBUG_MODULE_PARA_T> & _return, const int32_t iModuleIndex);

    void EngineerDebugGetVirtualPosition(std::vector< ::ENGINEER_DEBUG_VIRTUAL_POSITION_T> & _return);

    ::EXE_STATE::type EngineerDebugMotorActionExecuteAsync(const  ::ENGINEER_DEBUG_MOTOR_PARA_T& tMotorPara, const int32_t iUserData);

    ::EXE_STATE::type EngineerDebugWeighingAsync(const int32_t iNeedType, const int32_t iSampleOrReagentVol, const int32_t iDiulentVol, const int32_t iCups, const int32_t iUserData);

    ::EXE_STATE::type EngineerDebugInjectorKBSet(const  ::ENGINEER_DEBUG_INJECTOR_KB_T& tInjectorKB);

    void EngineerDebugInjectorKBGet(std::vector< ::ENGINEER_DEBUG_INJECTOR_KB_T> & _return);

    ::EXE_STATE::type EngineerDebugRunAsync(const int32_t iModuleIndex, const int32_t iCmd, const int32_t iUserData);

    ::EXE_STATE::type ThriftConfigPara(const  ::THRIFT_CONFIG_T& tThriftConfig, const int32_t iUserData);

    ::EXE_STATE::type SetTimeOut(const int32_t iType, const int32_t iSeconds);

    ::EXE_STATE::type GetUploadBackupFile(const int32_t iFileType, const int32_t iRandNo);

    ::EXE_STATE::type RestoreConfigFile(const std::string& strFileName, const int32_t iFileType, const std::string& hexConfigFile, const std::string& strMD5);

    void GetChannelStatus(std::vector< ::CHANNEL_STATUS_T> & _return);

    ::EXE_STATE::type SetChannelStatus(const std::vector< ::CHANNEL_STATUS_T> & lstChannelStatus);

    ::EXE_STATE::type StartAdjustChannelAsync(const int32_t iUserData);

    ::EXE_STATE::type SetOpticalLED(const int32_t iOnOrOff, const int32_t iWave);

    void GetChannelAD(std::vector< ::CHANNEL_AD_T> & _return);

    void GetChannelGain( ::CHANNEL_GAIN_T& _return);

    ::EXE_STATE::type EngineerAgingRunAsync(const ::SLAVE_ASSEMBLY_AGING_PARA_T& tAssemblyAgingPara, const int32_t iUserData);

    int32_t GetOtherPara(const int32_t iParaType);

    ::EXE_STATE::type SetOtherPara(const int32_t iParaType, const int32_t iParaVal);

    ::EXE_STATE::type EngineerDebugAutoCalibrationAsync(const int32_t iCalibID, const int32_t iType, const int32_t iUserData);

    ::EXE_STATE::type SetSystemBaseData(const std::string& strJson);
};

class HIOtherHandlerFactory : virtual public HIOtherIfFactory {
public:
    virtual ~HIOtherHandlerFactory() {}
    virtual HIOtherIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo);
    virtual void releaseHandler(HIOtherIf* handler);
};

#endif

