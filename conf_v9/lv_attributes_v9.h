/* 05/11/2026 — banc v9 (hors depot) : attributs repris de include/lv_conf.h 8.4 du depot
   (LV_ATTRIBUTE_MEM_ALIGN aligned(4), LV_ATTRIBUTE_FAST_MEM IRAM_ATTR, reste vide).
   En 9.6 ces options ne sont plus dans lv_conf.h : on les fournit par LV_ATTRIBUTE_CUSTOM_INCLUDE. */
#pragma once
#include "esp_attr.h"

#define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(4)))
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FLUSH_READY
#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR
