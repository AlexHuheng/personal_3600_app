#ifndef _HIREALMONITORHANDLER_H_
#define _HIREALMONITORHANDLER_H_

#include "HIRealMonitor.h"

using namespace  ::H2103_Host_Invoke;

class HIRealMonitorHandler : virtual public HIRealMonitorIf {
public:
    HIRealMonitorHandler();

    ::EXE_STATE::type GetIOAsync(const  ::OUTPUT_IO::type sensor, const int32_t iUserData);

    ::EXE_STATE::type GetTemperatureAsync(const  ::TEMPERATURE_SENSOR::type sensor, const int32_t iUserData);

    ::EXE_STATE::type GetPressureAsync(const int32_t sensor, const int32_t iUserData);

    ::EXE_STATE::type SetIOAsync(const  ::INPUT_IO::type sensor, const int32_t iState, const int32_t iUserData);

    ::EXE_STATE::type ManualUnlockSlotAsync(const  ::INPUT_IO::type sensor, const int32_t iUserData);

    ::EXE_STATE::type GetLightSignalAsync(const int32_t iWave, const int32_t iUserData);

	::EXE_STATE::type SetIndicatorLightAsync(const int32_t iIndicatorLightNo, const int32_t iColor, const int32_t iBlink, const int32_t iUserData);

    ::EXE_STATE::type SetAlarmSoundAsync(const  ::IBOOL bOpen, const int32_t iSound, const int32_t iUserData);

    ::EXE_STATE::type ManualStopAsync(const int32_t iUserData);

    ::EXE_STATE::type SetConsumablesInfo(const  ::CONSUMABLES_INFO_T& tConsumablesInfo);

    ::EXE_STATE::type SetCounterOrTimer(const int32_t iCounterID, const int32_t iNumberOfTimesOrElapsedTimes);

    ::EXE_STATE::type SetTotalCounterOrTimer(const int32_t iCounterOrTimerID, const int32_t iTotalNumberOfTimesOrElapsedTime);

    void GetAllCounterOrTimer(std::vector< ::SLAVE_COUNTEROR_TIMER_INFO_T> & _return);

    int32_t GetCurrentInstrumentState();
};

#endif
