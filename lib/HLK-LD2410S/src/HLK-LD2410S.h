/**
 * @file LD2410S.h
 * @brief Driver library for LD2410S radar sensor (motion & distance)
 * @author PhuongNam720
 * @version 1.0
 * @date 2025-07-03
 */

#pragma once
#include <Arduino.h>

/**
 * @def LD2410S_ENABLE_LOG
 * @brief Enable or disable Serial debug logging (1 = enabled, 0 = disabled)
 */
#define LD2410S_ENABLE_LOG 0

#if LD2410S_ENABLE_LOG
#define LD2410S_LOG(...) Serial.printf(__VA_ARGS__)
#define LD2410S_LOG_LINE(x) Serial.println(x)
#else
#define LD2410S_LOG(...)
#define LD2410S_LOG_LINE(x)
#endif

/**
 * @brief Mode de sortie série du capteur.
 *
 * Minimal : trame courte usine (présence + distance), pensée pour la basse consommation.
 * PC      : trame "standard" documentée par Hi-Link comme destinée à l'outil PC officiel
 *           (HLK-LD2410S_TOOL) ; en plus de la présence/distance, elle transporte une
 *           valeur d'énergie par porte de détection (0 à 15).
 */
enum class LD2410SCommMode : uint8_t
{
    Minimal = 0,
    PC = 1
};

/**
 * @class LD2410S
 * @brief Class to communicate and configure the LD2410S radar sensor.
 */
class LD2410S
{
public:
    static constexpr uint8_t GATE_COUNT = 16;

  bool reboot();

/**
     * @brief Écrit les paramètres SNR de déclenchement (Trigger) et de maintien (Hold) pour les portes 8 à 15 (Commande 0x7400).
     * @param triggerSNR Tableau de 8 éléments contenant les valeurs SNR de déclenchement pour les portes 8 à 15
     * @param holdSNR Tableau de 8 éléments contenant les valeurs SNR de maintien pour les portes 8 à 15
     * @return true si le capteur valide la configuration (ACK reçu), false sinon.
     */
    bool writeSNRParameters(const uint8_t triggerSNR[8], const uint8_t holdSNR[8]);

    /**
     * @brief Lit les paramètres SNR de déclenchement et de maintien actuels pour les portes 8 à 15 (Commande 0x7500).
     * @param triggerSNROut Tableau de 8 éléments qui recevra les valeurs SNR de déclenchement lues
     * @param holdSNROut Tableau de 8 éléments qui recevra les valeurs SNR de maintien lues
     * @return true si la lecture réussit, false sinon.
     */
    bool readSNRParameters(uint8_t triggerSNROut[8], uint8_t holdSNROut[8]);

    /**
     * @brief 30/10/2026 : lecture brute des 16 paramètres (mots 0 à 15) d'une commande de lecture de seuils, sans
     * interprétation (0x73, 0x75 de la notice V1.2 ; 0x73, 0x77 du protocole V1.00 du 23/08/2024, firmware 1.1.1).
     * @note entre enterConfigMode() et exitConfigMode()
     */
    bool readParams16(uint16_t cmd, uint32_t out[16]);

    /**
     * @brief 30/10/2026 : seuils du firmware 1.1.1 (protocole V1.00 du 23/08/2024, tutoriel Hi-Link §3.1/§3.3.1) :
     * 16 seuils de DÉCLENCHEMENT (portes 0 à 15, 0x72 écrire / 0x73 lire) et 16 seuils de MAINTIEN (0x76 / 0x77), tous
     * 10-95 dB. Vérifié sur le capteur du séjour : 0x77 répond, et après un réglage auto il suit 0x73 à 1 dB près sur 14
     * portes sur 16 (facteurs 2 et 1). Les fonctions « Threshold/SNR » plus haut suivent la notice V1.2 de 2023 (anciens
     * firmwares 1.0.1/1.1.0) : sur le 1.1.1 elles écrivent le « maintien 0-7 » dans le déclenchement 8-15.
     * @note entre enterConfigMode() et exitConfigMode()
     */
    bool readTriggers16(uint8_t out[16]);
    bool readHolds16(uint8_t out[16]);
    bool writeTriggers16(const uint8_t v[16]);
    bool writeHolds16(const uint8_t v[16]);

/**
     * @brief Écrit les seuils de déclenchement (Trigger) et de maintien (Holding) pour les portes 0 à 7 (Commande 0x7200).
     * @param triggerThresholds Tableau de 8 éléments contenant les seuils de déclenchement (0 à 100)
     * @param holdingThresholds Tableau de 8 éléments contenant les seuils de maintien (0 à 100)
     * @return true si le capteur valide la configuration (ACK reçu), false sinon.
     */
    bool writeThresholdParameters(const uint8_t triggerThresholds[8], const uint8_t holdingThresholds[8]);

