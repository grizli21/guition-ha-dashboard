#include "HLK-LD2410S.h"
#include <math.h>
#include <string.h>

static uint32_t parseLEUint32(const uint8_t *data);

/*
    Command frame : FD FC FB FA xx xx ... xx 04 03 02 01
*/


/**
 * @brief Constructs an LD2410S object with the specified serial interface and pin configuration.
 *
 * Initializes the LD2410S sensor driver, associating it with the provided HardwareSerial instance
 * and configuring the RX and TX pins for communication.
 *
 * @param serial Reference to a HardwareSerial object used for UART communication with the sensor.
 * @param rxPin GPIO pin number used for receiving data from the sensor.
 * @param txPin GPIO pin number used for transmitting data to the sensor.
 */
LD2410S::LD2410S(HardwareSerial &serial, int rxPin, int txPin)
    : _serial(serial), _rxPin(rxPin), _txPin(txPin), _motion(false), _distance(0) {}


/**
 * @brief Destructor for the LD2410S class.
 *
 * Cleans up any resources allocated by the LD2410S instance.
 * As this destructor is defaulted, no custom cleanup is performed.
 */
LD2410S::~LD2410S() = default;


/**
 * @brief Initializes the LD2410S sensor and configures its communication parameters.
 *
 * This function sets up the serial communication with the specified baud rate and pin configuration,
 * attempts to enter configuration mode (with up to two retries), writes generic parameters to the sensor,
 * and switches the sensor to minimal data output mode. Delays are used to ensure proper timing between operations.
 *
 * @param baudrate The baud rate for serial communication with the LD2410S sensor.
 * @return true if initialization and configuration succeed; false otherwise.
 */
bool LD2410S::begin(uint32_t baudrate)
{
    _serial.begin(baudrate, SERIAL_8N1, _rxPin, _txPin);
    /*delay(100);

    for (int i = 0; i < 2; ++i)
    {
        if (enterConfigMode())
            break;
        LD2410S_LOG_LINE("Enter config mode failed!. Try again");
        delay(100);
        if (i == 1)
            return false;
    }

    delay(100);
    if (!writeGenericParametersCommand())
    {
        LD2410S_LOG_LINE("Write generic parameters failed!");
        return false;
    }

    if (!switchToMinimalMode())
    {
        LD2410S_LOG_LINE("Set to minimal data output mode failed!");
    }
    delay(100);
    return true;*/
    return true; // 26/10/2026 (projet lvgl) : manquait (le compilateur de l'IDF 5 refuse une fonction bool sans return)
}

/**
 * @brief Redémarre le capteur LD2410S par voie logicielle sans couper l'alimentation.
 * * Cette fonction bascule d'abord le capteur en mode configuration, puis lui transmet 
 * la commande officielle de redémarrage (0x00A3). Elle attend ensuite l'ACK de confirmation
 * renvoyé par le matériel avant l'extinction effective de son processeur.
 * * @return true si la commande a été acceptée et confirmée par le capteur, false sinon.
 */
/*bool LD2410S::reboot()
{
    // 1. Nettoyage initial du buffer de réception
    while(_serial.available()) _serial.read();

    // 2. Le capteur doit impérativement être en mode configuration
    if (!enterConfigMode())
    {
        LD2410S_LOG_LINE("Reboot impossible : Échec de l'entrée en mode configuration.");
        return false;
    }

    // Petit délai de tolérance pour laisser le processeur du LD2410S respirer
    delay(50);

    // 3. Envoi de la trame de redémarrage
    sendFrameHead();

    // Longueur des données suivantes : 0 octet (0x00, 0x00) pour le LD2410S sur cette commande
    // Mot de commande de redémarrage : 0x00A3 (Little-Endian -> A3 00)
    // Contrairement aux autres commandes, le reboot n'a pas besoin de bloc de paramètre de 2 octets.
    uint8_t cmdHeader[4] = {0x00, 0x00, 0xA3, 0x00};
    _serial.write(cmdHeader, sizeof(cmdHeader));

    sendFrameEnd();
    _serial.flush();

    // 4. Attente de l'ACK de confirmation
    // Sur le LD2410S, l'ACK peut faire entre 10 et 14 octets selon le firmware.
    constexpr size_t MAX_ACK_SIZE = 14;
    uint8_t ack[MAX_ACK_SIZE];
    size_t bytesRead = 0;
    
    unsigned long start = millis();
    while (millis() - start < 500 && bytesRead < MAX_ACK_SIZE)
    {
        if (_serial.available() > 0)
        {
            ack[bytesRead++] = _serial.read();
        }
        yield();
    }

    // Log de débug pour voir ce que le capteur renvoie réellement en cas d'échec
    LD2410S_LOG("ACK Reboot reçu (%d octets): ", bytesRead);
    for (size_t i = 0; i < bytesRead; i++) {
        LD2410S_LOG("%02X ", ack[i]);
    }
    LD2410S_LOG_LINE("");

    // 5. Validation de la trame (Entête + Commande 0xA3)
    if (bytesRead >= 8 &&
        ack[0] == 0xFD && ack[1] == 0xFC && ack[2] == 0xFB && ack[3] == 0xFA &&
        ack[6] == 0xA3)
    {
        LD2410S_LOG_LINE("Commande de Reboot acceptée. Redémarrage du matériel...");
        delay(200); // Laisse le temps au hardware de couper
        return true;
    }
    
    LD2410S_LOG_LINE("Erreur : Trame de reboot rejetée ou non reconnue.");
    return false;
}*/

