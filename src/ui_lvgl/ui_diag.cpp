// 24/10/2026 : diagnostic des blocages, version du simulateur PC (traces sur la sortie standard, pas de mémoire RTC ni de
// Telnet). L'ESP32 a la sienne : ui_diag_esp.cpp.
#ifndef ARDUINO
#include "ui_diag.h"
#include <stdarg.h>
#include <stdio.h>

void ui_trace(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
}
void ui_diag_etape(const char *) {}
void ui_diag_point(const char *) {}
void ui_diag_demarrer() {}
void ui_diag_tache() {}
void ui_diag_publier(bool (*)(const char *, const char *, bool), const char *) {}
const char *ui_diag_cause() { return ""; }
size_t ui_diag_traces(char *dst, size_t n)
{
    if (n)
        dst[0] = 0;
    return 0;
}
void ui_diag_traces_effacer() {}
void ui_diag_ram_etape(const char *) {}
uint32_t ui_diag_boucle_max() { return 0; }
const char *ui_diag_etape_precedente() { return ""; }
#endif
