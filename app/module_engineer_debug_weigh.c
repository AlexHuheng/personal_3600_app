/******************************************************
工程师模式的相关流程实现文件（实现 称量功能）
******************************************************/
#include <log.h>
#include <module_common.h>
#include <module_engineer_debug_weigh.h>
#include <module_cup_mix.h>
#include <module_magnetic_bead.h>
#include <module_needle_s.h>
#include <module_needle_r2.h>

int needle_s_weigh(int needle_take_ul)
{
    return needle_s_add_test(needle_take_ul);
}

int needle_s_dilu_weigh(int needle_take_dilu_ul, int needle_take_ul)
{
    return needle_s_dilu_add_test(needle_take_dilu_ul, needle_take_ul);
}

int needle_s_sp_weigh(int needle_take_ul)
{
    return needle_s_sp_add_test(needle_take_ul);
}

int needle_r2_weigh(int needle_take_ul)
{
    return needle_r2_add_test(needle_take_ul);
}

int needle_s_muti_weigh(int needle_take_ul)
{
    return needle_s_muti_add_test(needle_take_ul);
}

int needle_s_sp_muti_weigh(int needle_take_ul)
{
    return needle_s_sp_muti_add_test(needle_take_ul);
}

int needle_r2_muti_weigh(int needle_take_ul)
{
    return needle_r2_muti_add_test(needle_take_ul);
}