/**
 * @brief Switches the LD2410S device to standard output mode.
 *
 * This function sends the appropriate command sequence to the LD2410S device
 * over the configured serial interface to switch it into standard data output mode.
 * It waits for an acknowledgment frame from the device to confirm the mode change.
 *
 * @note The function waits up to 1 second for the acknowledgment response.
 *
 * @return true if the device successfully switches to standard output mode, false otherwise.
 */
bool LD2410S::switchToStandardMode()
{
    const uint8_t param[6] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    uint8_t ack[32];
    return commande(0x007A, param, sizeof(param), ack, sizeof(ack), 1000) >= 10;
}

/**
 * @brief Switches the LD2410S device to minimal output mode.
 *
 * This function sends a command frame to the LD2410S device to change its output mode to minimal.
 * It writes the appropriate command and parameters to the serial interface, then waits for an
 * acknowledgment response from the device. The function checks the response for specific header
 * and command bytes to confirm the mode change was successful.
 *
 * @return true if the device successfully switches to minimal output mode, false otherwise.
 */
bool LD2410S::switchToMinimalMode()
{
    const uint8_t param[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t ack[32];
    return commande(0x007A, param, sizeof(param), ack, sizeof(ack), 1000) >= 10;
}

/**
 * @brief read data serial continously
 */
/**
 * @brief Processes incoming serial data to detect and parse minimal data frames from the LD2410S sensor.
 *
 * This function should be called repeatedly (typically in the main loop) to read bytes from the serial interface.
 * It accumulates bytes into a buffer when a frame start byte (0x6E) is detected, and continues until a frame end
 * byte (0x62) is found or the maximum frame size is reached. Upon detecting a complete frame, it logs the frame
 * contents and invokes the minimal frame parser.
 *
 * The function is non-blocking and processes all available bytes in the serial buffer during each call.
 *
 * @note The function uses static variables to maintain state between calls, allowing it to handle frames that
 *       span multiple invocations.
 */
void LD2410S::loop()
{
    // Deux formats de trame "rapportée" possibles selon le mode courant (voir setCommMode) :
    //  - Minimale : 6E <état:1> <distance LE:2> 62                          (5 octets fixes)
    //  - Standard/"PC" : F4 F3 F2 F1 <longueur LE:2> <payload...> F8 F7 F6 F5
    // La longueur de la trame standard est donnée par son champ "longueur", on ne peut
    // donc pas se contenter de chercher un octet de fin comme pour la trame minimale
    // (un octet de payload pourrait coïncider par hasard avec un octet de fin).
    static constexpr size_t MAX_FRAME_SIZE = 96;
    static uint8_t buffer[MAX_FRAME_SIZE];
    static size_t pos = 0;
    static const uint8_t STD_HEAD[4] = {0xF4, 0xF3, 0xF2, 0xF1};
    static const uint8_t STD_END[4] = {0xF8, 0xF7, 0xF6, 0xF5};
    enum class Kind
    {
        None,
        Minimal,
        Standard
    };
    static Kind kind = Kind::None;

    while (_serial.available())
    {
        uint8_t byte = _serial.read();
        _byteCount++;

        if (kind == Kind::None)
        {
            if (byte == 0x6E)
            {
                kind = Kind::Minimal;
                pos = 0;
                buffer[pos++] = byte;
            }
            else if (byte == STD_HEAD[0])
            {
                kind = Kind::Standard;
                pos = 0;
                buffer[pos++] = byte;
            }
            continue;
        }

        if (pos >= MAX_FRAME_SIZE)
        {
            // Trame corrompue/trop longue : on resynchronise
            kind = Kind::None;
            pos = 0;
            continue;
        }
        buffer[pos++] = byte;

        if (kind == Kind::Minimal)
        {
            if (byte == 0x62 || pos >= 5)
            {
                if (pos == 5 && buffer[4] == 0x62)
                {
                    LD2410S_LOG_LINE("Minimal Frame:");
                    for (size_t i = 0; i < pos; i++)
                        LD2410S_LOG("%02X ", buffer[i]);
                    LD2410S_LOG_LINE();
                    _frameCount++;
                    if (surTrame)
                        surTrame(buffer, pos);
                    parseMinimalFrame(buffer, pos);
                }
                kind = Kind::None;
                pos = 0;
            }
        }
        else // Kind::Standard
        {
            // Vérifie les 4 octets d'en-tête au fur et à mesure ; abandonne sinon
            if (pos <= 4)
            {
                if (buffer[pos - 1] != STD_HEAD[pos - 1])
                {
                    kind = Kind::None;
                    pos = 0;
                }
                continue;
            }

            if (pos < 6)
                continue; // attend les 2 octets de longueur

            uint16_t payloadLen = static_cast<uint16_t>(buffer[4]) | (static_cast<uint16_t>(buffer[5]) << 8);
            size_t totalLen = 6 + static_cast<size_t>(payloadLen) + 4;

            if (totalLen > MAX_FRAME_SIZE)
            {
                LD2410S_LOG_LINE("ERROR: standard frame too long, resync");
                kind = Kind::None;
                pos = 0;
                continue;
            }

            if (pos >= totalLen)
            {
                if (buffer[totalLen - 4] == STD_END[0] && buffer[totalLen - 3] == STD_END[1] &&
                    buffer[totalLen - 2] == STD_END[2] && buffer[totalLen - 1] == STD_END[3])
                {
                    LD2410S_LOG_LINE("Standard (PC) Frame:");
                    for (size_t i = 0; i < totalLen; i++)
                        LD2410S_LOG("%02X ", buffer[i]);
                    LD2410S_LOG_LINE();
                    _frameCount++;
                    if (surTrame)
                        surTrame(buffer, totalLen);
                    parseStandardFrame(buffer, totalLen);
                }
                kind = Kind::None;
                pos = 0;
            }
        }
    }
}


/**
 * @brief extract minimal frame 6E ... 62
 * @param frame
 * @param len
 * @return true
 * @return false
 */
/**
 * @brief Parses a minimal data frame from the LD2410S sensor and updates motion and distance state.
 *
 * This function checks the validity of the provided frame based on expected header and footer bytes.
 * It then interprets the target state to determine if motion is detected, and extracts the distance value.
 *
 * @param frame Pointer to the buffer containing the frame data.
 * @param len Length of the frame buffer. Must be at least 5 bytes.
 * @return true if the frame is valid and successfully parsed; false otherwise.
 *
 * The function updates the internal motion state (`_motion`) and distance (`_distance`) members.
 * - Motion is set to true if the target state is 2 or 3, false if 0 or 1.
 * - Distance is extracted from bytes 2 and 3 (little-endian).
 */

bool LD2410S::parseMinimalFrame(uint8_t *frame, size_t len)
{
    if (len < 5)
        return false;

    if (frame[0] != 0x6E || frame[4] != 0x62)
        return false;

    uint8_t target_state = frame[1];

    if (target_state == 0 || target_state == 1)
    {
        _motion = false;
    }
    else if (target_state == 2 || target_state == 3)
    {
        _motion = true;
    }
    else
    {
        return false;
    }

    _lastTargetState = target_state;
    _distance = (static_cast<uint16_t>(frame[3]) << 8) | frame[2];
    return true;
}

/**
 * @brief Parses a standard/"PC" output frame (F4 F3 F2 F1 ... F8 F7 F6 F5).
 *
 * Deux types de trame partagent cet en-tête (Table 2-1 du protocole série) :
 *  - type 0x01 "rapport de cible" : état de cible (1 octet), distance (2 octets LE),
 *    bits réservés (2 octets), puis l'énergie de chaque porte de détection (0 à 15)
 *    sur 4 octets chacun (voir note doxygen de gateEnergy() sur l'encodage supposé).
 *  - type 0x03 "progression de calibration auto" : 2 octets = progression * 100
 *    (10000 = 100%), émis uniquement pendant/à la suite de startAutoThreshold().
 *
 * @param frame Trame complète (en-tête + longueur inclus)
 * @param len Longueur totale de la trame
 * @return true si la trame a été reconnue et décodée
 */
bool LD2410S::parseStandardFrame(uint8_t *frame, size_t len)
{
    static constexpr size_t HEADER_LEN = 6; // F4 F3 F2 F1 + longueur (2 octets)
    static constexpr size_t FOOTER_LEN = 4; // F8 F7 F6 F5

    if (len < HEADER_LEN + 1 + FOOTER_LEN)
        return false;

    uint16_t payloadLen = static_cast<uint16_t>(frame[4]) | (static_cast<uint16_t>(frame[5]) << 8);
    const uint8_t *payload = &frame[HEADER_LEN];

    if (payload[0] == 0x03)
    {
        // Progression de l'auto-réglage des seuils (2 octets). Constat sur capteur réel :
        // malgré le "*100" du nom dans la doc Hi-Link, la valeur transportée est déjà un
        // pourcentage direct 0-100 (et non un centième de pourcentage 0-10000).
        if (payloadLen < 3)
            return false;
        uint16_t progressPercent = static_cast<uint16_t>(payload[1]) | (static_cast<uint16_t>(payload[2]) << 8);
        _autoThresholdProgressPercent = progressPercent;
        _autoThresholdRunning = (progressPercent < 100);
        Serial.printf("[LD2410S] Progression auto-reglage : %u%%\n", progressPercent);
        return true;
    }

    if (payload[0] != 0x01)
    {
        // Log non gate (diagnostic) : type de trame standard non géré par cette lib.
        // Utile pour vérifier l'hypothèse d'encodage du champ "Data type" (1 octet ici)
        // si l'auto-réglage ou l'énergie par porte semblent ne jamais se mettre à jour.
        Serial.printf("[LD2410S] Trame standard type inconnu (0x%02X), %u octets brut : ", payload[0], (unsigned)len);
        for (size_t i = 0; i < len; i++)
            Serial.printf("%02X ", frame[i]);
        Serial.println();
        return false;
    }

    if (payloadLen < 6) // état(1) + distance(2) + réservé(2), au minimum
        return false;

    uint8_t target_state = payload[1];
    if (target_state > 3)
        return false;

    _lastTargetState = target_state;
    _motion = (target_state == 2 || target_state == 3);
    _distance = (static_cast<uint16_t>(payload[3]) << 8) | payload[2];

    // Énergie par porte : cf. note doxygen de gateEnergy() sur l'encodage supposé
    size_t energyOffset = 1 + 1 + 2 + 2; // type + état + distance + réservé
    size_t availableEnergyBytes = (payloadLen > energyOffset) ? (payloadLen - energyOffset) : 0;
    size_t gatesAvailable = availableEnergyBytes / 4;
    if (gatesAvailable > GATE_COUNT)
        gatesAvailable = GATE_COUNT;

    for (uint8_t g = 0; g < GATE_COUNT; g++)
    {
        const uint8_t *e = &payload[energyOffset + g * 4];
        _gateEnergyRaw[g] = (g < gatesAvailable) ? (uint32_t)e[0] | (uint32_t)e[1] << 8 | (uint32_t)e[2] << 16 | (uint32_t)e[3] << 24 : 0;
        // 26/10/2026 : en dB, comme l'outil Hi-Link (voir gateEnergy())
        const float db = _gateEnergyRaw[g] > 100 ? 20.0f * log10f(_gateEnergyRaw[g] / 100.0f) : 0.0f;
        _gateEnergy[g] = (uint8_t)(db > 100.0f ? 100 : db + 0.5f);
    }

    _pcFrameReceived = true;
    _pcFrameCount++;
    return true;
}

/**
 * @brief Checks if motion has been detected by the LD2410S sensor.
 *
 * @return true if motion is currently detected; false otherwise.
 */
bool LD2410S::isMotionDetected() const
{
    return _motion;
}


/**
 * @brief Retrieves the last measured distance value.
 *
 * This method returns the most recent distance measurement obtained by the LD2410S sensor.
 *
 * @return The distance value in units defined by the sensor (typically millimeters).
 */
uint16_t LD2410S::getDistance() const
{
    return _distance;
}

/**
 * @brief Bascule le mode de sortie du capteur et mémorise le mode obtenu en cas de succès.
 */
bool LD2410S::setCommMode(LD2410SCommMode mode)
{
    bool ok = (mode == LD2410SCommMode::PC) ? switchToStandardMode() : switchToMinimalMode();
    if (ok)
    {
        _commMode = mode;
        if (mode == LD2410SCommMode::Minimal)
            _pcFrameReceived = false; // les données d'énergie deviennent obsolètes
    }
    return ok;
}

LD2410SCommMode LD2410S::commMode() const
{
    return _commMode;
}

uint8_t LD2410S::rawTargetState() const
{
    return _lastTargetState;
}

bool LD2410S::hasEngineeringData() const
{
    return _pcFrameReceived;
}

uint8_t LD2410S::gateEnergy(uint8_t gate) const
{
    return (gate < GATE_COUNT) ? _gateEnergy[gate] : 0;
}

uint32_t LD2410S::gateEnergyRaw(uint8_t gate) const
{
    return (gate < GATE_COUNT) ? _gateEnergyRaw[gate] : 0;
}

/**
 * @brief Envoie la commande d'auto-réglage des seuils (0x0009).
 * @note Doit être appelé entre enterConfigMode() et exitConfigMode().
 */
bool LD2410S::startAutoThreshold(uint16_t triggerFactor, uint16_t retentionFactor, uint16_t scanTimeSec)
{
    const uint8_t params[6] = {
        static_cast<uint8_t>(triggerFactor & 0xFF), static_cast<uint8_t>((triggerFactor >> 8) & 0xFF),
        static_cast<uint8_t>(retentionFactor & 0xFF), static_cast<uint8_t>((retentionFactor >> 8) & 0xFF),
        static_cast<uint8_t>(scanTimeSec & 0xFF), static_cast<uint8_t>((scanTimeSec >> 8) & 0xFF)};
    // taille d'ACK non documentée : seul l'écho du mot de commande (0x0901) est vérifié
    uint8_t ack[32];
    const bool ok = commande(0x0009, params, sizeof(params), ack, sizeof(ack), 500) >= 8;
    if (ok)
    {
        _autoThresholdRunning = true;
        _autoThresholdProgressPercent = 0;
    }
    return ok;
}

float LD2410S::autoThresholdProgress() const
{
    return static_cast<float>(_autoThresholdProgressPercent);
}

bool LD2410S::isAutoThresholdRunning() const
{
    return _autoThresholdRunning;
}


/**
 * @brief Sends the frame header sequence to the connected serial device.
 *
 * This function writes a predefined 4-byte header (0xFD, 0xFC, 0xFB, 0xFA)
 * to the serial interface. The header is typically used to indicate the
 * start of a new data frame in communication with the LD2410S device.
 */
void LD2410S::sendFrameHead()
{
    static const uint8_t head[4] = {0xFD, 0xFC, 0xFB, 0xFA};
    _serial.write(head, sizeof(head));
}


/**
 * @brief Sends the end-of-frame sequence to the connected device.
 *
 * This function writes a predefined 4-byte tail sequence (0x04, 0x03, 0x02, 0x01)
 * to the serial interface, indicating the end of a data frame for the LD2410S device.
 */
void LD2410S::sendFrameEnd()
{
    static const uint8_t tail[4] = {0x04, 0x03, 0x02, 0x01};
    _serial.write(tail, sizeof(tail));
}


/**
 * @brief 30/10/2026 : envoie une commande et attend SA réponse, comme ESPHome (ld2410s de Mike Lawrence, ld2410).
 *
 * La trame entière (FD FC FB FA, longueur, mot de commande, données, 04 03 02 01) est construite puis envoyée d'un seul
 * write (l'ancien code l'écrivait en 19 morceaux) ; la réponse est cherchée octet par octet : en-tête FD FC FB FA, longueur,
 * puis mot de commande renvoyé = commande | 0x0100 (une trame d'un autre type ou la réponse à une autre commande est
 * ignorée, au lieu d'un bloc de taille fixe pris au début du tampon).
 * @return taille de la réponse (en-tête et fin compris), 0 sans réponse
 */
size_t LD2410S::commande(uint16_t cmd, const uint8_t *donnees, size_t n, uint8_t *rep, size_t max, uint32_t attente_ms)
{
    uint8_t t[128];
    if (n + 12 > sizeof(t) || max < 12)
        return 0;
    static const uint8_t TETE[4] = {0xFD, 0xFC, 0xFB, 0xFA}, FIN[4] = {0x04, 0x03, 0x02, 0x01};
    size_t k = 0;
    memcpy(t, TETE, 4);
    k = 4;
    t[k++] = (uint8_t)((2 + n) & 0xFF);
    t[k++] = (uint8_t)((2 + n) >> 8);
    t[k++] = cmd & 0xFF;
    t[k++] = cmd >> 8;
    if (n)
        memcpy(t + k, donnees, n);
    k += n;
    memcpy(t + k, FIN, 4);
    k += 4;
    // 30/10/2026 : renvoyée jusqu'à 3 fois sans réponse (ESPHome ld2410s : 5) ; vu sur le séjour juste après un réglage
    // auto : lecture 0x73 restée sans réponse 600 ms, 0x77 répondue aussitôt après
    for (int essai = 0; essai < (envoisMax ? envoisMax : 1); essai++)
    {
        if (essai)
            renvois++;
        while (_serial.available())
            _serial.read();
        _serial.write(t, k);
        _serial.flush();
        const size_t n = attendre(cmd, rep, max, attente_ms);
        if (n)
        {
            if (surCommande)
                surCommande(cmd, true);
            return n;
        }
    }
    if (surCommande)
        surCommande(cmd, false);
    return 0;
}

size_t LD2410S::attendre(uint16_t cmd, uint8_t *rep, size_t max, uint32_t attente_ms)
{
    static const uint8_t TETE[4] = {0xFD, 0xFC, 0xFB, 0xFA}, FIN[4] = {0x04, 0x03, 0x02, 0x01};
    size_t r = 0;
    const unsigned long debut = millis();
    dbgReponse.cmd = cmd;
    dbgReponse.nb = 0;
    while (millis() - debut < attente_ms)
    {
        if (!_serial.available())
        {
            yield();
            continue;
        }
        const uint8_t b = _serial.read();
        if (dbgReponse.nb < sizeof(dbgReponse.recu)) // MESURE : tout ce qui arrive, reconnu ou non
            dbgReponse.recu[dbgReponse.nb++] = b;
        if (r < 4)
        {
            if (b == TETE[r])
                rep[r++] = b;
            else
                r = b == TETE[0] ? (rep[0] = b, 1) : 0;
            continue;
        }
        rep[r++] = b;
        if (r < 8)
            continue;
        const size_t total = 6 + (size_t)(rep[4] | rep[5] << 8) + 4;
        if (total > max || total < 12)
        {
            r = 0;
            continue;
        }
        if (r < total)
            continue;
        if (rep[6] == (cmd & 0xFF) && rep[7] == ((cmd >> 8) | 0x01) && memcmp(rep + total - 4, FIN, 4) == 0)
            return total;
        r = 0; // réponse à autre chose : on continue d'attendre la nôtre
    }
    return 0;
}

/**
 * @brief Sends the command to enter configuration mode on the LD2410S device.
 *
 * This function transmits a specific command frame to the LD2410S device via the serial interface
 * to request entry into configuration mode. It waits for up to 500 milliseconds for an 18-byte
 * acknowledgment frame from the device. The function checks specific bytes in the response to
 * verify successful entry into configuration mode.
 *
 * @return true if the device successfully enters configuration mode and the expected acknowledgment is received; false otherwise.
 */
bool LD2410S::enterConfigMode()
{
    // 28/10/2026 : voir purgeAvantCommande (HLK-LD2410S.h) ; dbgEntree garde ce qui est arrivé (mesure)
    dbgEntree.avant = (uint16_t)_serial.available();
    dbgEntree.nb = 0;
    if (purgeAvantCommande)
        while (_serial.available())
            _serial.read();
    // 30/10/2026 : la trame d'un seul write (voir commande())
    const uint8_t trame[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
    _serial.write(trame, sizeof(trame));
    _serial.flush();

    static const uint8_t HEAD[4] = {0xFD, 0xFC, 0xFB, 0xFA};
    uint8_t ack[18];
    size_t n = 0;
    unsigned long start = millis();
    while (millis() - start < 500)
    {
        if (!_serial.available())
            continue;
        const uint8_t b = _serial.read();
        if (dbgEntree.nb < sizeof dbgEntree.recu)
            dbgEntree.recu[dbgEntree.nb++] = b;
        if (purgeAvantCommande && n < 4) // en-tête cherché octet par octet
        {
            if (b == HEAD[n])
                ack[n++] = b;
            else
                n = b == HEAD[0] ? (ack[0] = b, 1) : 0;
            continue;
        }
        ack[n++] = b; // ancienne lecture : blocs de 18 pris tels quels
        if (n < sizeof ack)
            continue;
        n = 0;
        if (ack[0] == 0xFD && ack[1] == 0xFC && ack[2] == 0xFB && ack[3] == 0xFA &&
            ack[6] == 0xFF && ack[7] == 0x01 && ack[10] == 0x03 && ack[11] == 0x00)
        {
            dbgEntree.ms = (uint16_t)(millis() - start);
            LD2410S_LOG_LINE("Enter config mode Ok");
            if (surCommande)
                surCommande(0x00FF, true);
            return true;
        }
    }
    dbgEntree.ms = (uint16_t)(millis() - start);
    if (surCommande)
        surCommande(0x00FF, false);
    // 01/11/2026 : l'accusé peut manquer alors que le capteur est BIEN entré en configuration (mesuré le 28/10 : route
    // /radar/essai) ; les appelants abandonnent sans refermer : on referme ici, pour tous
    exitConfigMode();
    return false;
}


/**
 * @brief Exits the configuration mode of the LD2410S device.
 *
 * Sends the appropriate command to the device to exit configuration mode and waits for an acknowledgment.
 * The function waits up to 500 milliseconds for a response from the device. If a valid acknowledgment
 * frame is received, it logs a success message and returns true. Otherwise, it returns false.
 *
 * @return true if the device successfully exits configuration mode, false otherwise.
 */
bool LD2410S::exitConfigMode()
{
    uint8_t ack[32];
    return commande(0x00FE, nullptr, 0, ack, sizeof(ack), 500) >= 8;
}


/**
 * @brief Sends a command to write generic parameters to the LD2410S device.
 *
 * This function constructs and sends a command frame to the LD2410S sensor to set various generic parameters,
 * including farthest and nearest detection distances, delay time, status report frequency, distance report frequency,
 * and response speed. The function waits for an acknowledgment from the device and verifies the response.
 *
 * @return true if the parameters were successfully written and acknowledged by the device; false otherwise.
 */
bool LD2410S::writeGenericParametersCommand()
{
    const uint8_t farthest_distance[6] = {0x05, 0x00, 0x01, 0x00, 0x00, 0x00};    // 1m
    const uint8_t nearest_distance[6] = {0x0A, 0x00, 0x00, 0x00, 0x00, 0x00};     // 0.0m
    const uint8_t delay_time[6] = {0x06, 0x00, 0x0A, 0x00, 0x00, 0x00};           // 10s
    const uint8_t freq_status_report[6] = {0x02, 0x00, 0x28, 0x00, 0x00, 0x00};   // 4.0 Hz
    const uint8_t freq_distance_report[6] = {0x0C, 0x00, 0x28, 0x00, 0x00, 0x00}; // 4.0Hz

    const uint8_t response_speed[6] = {0x0B, 0x00, 0x0A, 0x00, 0x00, 0x00};       // response normal

    sendFrameHead();

    const uint8_t cmd[] = {0x26, 0x00, 0x70, 0x00};
    _serial.write(cmd, sizeof(cmd));
    _serial.write(farthest_distance, sizeof(farthest_distance));
    _serial.write(nearest_distance, sizeof(nearest_distance));
    _serial.write(delay_time, sizeof(delay_time));
    _serial.write(freq_status_report, sizeof(freq_status_report));
    _serial.write(freq_distance_report, sizeof(freq_distance_report));
    _serial.write(response_speed, sizeof(response_speed));

    sendFrameEnd();
    _serial.flush();

    unsigned long start = millis();

    while (millis() - start < 1000)
    {
        if (_serial.available() >= 14)
        {
            uint8_t ack[14];
            _serial.readBytes(ack, sizeof(ack));
            LD2410S_LOG_LINE("Generic parameters data: ");
            for (int i = 0; i < 14; i++)
            {
                LD2410S_LOG("%02X ", ack[i]);
            }
            LD2410S_LOG_LINE("");
            if (ack[0] == 0xFD && ack[1] == 0xFC && ack[2] == 0xFB && ack[3] == 0xFA &&
                ack[6] == 0x70 && ack[7] == 0x01)
            {
                LD2410S_LOG_LINE("Write generic parameters Ok");
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Configure les paramètres généraux du capteur LD2410S.
 * * Mot de commande : 0x7000
 * Retour : 0x0100 (Succès : 0, Échec : 1)
 * * @param maxDistCm        Distance max de détection [Plage : 1 à 16] (Unité : Porte/Gate, ~15-20cm par gate selon firmware)
 * @param minDistCm        Distance min de détection [Plage : 0 à 16] (Unité : Porte/Gate)
 * @param delaySec         Temps de maintien sans présence [Plage : 10 à 120] (Unité : Secondes)
 * @param statusFreqHz     Fréquence rapport d'état (Trigger) [Plage : 0.5 à 8.0] (Pas : 0.5) (Unité : Hz)
 * @param distanceFreqHz   Fréquence rapport de distance (Maintain) [Plage : 0.5 à 8.0] (Pas : 0.5) (Unité : Hz)
 * @param speedMode        Vitesse de réponse [Valeurs : 5 (Normal) ou 10 (Rapide)]
 * @return true si la configuration est acceptée par le capteur (ACK reçu), false sinon.
 */
bool LD2410S::setCustomParameters(uint16_t maxDistCm, uint16_t minDistCm, uint16_t delaySec, float statusFreqHz, float distanceFreqHz, uint8_t speedMode)
{
    // fréquences : le capteur attend les Hz ×10 (0,5 Hz -> 5) ; mots de paramètre de la doc (Table 5-2)
    const uint32_t valeurs[6] = {maxDistCm, minDistCm, delaySec, (uint32_t)(statusFreqHz * 10.0f), (uint32_t)(distanceFreqHz * 10.0f), speedMode};
    const uint16_t mots[6] = {0x05, 0x0A, 0x06, 0x02, 0x0C, 0x0B};
    uint8_t d[36];
    for (int i = 0; i < 6; i++)
    {
        d[6 * i] = mots[i] & 0xFF;
        d[6 * i + 1] = mots[i] >> 8;
        for (int b = 0; b < 4; b++)
            d[6 * i + 2 + b] = (uint8_t)(valeurs[i] >> (8 * b));
    }
    uint8_t ack[32];
    const size_t n = commande(0x0070, d, sizeof(d), ack, sizeof(ack), 500);
    return n >= 10 && ack[8] == 0x00 && ack[9] == 0x00;
}

bool LD2410S::writeThresholdParameters(const uint8_t triggerThresholds[8], const uint8_t holdingThresholds[8])
{
    // 30/10/2026 : 16 blocs (mot de paramètre 2 octets + valeur 4 octets), paramètres 0-7 puis 8-15 (doc Table 5-3)
    uint8_t d[96];
    for (uint16_t i = 0; i < 16; i++)
    {
        uint8_t *b = d + 6 * i;
        b[0] = i & 0xFF;
        b[1] = i >> 8;
        b[2] = i < 8 ? triggerThresholds[i] : holdingThresholds[i - 8];
        b[3] = b[4] = b[5] = 0;
    }
    uint8_t ack[32];
    const size_t n = commande(0x0072, d, sizeof(d), ack, sizeof(ack), 500);
    if (surEchange)
        surEchange("ack 72", ack, n);
    return n >= 10 && ack[8] == 0x00 && ack[9] == 0x00;
}

bool LD2410S::readThresholdParameters(uint8_t triggerThresholdsOut[8], uint8_t holdingThresholdsOut[8])
{
    uint8_t d[32];
    for (uint16_t i = 0; i < 16; i++)
    {
        d[2 * i] = i & 0xFF;
        d[2 * i + 1] = i >> 8;
    }
    uint8_t rep[96];
    const size_t n = commande(0x0073, d, sizeof(d), rep, sizeof(rep), 600);
    if (surEchange)
        surEchange("lu 73", rep, n);
    if (n != 78 || rep[8] != 0x00 || rep[9] != 0x00)
        return false;
    for (int i = 0; i < 8; i++) // premier octet de chaque valeur 32 bits (seuils 0-95 dB)
    {
        triggerThresholdsOut[i] = rep[10 + 4 * i];
        holdingThresholdsOut[i] = rep[10 + 32 + 4 * i];
    }
    return true;
}

/*bool LD2410S::resetToFactoryDefaults()
{
    // 1. Nettoyage initial du buffer
    while(_serial.available()) _serial.read();

    sendFrameHead();

    // Longueur intra-trame : 2 octets (0x02, 0x00)
    // Mot de commande d'usine : 0x00A2 (Little-Endian -> A2 00)
    uint8_t cmdHeader[4] = {0x02, 0x00, 0xA2, 0x00};
    _serial.write(cmdHeader, sizeof(cmdHeader));

    sendFrameEnd();
    _serial.flush();

    // Attente de l'ACK officiel (14 octets d'après la trame de réponse)
    constexpr size_t ACK_SIZE = 14;
    uint8_t ack[ACK_SIZE];
    size_t bytesRead = 0;
    
    unsigned long start = millis();
    while (millis() - start < 800 && bytesRead < ACK_SIZE)
    {
        if (_serial.available() > 0)
        {
            ack[bytesRead++] = _serial.read();
        }
        yield();
    }

    // Validation STRICTE de l'ACK officiel :
    // ack[6] et ack[7] doivent être : 0xA2 0x01
    // ack[8] et ack[9] doivent être : 0x00 0x00 (Statut de succès)
    if (bytesRead >= 10 &&
        ack[0] == 0xFD && ack[1] == 0xFC && ack[2] == 0xFB && ack[3] == 0xFA &&
        ack[6] == 0xA2 && ack[7] == 0x01 && ack[8] == 0x00 && ack[9] == 0x00)
    {
        return true;
    }
    
    return false;
}*/

bool LD2410S::writeSNRParameters(const uint8_t triggerSNR[8], const uint8_t holdSNR[8])
{
    // 30/10/2026 : 16 blocs (mot de paramètre 2 octets + valeur 4 octets), paramètres 0-7 puis 8-15 (doc Table 5-4)
    uint8_t d[96];
    for (uint16_t i = 0; i < 16; i++)
    {
        uint8_t *b = d + 6 * i;
        b[0] = i & 0xFF;
        b[1] = i >> 8;
        b[2] = i < 8 ? triggerSNR[i] : holdSNR[i - 8];
        b[3] = b[4] = b[5] = 0;
    }
    uint8_t ack[32];
    const size_t n = commande(0x0074, d, sizeof(d), ack, sizeof(ack), 500);
    if (surEchange)
        surEchange("ack 74", ack, n);
    return n >= 10 && ack[8] == 0x00 && ack[9] == 0x00;
}

bool LD2410S::readSNRParameters(uint8_t triggerSNROut[8], uint8_t holdSNROut[8])
{
    uint8_t d[32];
    for (uint16_t i = 0; i < 16; i++)
    {
        d[2 * i] = i & 0xFF;
        d[2 * i + 1] = i >> 8;
    }
    uint8_t rep[96];
    const size_t n = commande(0x0075, d, sizeof(d), rep, sizeof(rep), 600);
    if (surEchange)
        surEchange("lu snr", rep, n);
    if (n != 78 || rep[8] != 0x00 || rep[9] != 0x00)
        return false;
    for (int i = 0; i < 8; i++) // premier octet de chaque valeur 32 bits (seuils 0-95 dB)
    {
        triggerSNROut[i] = rep[10 + 4 * i];
        holdSNROut[i] = rep[10 + 32 + 4 * i];
    }
    return true;
}


/**
 * @brief Parses a 32-bit unsigned integer from a little-endian byte array.
 *
 * This function takes a pointer to a 4-byte array and interprets the bytes as a
 * 32-bit unsigned integer in little-endian order (least significant byte first).
 *
 * @param data Pointer to an array of at least 4 bytes containing the little-endian data.
 * @return The parsed 32-bit unsigned integer.
 */
static uint32_t parseLEUint32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

bool LD2410S::readParams16(uint16_t cmd, uint32_t out[16])
{
    uint8_t d[32];
    for (uint16_t i = 0; i < 16; i++)
    {
        d[2 * i] = i & 0xFF;
        d[2 * i + 1] = i >> 8;
    }
    uint8_t rep[96];
    const size_t n = commande(cmd, d, sizeof(d), rep, sizeof(rep), 600);
    if (surEchange)
    {
        char quoi[16];
        snprintf(quoi, sizeof(quoi), "lu %02X", (unsigned)(cmd & 0xFF));
        surEchange(quoi, rep, n);
    }
    if (n != 78 || rep[8] != 0x00 || rep[9] != 0x00)
        return false;
    for (int i = 0; i < 16; i++)
        out[i] = parseLEUint32(&rep[10 + 4 * i]);
    return true;
}

static bool lire16(LD2410S &ld, uint16_t cmd, uint8_t out[16])
{
    uint32_t v[16];
    if (!ld.readParams16(cmd, v))
        return false;
    for (int i = 0; i < 16; i++)
        out[i] = (uint8_t)(v[i] > 255 ? 255 : v[i]);
    return true;
}

bool LD2410S::readTriggers16(uint8_t out[16]) { return lire16(*this, 0x0073, out); }
bool LD2410S::readHolds16(uint8_t out[16]) { return lire16(*this, 0x0077, out); }

// 16 blocs : mot de paramètre (porte 0 à 15, 2 octets) + valeur (4 octets)
static size_t blocs16(const uint8_t v[16], uint8_t d[96])
{
    for (uint16_t i = 0; i < 16; i++)
    {
        uint8_t *b = d + 6 * i;
        b[0] = i & 0xFF;
        b[1] = i >> 8;
        b[2] = v[i];
        b[3] = b[4] = b[5] = 0;
    }
    return 96;
}

bool LD2410S::writeTriggers16(const uint8_t v[16])
{
    uint8_t d[96], ack[32];
    const size_t n = commande(0x0072, d, blocs16(v, d), ack, sizeof(ack), 500);
    if (surEchange)
        surEchange("ack 72", ack, n);
    return n >= 10 && ack[8] == 0x00 && ack[9] == 0x00;
}

bool LD2410S::writeHolds16(const uint8_t v[16])
{
    uint8_t d[96], ack[32];
    const size_t n = commande(0x0076, d, blocs16(v, d), ack, sizeof(ack), 500);
    if (surEchange)
        surEchange("ack 76", ack, n);
    return n >= 10 && ack[8] == 0x00 && ack[9] == 0x00;
}


/**
 * @brief Reads the common parameters from the LD2410S sensor.
 *
 * This function sends a command to the LD2410S sensor to request its common parameters,
 * waits for a response, and parses the returned data. The parameters retrieved include
 * the farthest and nearest detection distances, delay time, frequency status, frequency
 * distance, and response speed. The function returns true if the parameters are successfully
 * read and parsed, otherwise returns false.
 *
 * @param[out] farthest_distance The farthest detection distance reported by the sensor.
 * @param[out] nearest_distance The nearest detection distance reported by the sensor.
 * @param[out] delay_time The delay time parameter from the sensor.
 * @param[out] freq_status The frequency status parameter from the sensor.
 * @param[out] freq_distance The frequency distance parameter from the sensor.
 * @param[out] respond_speed The response speed parameter from the sensor.
 * @return true if the parameters were successfully read and parsed, false otherwise.
 */
bool LD2410S::readCommonParametersCommand(uint32_t &farthest_distance, uint32_t &nearest_distance, uint32_t &delay_time, uint32_t &freq_status, uint32_t &freq_distance, uint32_t &respond_speed)
{
    const uint8_t mots[] = {0x05, 0x00, 0x0A, 0x00, 0x06, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x0B, 0x00};
    uint8_t ack[64];
    if (commande(0x0071, mots, sizeof(mots), ack, sizeof(ack), 1000) != 38 || ack[8] != 0x00 || ack[9] != 0x00)
        return false;
    farthest_distance = parseLEUint32(&ack[10]);
    nearest_distance = parseLEUint32(&ack[14]);
    delay_time = parseLEUint32(&ack[18]);
    freq_status = parseLEUint32(&ack[22]);
    freq_distance = parseLEUint32(&ack[26]);
    respond_speed = parseLEUint32(&ack[30]);
    return true;
}


/**
 * @brief Reads the firmware version from the LD2410S device.
 *
 * This function sends a command to the LD2410S device to request its firmware version.
 * It waits for a response and parses the major, minor, and patch version numbers from the received data.
 *
 * @param[out] major Reference to a uint16_t variable where the major version will be stored.
 * @param[out] minor Reference to a uint16_t variable where the minor version will be stored.
 * @param[out] patch Reference to a uint16_t variable where the patch version will be stored.
 * @return true if the firmware version was successfully read and parsed, false otherwise.
 */
bool LD2410S::readFirmwareVersion(uint16_t &major, uint16_t &minor, uint16_t &patch)
{
    uint8_t ack[64];
    if (commande(0x0000, nullptr, 0, ack, sizeof(ack), 1000) != 24)
        return false;
    major = (static_cast<uint16_t>(ack[15]) << 8) | ack[14];
    minor = (static_cast<uint16_t>(ack[17]) << 8) | ack[16];
    patch = (static_cast<uint16_t>(ack[19]) << 8) | ack[18];
    return true;
}


/**
 * @brief Reads the serial number from the LD2410S device via the serial interface.
 *
 * This function sends a command to the LD2410S device to request its serial number,
 * waits for the response, and extracts the serial number from the received data.
 * The serial number is expected to be 8 bytes long and will be stored as a null-terminated
 * string in the provided buffer.
 *
 * The function first clears the serial buffer, sends the serial number request command,
 * and waits up to 1 second for a valid response. If a valid response is received,
 * the serial number is extracted and copied into the buffer.
 *
 * @param[out] buffer Pointer to a character array where the serial number will be stored.
 *                    The buffer must be at least 9 bytes long to accommodate the 8-byte
 *                    serial number and the null terminator.
 * @param[in] len     The length of the provided buffer.
 * @return true if the serial number was successfully read and stored in the buffer,
 *         false otherwise (e.g., if the buffer is too small or a valid response was not received).
 */
bool LD2410S::readSerialNumber(char *buffer, size_t len)
{
    if (len < 9)
        return false;
    uint8_t ack[64];
    if (commande(0x0011, nullptr, 0, ack, sizeof(ack), 1000) != 24 || ack[8] != 0x00 || ack[9] != 0x00)
        return false;
    for (uint8_t i = 0; i < 8; i++)
        buffer[i] = static_cast<char>(ack[12 + i]);
    buffer[8] = '\0';
    return true;
}