    /**
     * @brief Lit les seuils de déclenchement et de maintien actuels du capteur pour les portes 0 à 7 (Commande 0x7300).
     * @param triggerThresholdsOut Tableau de 8 éléments qui recevra les seuils de déclenchement lus
     * @param holdingThresholdsOut Tableau de 8 éléments qui recevra les seuils de maintien lus
     * @return true si la lecture réussit et que les données sont valides, false sinon.
     */
    bool readThresholdParameters(uint8_t triggerThresholdsOut[8], uint8_t holdingThresholdsOut[8]);  

    /**
     * @brief Constructor
     * @param serial Reference to HardwareSerial object
     * @param rxPin GPIO used for sensor TX
     * @param txPin GPIO used for sensor RX
     */
    LD2410S(HardwareSerial &serial, int rxPin, int txPin);

    /**
     * @brief Destructor
     */
    ~LD2410S();

    /**
     * @brief Initialize serial communication and enter config mode
     * @param baudrate Serial baudrate (default = 115200)
     * @return true if successful
     */
    bool begin(uint32_t baudrate = 115200); // set default baudrate 115200

    /**
     * @brief Read and process incoming frames (non-blocking)
     */
    void loop();

    /**
     * @brief Enter configuration mode
     * @return true if success
     */
    bool enterConfigMode();

    // 28/10/2026 : l'entrée en configuration ratait au 1er essai (diag radar du séjour : « le capteur ne répond pas »).
    // L'ancienne lecture prenait la réponse par blocs de 18 octets ALIGNÉS sur le début de ce qui attendait dans le tampon
    // série, sans le vider : des octets de trames du capteur devant l'accusé le décalaient. true = tampon vidé avant
    // l'envoi + en-tête cherché octet par octet ; false = ancienne lecture (essai A/B au banc)
    bool purgeAvantCommande = true;
    // 30/10/2026 : chaque trame complète reçue (minimale ou PC), octets tels quels, avant décodage (flux brut du journal)
    void (*surTrame)(const uint8_t *trame, size_t n) = nullptr;
    // 30/10/2026 : MESURE : réponses du capteur aux écritures et lectures de seuils, octets tels quels (relecture décalée
    // d'une porte après écriture : écriture ou lecture ?)
    void (*surEchange)(const char *quoi, const uint8_t *octets, size_t n) = nullptr;
    // 01/11/2026 : chaque commande envoyée (0x00FF = entrée en configuration, 0x00FE = sortie) et si elle a été acquittée
    void (*surCommande)(uint16_t cmd, bool ok) = nullptr;
    // 30/10/2026 : envois d'une commande restée sans réponse (1 = pas de renvoi) ; MESURE : octets reçus pendant la
    // dernière attente de réponse, reconnus ou non
    uint8_t envoisMax = 3;
    uint32_t renvois = 0; // MESURE : commandes renvoyées faute de réponse depuis le démarrage
    struct
    {
        uint16_t cmd;
        uint8_t recu[40];
        uint8_t nb;
    } dbgReponse = {};
    // MESURE de la dernière entrée en configuration : octets en attente avant l'envoi, octets reçus pendant l'attente
    struct
    {
        uint16_t avant;
        uint8_t recu[64];
        uint8_t nb;
        uint16_t ms;
    } dbgEntree = {};

    /**
     * @brief Exit configuration mode
     * @return true if success
     */
    bool exitConfigMode();

// Déclaration de la nouvelle fonction de reset
    bool resetToFactoryDefaults();

    /**
     * @brief Switch to standard output mode (detailed)
     * @return true if success
     */
    bool switchToStandardMode();

    /**
     * @brief Switch to minimal output mode (shorter packets)
     * @return true if success
     */
    bool switchToMinimalMode();

    /**
     * @brief Read firmware version of the sensor
     * @param major Major version
     * @param minor Minor version
     * @param patch Patch version
     * @return true if response received
     */
    bool readFirmwareVersion(uint16_t &major, uint16_t &minor, uint16_t &patch);

