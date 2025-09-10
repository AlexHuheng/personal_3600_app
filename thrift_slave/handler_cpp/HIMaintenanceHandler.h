#ifndef _HIMAINTENANCEHANDLER_H_
#define _HIMAINTENANCEHANDLER_H_

#include "HIMaintenance.h"

using namespace  ::H2103_Host_Invoke;

class HIMaintenanceHandler : virtual public HIMaintenanceIf {
public:
    HIMaintenanceHandler();

    ::EXE_STATE::type ReagentScanAsync(const int32_t iAreaIndex, const int32_t iUserData);

    ::EXE_STATE::type ActiveMachineAsync(const int32_t iUserData);

    ::EXE_STATE::type SetFluxModeAsync(const int32_t         iFluxMode, const int32_t iUserData);

    int32_t GetFluxMode();

    ::EXE_STATE::type ReagentRemainDetectAsync(const std::vector< ::REAGENT_POS_INFO_T> & lstReagPosInfo, const int32_t iUserData);
    
    ::EXE_STATE::type ReagentMixingAsync(const std::vector< ::REAGENT_MIX_INFO_T> & lstReagMixInfo, const int32_t iUserData);
	
	::EXE_STATE::type RunMaintenance(const int32_t iMainID, const int32_t iKeepCool, const std::vector<int32_t> & lstReserved1, const std::vector<std::string> & lstReserved2, const int32_t iUserData);

    ::EXE_STATE::type RunMaintenanceGroup(const int32_t iGroupID, const std::vector< ::MAINTENANCE_ITEM_T> & lstItems, const int32_t iUserData);

};

#endif

