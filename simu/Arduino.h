// 22/10/2026 : simulateur PC de l'interface LVGL (simu/). Remplace le seul bout d'Arduino dont lv_conf.h a besoin :
// l'horloge de LVGL (LV_TICK_CUSTOM -> millis()). Ici c'est une horloge VIRTUELLE que simu.cpp fait avancer,
// pour que les captures soient identiques d'une fois sur l'autre.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
#ifdef __cplusplus
}
#endif
