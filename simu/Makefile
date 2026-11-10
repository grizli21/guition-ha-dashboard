# 08/11/2026 — simulateur PC de l'interface LVGL, en LVGL **9.6** (la prod de ce soir). Voir simu.cpp.
#   make -C simu          construit puis lance : PNG dans simu/captures/
#   make -C simu asan     même chose avec AddressSanitizer + UBSan (construit à part : construction-asan, simu-asan)
#   make -C simu propre   efface la construction (PAS simu/captures : ce sont les rendus qu'on compare à l'écran)
#
# La ligne 8.4 est morte (les icônes de lib/ressources sont au format 9.6, `icon_ampoule.c:285 : lv_image_dsc_t inconnu`) :
# elle est gardée telle quelle dans `simu/Makefile.84`, avec ses dix cibles annexes (trace, comptes, sonde, defile, verif,
# verif_piege, verif_cas, pages, pages_t, pages_force) dont les wraps au lien portent sur des symboles 8.4
# (_lv_disp_refr_timer, _lv_inv_area, lv_disp_drv_register) sans équivalent sous 9.6.
RACINE  := ..
LVGL    := $(RACINE)/lib/lvgl-9.6.0
CONF    := conf_v9_pc   # copie de conf_v9/lv_conf.h (la conf de la prod), malloc de la libc, log coupé
CONSTR  := construction

# $(CONF) AVANT $(RACINE)/include : le second contient le lv_conf.h de la 8.4, il ne doit pas être vu
INC := -I. -I$(CONF) -I$(RACINE)/include -I$(LVGL) -I$(RACINE)/lib/icones -I$(RACINE)/lib/ressources \
       -I$(RACINE)/lib/ArduinoJson-7.4.2/src -I$(RACINE)/lib/JPEGDEC-1.8.4/src
# lv_conf.h attend l'Arduino de l'ESP32 : horloge -> Arduino.h de ce dossier, ps_malloc -> malloc du PC
# JPEGDEC : __LINUX__ = version PC ; NO_SIMD = version C de référence (sa version SSE ignore RGB565_BIG_ENDIAN :
# couleurs brouillées au simulateur, 23/10 ; sur l'ESP32-S3 sa version SIMD le respecte, comme la production)
DEFS := -D__LINUX__ -DNO_SIMD -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
       -Dps_malloc=malloc -Dps_realloc=realloc -Dps_free=free -Dps_calloc=calloc
# make -C simu POLICE_3X5=1 : police 3x5 de K-Type (option, voir tools/fonts/README.md) au lieu de Montserrat Bold
ifdef POLICE_3X5
DEFS += -DPOLICE_3X5
endif
# 08/11 : ps_malloc seul était remplacé ; ui_musique.cpp (9.6) appelle aussi ps_realloc (les tableaux
# d'albums gardés entre deux versions), et ui_psram.h du PC ne définit pas ces noms-là.
# -MMD -MP : chaque .o dépend de ses en-têtes (24/10 : un en-tête modifié laissait des .o périmés)
# 10/11/2026 : UI_LANG_EN=1 construit l'interface en ANGLAIS
# (macro TXT de src/ui_lvgl/ui_langue.h). Sans cette variable : français, rendu STRICTEMENT identique.
#   make -C simu UI_LANG_EN=1
# Dossier de construction et binaire séparés (construction-en, simu-en) : make ne suit pas les drapeaux
# de DEFS ; sans ça, changer UI_LANG_EN laisserait un binaire moitié FR, moitié EN.
ifeq ($(UI_LANG_EN),1)
DEFS += -DUI_LANG_EN
CONSTR := construction-en
BIN := simu-en
endif

CFLAGS   := -O2 -w -MMD -MP $(INC) $(DEFS)
CXXFLAGS := -O2 -std=c++17 -Wall -Wno-unused-function -MMD -MP $(INC) $(DEFS)
# make asan : SAN doit aussi passer à la COMPILATION (avant le 10/11, seulement à l'édition de liens :
# ASan ne surveillait pas le code des pages)
SAN ?=
BIN ?= simu

SRC_C   := $(shell find $(LVGL)/src -name '*.c') \
           $(wildcard $(RACINE)/lib/icones/*.c) $(wildcard $(RACINE)/lib/ressources/*.c) $(wildcard donnees/*.c)
SRC_CPP := $(wildcard $(RACINE)/src/ui_lvgl/*.cpp) $(RACINE)/lib/JPEGDEC-1.8.4/src/JPEGDEC.cpp simu.cpp

OBJ := $(patsubst $(RACINE)/%,$(CONSTR)/%,$(SRC_C:.c=.o)) \
       $(patsubst $(RACINE)/%,$(CONSTR)/%,$(filter $(RACINE)/%,$(SRC_CPP:.cpp=.o))) \
       $(CONSTR)/simu.o $(CONSTR)/maquettes_eau.o $(CONSTR)/maquettes_batterie.o $(CONSTR)/maquettes_pc.o

.PHONY: tout propre asan

tout: $(BIN)
	./$(BIN)

$(BIN): $(OBJ)
	$(CXX) $(SAN) -o $@ $^ -lz -lm

asan:
	$(MAKE) CONSTR=construction-asan BIN=simu-asan SAN="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize-recover=address"

$(CONSTR)/%.o: $(RACINE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SAN) -c $< -o $@

$(CONSTR)/%.o: $(RACINE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) -c $< -o $@

# 08/11 : les .o de donnees/ ne sont plus suivis par git (artefacts de compilation) : règle explicite,
# sinon make retombait sur sa règle implicite, sans les -I, et la compilation echouait
donnees/%.o: donnees/%.c
	$(CC) $(CFLAGS) $(SAN) -c $< -o $@

$(CONSTR)/simu.o: simu.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) -c $< -o $@

$(CONSTR)/maquettes_eau.o: maquettes_eau.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) -c $< -o $@

$(CONSTR)/maquettes_batterie.o: maquettes_batterie.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) -c $< -o $@

$(CONSTR)/maquettes_pc.o: maquettes_pc.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SAN) -c $< -o $@

-include $(OBJ:.o=.d)

propre:
	rm -rf $(CONSTR) construction-asan construction-en simu simu-asan simu-en
