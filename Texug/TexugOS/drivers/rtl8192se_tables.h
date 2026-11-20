#ifndef TEXUGOS_RTL8192SE_TABLES_H
#define TEXUGOS_RTL8192SE_TABLES_H

#include <stdint.h>

typedef uint32_t u32;

#define PHY_REG_2T2RARRAYLENGTH 372u
#define PHY_CHANGETO_1T1RARRAYLENGTH 48u
#define PHY_CHANGETO_1T2RARRAYLENGTH 45u
#define PHY_REG_ARRAY_PGLENGTH 84u
#define RADIOA_1T_ARRAYLENGTH 202u
#define RADIOB_ARRAYLENGTH 22u
#define RADIOB_GM_ARRAYLENGTH 10u
#define MAC_2T_ARRAYLENGTH 106u
#define AGCTAB_ARRAYLENGTH 320u

extern u32 rtl8192sephy_reg_2t2rarray[PHY_REG_2T2RARRAYLENGTH];
extern u32 rtl8192sephy_changeto_1t1rarray[PHY_CHANGETO_1T1RARRAYLENGTH];
extern u32 rtl8192sephy_changeto_1t2rarray[PHY_CHANGETO_1T2RARRAYLENGTH];
extern u32 rtl8192sephy_reg_array_pg[PHY_REG_ARRAY_PGLENGTH];
extern u32 rtl8192seradioa_1t_array[RADIOA_1T_ARRAYLENGTH];
extern u32 rtl8192seradiob_array[RADIOB_ARRAYLENGTH];
extern u32 rtl8192seradiob_gm_array[RADIOB_GM_ARRAYLENGTH];
extern u32 rtl8192semac_2t_array[MAC_2T_ARRAYLENGTH];
extern u32 rtl8192seagctab_array[AGCTAB_ARRAYLENGTH];

#endif