    /**
     * @brief Read configuration parameters from sensor
     * @param farthest_distance Max detection distance
     * @param nearest_distance Min detection distance
     * @param delay_time Delay before entering static state
     * @param freq_status Report frequency for motion/static state
     * @param freq_distance Report frequency for distance
     * @param respond_speed Sensor response speed (normal/fast)
     * @return true if successful
     */
    bool readCommonParametersCommand(uint32_t &farthest_distance, uint32_t &nearest_distance, uint32_t &delay_time, uint32_t &freq_status, uint32_t &freq_distance, uint32_t &respond_speed);

    /**
     * @brief Read 8-byte serial number
     * @param buffer Buffer to store null-terminated serial number
     * @param len Buffer size (must be at least 9)
     * @return true if successful
     */
    bool readSerialNumber(char *buffer, size_t len);

    /**
     * @brief Check if motion is currently detected
     * @return true if motion detected
     */
    bool isMotionDetected() const;

    /**
     * @brief Get target distance
     * @return Distance in cm
     */
    uint16_t getDistance() const;

    /**
     * @brief Bascule le capteur en mode Minimal ou PC (Standard) et mémorise le mode courant.
     * @note Doit être appelé entre enterConfigMode() et exitConfigMode(), comme
     *       switchToMinimalMode()/switchToStandardMode() qu'elle encapsule.
     * @return true si le capteur a confirmé le changement de mode.
     */
    bool setCommMode(LD2410SCommMode mode);

    /**
     * @brief Dernier mode de sortie appliqué avec succès via setCommMode().
     */
    LD2410SCommMode commMode() const;

    /**
     * @brief État brut de cible (0/1 = personne absente, 2/3 = personne présente)
     * renvoyé par la dernière trame reçue (minimale ou standard).
     */
    uint8_t rawTargetState() const;

    /**
     * @brief true si au moins une trame "standard" (mode PC) a été reçue et décodée,
     * c'est-à-dire si l'énergie par porte est disponible.
     */
    bool hasEngineeringData() const;
    uint32_t pcFrameCount() const { return _pcFrameCount; } // 28/10/2026 : trames PC décodées (mesure du bruit)
    uint32_t frameCount() const { return _frameCount; }     // 30/10/2026 : toutes les trames reçues (minimales ou PC)
    uint32_t byteCount() const { return _byteCount; }       // 01/11/2026 : octets lus par loop() (trames ou pas)

    /**
     * @brief Valeur d'énergie de la porte donnée (0 à GATE_COUNT-1), issue de la dernière
     * trame standard reçue. N'a de sens qu'en mode PC (voir hasEngineeringData()).
     *
     * @note 26/10/2026 : en dB (0 à 100), comparable aux seuils. Chaque porte est un entier 32 bits little-endian,
     * une amplitude ×100 (les valeurs relevées sont des 100·√(I²+Q²) : 223, 316, 583…), d'où dB = 20·log10(brut/100).
     * Vérifié sur le capteur du séjour (firmware 1.1.1) : la table brut -> dB de l'outil Hi-Link publiée sur
     * ask.hlktech.com/question/178914 à 0,4 dB près ; pièce vide, aucune trame au-dessus du seuil de déclenchement
     * (157/157) ; « présence », une porte au-dessus du seuil de maintien (50/52). L'ancienne valeur (le premier
     * octet du bloc) n'avait pas de sens : elle sautait de 0 à 255 au moindre bruit.
     */
    uint8_t gateEnergy(uint8_t gate) const;

    /**
     * @brief 26/10/2026 : la valeur brute de la porte (entier 32 bits little-endian, amplitude ×100).
     */
    uint32_t gateEnergyRaw(uint8_t gate) const;

    /**
     * @brief Lance la calibration automatique des seuils (commande 0x0009).
     *
     * Le capteur écoute l'environnement pendant scanTimeSec secondes et calcule
     * lui-même les seuils de déclenchement/maintien de chaque porte à partir du
     * bruit ambiant mesuré.
     *
     * @warning La pièce doit être vide de toute personne pendant toute la durée du
     * scan : une présence pendant la mesure fausserait le bruit de fond de référence
     * et donc les seuils calculés.
     * @note Doit être appelé entre enterConfigMode() et exitConfigMode(). La
     * progression est ensuite rapportée de façon asynchrone via loop() ; suivre
     * autoThresholdProgress()/isAutoThresholdRunning().
     * @param triggerFactor Facteur de marge pour le seuil de déclenchement (exemple doc : 2)
     * @param retentionFactor Facteur de marge pour le seuil de maintien (exemple doc : 1)
     * @param scanTimeSec Durée du scan en secondes (exemple doc : 120)
     * @return true si le capteur a accepté la commande.
     */
    bool startAutoThreshold(uint16_t triggerFactor, uint16_t retentionFactor, uint16_t scanTimeSec);

