#!/usr/bin/env python3
"""Publie vers les écrans Guition l'usage et l'activité de Claude Code (page « IA » du rail).

Autonome : bibliothèque standard seulement. Lit les comptes Claude Code LOCAUX (dossiers de config
donnés dans le fichier de config — aucune adresse e-mail n'est codée ici), l'usage 5 h / semaine
(« /usage » de Claude Code, même source que lui) et l'activité en direct (claude_activite.py, hooks +
journaux locaux), puis publie une fois par seconde, quand elles ont changé :

  <prefixe>/pc/ia           retenu  {"comptes":[{nom,h5,h5_reset,semaine,semaine_reset,maj}]}  (UN compte : celui en cours)
  <prefixe>/pc/ia_activite  retenu  {etat,alerte,tokens,outils,prompts,sessions,histo[30],maj}

Formats exacts : CONTRAT_MQTT.md, section « Page IA ». Garde-fous repris de l'agent de production :
au plus une lecture d'usage par compte toutes les 5 min (2 min si relancé par un hook Stop, jamais à
moins de 2 min d'écart), un HTTP 429 met le compte de côté pour son Retry-After (10 min sinon).
LE JETON ne s'affiche jamais, ne s'écrit nulle part : il ne part qu'en en-tête d'Authorization,
vers l'adresse d'usage (https://api.anthropic.com/api/oauth/usage).

Usage :
  python3 publier_ia.py [--config FICHIER] [--hz 1]      service en continu
  python3 publier_ia.py --essai                          imprime les deux messages, ne publie rien

Installation pas à pas (jq, hook, settings.json, service systemd) : README.md dans ce dossier.
"""
import argparse
import json
import os
import socket
import struct
import sys
import time
import urllib.error
import urllib.request

import claude_activite

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
USAGE_INTERVALLE = 300      # s : au plus une lecture d'usage par compte toutes les 5 min (limité en débit)
USAGE_ECART_MIN = 120       # s : même relancé par un hook Stop, jamais deux lectures à moins de 2 min
USAGE_APRES_STOP = 10       # s : relecture ~10 s après chaque fin de réponse (hook Stop)


def _chaine(s):
    b = s.encode()
    return struct.pack(">H", len(b)) + b


class MqttPublieur:
    """Le strict minimum de MQTT 3.1.1 pour publier en QoS 0 : CONNECT + PUBLISH (+ PINGREQ)."""

    def __init__(self, conf):
        self.conf = conf
        self.sock = None
        self.prochain_essai = 0.0
        self.dernier_ping = 0.0

    def _connecter(self):
        c = self.conf
        s = socket.create_connection((c.get("hote", "127.0.0.1"), c.get("port", 1883)), timeout=3)
        drapeaux = 0xC2                                  # session neuve, nom d'utilisateur + mot de passe
        corps = (_chaine("MQTT") + bytes([4, drapeaux]) + struct.pack(">H", 60)
                 + _chaine("claude_ia") + _chaine(c.get("utilisateur", "")) + _chaine(c.get("mot_de_passe", "")))
        s.sendall(bytes([0x10]) + _mqtt_longueur(len(corps)) + corps)
        rep = s.recv(4)
        if len(rep) < 4 or rep[0] != 0x20 or rep[3] != 0:
            s.close()
            raise OSError(f"CONNACK refusé ({rep.hex()})")
        self.sock = s
        self.dernier_ping = time.monotonic()
        print("\nMQTT : connecté", flush=True)

    def publier(self, sujet, message, retenu=False):
        now = time.monotonic()
        if self.sock is None:
            if now < self.prochain_essai:
                return False
            try:
                self._connecter()
            except OSError as e:
                self.prochain_essai = now + 10           # broker injoignable : on réessaie dans 10 s
                print("\nMQTT : connexion impossible :", e, flush=True)
                return False
        elif now - self.dernier_ping > 30:               # keepalive 60 s : un PINGREQ si rien n'a été envoyé
            try:
                self.sock.sendall(bytes([0xC0, 0x00]))
                self.dernier_ping = now
            except OSError:
                pass
        corps = _chaine(sujet) + message.encode()
        try:
            self.sock.sendall(bytes([0x31 if retenu else 0x30]) + _mqtt_longueur(len(corps)) + corps)
            self.dernier_ping = now
            return True
        except OSError as e:
            print("\nMQTT : envoi perdu :", e, flush=True)
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
            self.prochain_essai = now + 5
            return False


def _mqtt_longueur(n):
    out = b""
    while True:
        b = n % 128
        n //= 128
        out += bytes([b | (0x80 if n else 0)])
        if not n:
            return out


def _horodatage(s):
    from datetime import datetime
    try:
        return int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp())
    except (ValueError, AttributeError):
        return 0


class Compte:
    """Un compte Claude Code : dossier de config (CLAUDE_CONFIG_DIR), jeton, usage."""

    def __init__(self, dossier, nom=None):
        self.dossier = os.path.expanduser(dossier)
        self.nom_ecrit = nom
        self.usage = None
        self.t = -1e9
        self.bloque = 0.0
        self.force_a = None
        self.hausse = 0

    def nom(self):
        """Adresse du compte RÉELLEMENT connecté dans ce dossier (un /login peut changer de compte) ;
        sinon le nom écrit dans la config, sinon « compte » — jamais une adresse codée dans le programme."""
        for cfg in (os.path.join(self.dossier, ".claude.json"),
                    os.path.expanduser("~/.claude.json") if self.dossier == os.path.expanduser("~/.claude") else ""):
            try:
                with open(cfg) as f:
                    adresse = (json.load(f).get("oauthAccount") or {}).get("emailAddress")
                if adresse:
                    return adresse
            except (OSError, ValueError):
                pass
        return self.nom_ecrit or "compte"

    def _jeton(self):
        """Jeton d'accès du compte, s'il n'a pas expiré. JAMAIS affiché, JAMAIS écrit : il ne part
        qu'en en-tête d'Authorization vers USAGE_URL."""
        try:
            with open(os.path.join(self.dossier, ".credentials.json")) as f:
                o = json.load(f)["claudeAiOauth"]
        except (OSError, ValueError, KeyError):
            return None
        return o.get("accessToken") if o.get("expiresAt", 0) / 1000 > time.time() else None

    def lire_usage(self):
        """Met à jour self.usage ; True si de nouvelles valeurs sont arrivées. Garde-fous : intervalle,
        écart minimum, pause sur 429 (Retry-After, 10 min sinon)."""
        maintenant = time.monotonic()
        ecart = maintenant - self.t
        force = self.force_a is not None and maintenant >= self.force_a
        if maintenant < self.bloque or ecart < (USAGE_ECART_MIN if force else USAGE_INTERVALLE):
            return False
        self.t, self.force_a = maintenant, None
        jeton = self._jeton()
        if not jeton:
            return False
        try:
            req = urllib.request.Request(USAGE_URL, headers={"Authorization": "Bearer " + jeton,
                                                             "anthropic-beta": "oauth-2025-04-20"})
            with urllib.request.urlopen(req, timeout=5) as r:
                d = json.load(r)
        except urllib.error.HTTPError as e:
            if e.code == 429:
                try:
                    attente = int(e.headers.get("retry-after") or 600)
                except ValueError:
                    attente = 600
                self.bloque = maintenant + attente + 10
                print(f"\nClaude : {self.nom()} limité (429), pause {attente} s", flush=True)
            else:
                print(f"\nClaude : usage de {self.nom()} illisible : HTTP {e.code}", flush=True)
            return False
        except (urllib.error.URLError, ValueError, OSError) as e:
            print(f"\nClaude : usage de {self.nom()} illisible : {e}", flush=True)
            return False
        fh, sd = d.get("five_hour") or {}, d.get("seven_day") or {}
        h5 = round(float(fh.get("utilization") or 0))
        if self.usage and h5 > self.usage["h5"]:
            self.hausse = _hausses()                     # hausse la plus récente = compte « en cours »
        self.usage = {"nom": self.nom(),
                      "h5": h5, "h5_reset": _horodatage(fh.get("resets_at")),
                      "semaine": round(float(sd.get("utilization") or 0)), "semaine_reset": _horodatage(sd.get("resets_at")),
                      "maj": int(time.time())}
        return True


_HAUSSE = [0]


def _hausses():
    _HAUSSE[0] += 1
    return _HAUSSE[0]


def choisir_compte(comptes):
    """Le compte EN COURS = celui dont l'usage 5 h a monté le plus récemment ; avant toute hausse, le
    plus haut % (algorithme de Clawdmeter). Une baisse (réinitialisation) n'est pas une activité."""
    meilleur, cle = None, None
    for c in comptes:
        if not c.usage:
            continue
        k = (c.hausse, c.usage["h5"])
        if cle is None or k > cle:
            meilleur, cle = c, k
    return meilleur


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--config", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json"),
                    help="fichier de config JSON (voir config.exemple.json)")
    ap.add_argument("--hz", type=float, default=1.0, help="tour par seconde (1 par défaut)")
    ap.add_argument("--essai", action="store_true",
                    help="imprime les deux messages au lieu de publier (pas de MQTT ; l'usage est bien lu)")
    a = ap.parse_args()

    try:
        with open(a.config) as f:
            conf = json.load(f)
    except OSError as e:
        sys.exit(f"config : {e}  (copiez config.exemple.json en config.json)")
    comptes = [Compte(c["dossier"], c.get("nom")) for c in conf.get("comptes", []) if c.get("dossier")]
    if not comptes:
        sys.exit('config : ajoutez au moins un compte, ex. {"comptes":[{"dossier":"~/.claude"}]}')
    prefixe = conf.get("prefixe", "dashboard_guition")
    mqtt = None if a.essai else MqttPublieur(conf.get("mqtt") or {})
    activite = claude_activite.Activite(comptes[0].dossier)

    period = 1.0 / max(0.25, a.hz)
    ia, act, compte_affiche = None, None, None
    while True:
        t0 = time.monotonic()
        for c in comptes:
            if c.lire_usage():
                ia = True
        choisi = choisir_compte(comptes)
        if choisi is not compte_affiche:
            compte_affiche, ia = choisi, True
        if choisi:
            activite.changer_dossier(choisi.dossier)
        reset = choisi.usage["h5_reset"] if choisi and choisi.usage else 0
        msg_act, nouveaux = activite.tour(time.time(), reset)
        if choisi:
            for e in nouveaux:
                if e.get("e") == "Stop":
                    choisi.force_a = time.monotonic() + USAGE_APRES_STOP
                elif e.get("e") == "UserPromptSubmit" and choisi.force_a is None:
                    choisi.force_a = time.monotonic()
        if msg_act:
            act = msg_act

        if a.essai:
            print(json.dumps({"comptes": [compte_affiche.usage] if ia and compte_affiche else []},
                             ensure_ascii=False))
            print(json.dumps(act if act else {}, ensure_ascii=False))
            if not (ia and compte_affiche):
                print("(pas d'usage : jeton absent ou expiré — lance d'abord Claude Code avec ce compte)",
                      file=sys.stderr)
            return
        elif mqtt:
            if ia and compte_affiche:
                if mqtt.publier(f"{prefixe}/pc/ia", json.dumps({"comptes": [compte_affiche.usage]},
                                                               separators=(",", ":"), ensure_ascii=False), True):
                    ia = None
            if act and mqtt.publier(f"{prefixe}/pc/ia_activite",
                                    json.dumps(act, separators=(",", ":"), ensure_ascii=False), True):
                act = None
        d = period - (time.monotonic() - t0)
        if d > 0:
            time.sleep(d)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nArrêt.")
