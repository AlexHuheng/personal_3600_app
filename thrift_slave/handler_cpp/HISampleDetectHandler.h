#ifndef _HISAMPLEDETECTHANDLER_H_
#define _HISAMPLEDETECTHANDLER_H_

#include "HISampleDetect.h"

using namespace  ::H2103_Host_Invoke;

class HISampleDetectHandler : virtual public HISampleDetectIf {
public:
    HISampleDetectHandler();

	::EXE_STATE::type InstrumentSelfTestAsync(const std::vector< ::REAGENT_MIX_INFO_T> & lstReagMixInfo, const std::vector< ::REAGENT_POS_INFO_T> & lstReagPosInfo, const int32_t iUserData);

    ::EXE_STATE::type CreateSampleOrder(const  ::SAMPLE_ORDER_INFO_T& tSampleOrderInfo);

	::EXE_STATE::type UpdateSTATSampleOrder(const int32_t  iSampleOrderNo);

	::EXE_STATE::type RemoveSlotOrder(const int32_t  iSlotNo);

    int32_t IsTestFinished();

    ::EXE_STATE::type NormalStopAsync(const int32_t iUserData);

	::EXE_STATE::type ConsumablesStopAsync(const int32_t  iUserData);
	
	::EXE_STATE::type SetSampleStopAsync(const int32_t iSampleStop, const int32_t iUserData);

	void QueryIsOpenReagentBinCoverOrDiluentCover(std::vector<int32_t> & _return);

    ::EXE_STATE::type      UpdateOrder(const int32_t iOrderNo, const int32_t iReagentType, const  ::ORDER_INFO_T& tOrderInfo, const int32_t iSamplePos);


    // 暂未使用
    ::EXE_STATE::type      DeleteOrder(const int32_t  iOrderNo);
};

#endif