    /// Progression de la calibration auto en cours, de 0.0 à 100.0 (%).
    float autoThresholdProgress() const;

    /// true tant que la calibration auto en cours n'est pas terminée.
    bool isAutoThresholdRunning() const;

    /// 30/10 : le capteur a cessé d'envoyer la progression (resté à 51 % sur le séjour, seuils inchangés) : on n'attend plus
    /// les 100 % qui ne viendront pas.
    void abandonAutoThreshold() { _autoThresholdRunning = false; }

    /**
     * @brief Write legacy generic parameters (fixed default configuration)
     * @return true if successful
     */
    bool writeGenericParametersCommand();

    /**
     * @brief Configure custom general parameters for the sensor (Command 0x7000).
     * * @param maxDistCm        Farthest distance gate [Range: 1 ~ 16] (Unit: Gate)
     * @param minDistCm        Nearest distance gate [Range: 0 ~ 16] (Unit: Gate)
     * @param delaySec         Unmanned delay time [Range: 10 ~ 120] (Unit: Seconds)
     * @param statusFreqHz     Trigger refresh rate / Status reporting frequency [Range: 0.5 ~ 8.0] (Step: 0.5) (Unit: Hz)
     * @param distanceFreqHz   Maintain refresh rate / Distance reporting frequency [Range: 0.5 ~ 8.0] (Step: 0.5) (Unit: Hz)
     * @param speedMode        Response speed [Values: 5 (Normal) or 10 (Fast)]
     * @return true if configuration succeeded (ACK 0x0100 received), false if failed or timeout.
     */
    bool setCustomParameters(uint16_t maxDistCm, uint16_t minDistCm, uint16_t delaySec, float statusFreqHz, float distanceFreqHz, uint8_t speedMode);
    
private:
    /// 30/10/2026 : trame entière envoyée d'un seul write, puis SA réponse cherchée par en-tête et écho du mot de commande
    size_t commande(uint16_t cmd, const uint8_t *donnees, size_t n, uint8_t *rep, size_t max, uint32_t attente_ms);
    size_t attendre(uint16_t cmd, uint8_t *rep, size_t max, uint32_t attente_ms); // la réponse à cmd, 0 sans réponse

    /**
     * @brief Parse minimal output frame
     * @param frame Data frame
     * @param len Length
     * @return true if successfully parsed
     */
    bool parseMinimalFrame(uint8_t *frame, size_t len);

    /**
     * @brief Parse standard/"PC" output frame (F4 F3 F2 F1 ... F8 F7 F6 F5).
     * @param frame Data frame (en-tête + longueur inclus)
     * @param len Longueur totale de la trame
     * @return true if successfully parsed
     */
    bool parseStandardFrame(uint8_t *frame, size_t len);

    /**
     * @brief Write frame header: FD FC FB FA
     */
    void sendFrameHead();

    /**
     * @brief Write frame footer: 04 03 02 01
     */
    void sendFrameEnd();


    HardwareSerial &_serial; ///< Reference to serial port
    int _rxPin;              ///< GPIO used for sensor TX
    int _txPin;              ///< GPIO used for sensor RX
    bool _motion = false;    ///< Internal motion state
    uint16_t _distance = 0;  ///< Last known distance
    uint8_t _lastTargetState = 0;                  ///< 0/1 = absent, 2/3 = présent
    LD2410SCommMode _commMode = LD2410SCommMode::Minimal;
    bool _pcFrameReceived = false;                 ///< Au moins une trame standard décodée
    uint32_t _pcFrameCount = 0;
    uint32_t _frameCount = 0;
    uint32_t _byteCount = 0;
    uint8_t _gateEnergy[GATE_COUNT] = {0};          ///< Énergie par porte (mode PC uniquement)
    uint32_t _gateEnergyRaw[GATE_COUNT] = {0};      ///< Les 4 octets de chaque porte
    uint16_t _autoThresholdProgressPercent = 0;     ///< Progression auto-réglage en % (0-100, valeur brute du capteur)
    bool _autoThresholdRunning = false;             ///< true tant que l'auto-réglage n'est pas fini
};