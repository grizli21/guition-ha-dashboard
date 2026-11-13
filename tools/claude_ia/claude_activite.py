"""Activité de Claude Code en direct, pour la page « IA » des écrans Guition (11/11/2026).

Deux sources, toutes deux LOCALES (aucun appel réseau) :
1. Les HOOKS de Claude Code (~/.claude/settings.json) : à chaque événement, `claude-hook-activite` ajoute une ligne JSON
   à EVENEMENTS (heure, événement, session, journal, type de notification, outil). Même principe que Clawdmeter
   (github.com/HermannBjorgvin/Clawdmeter, étudié le 11/11) : UserPromptSubmit, PostToolUse, Stop, Notification,
   PreToolUse(AskUserQuestion). On en tire l'état (réfléchit / attend une réponse / au repos), les prompts, les outils et
   les sessions actives.
2. Les JOURNAUX de conversation (<config>/projects/**/*.jsonl) : somme des `output_tokens` des réponses (ce que
   Clawdmeter ne fait pas). Une même réponse est écrite sur plusieurs lignes (une par bloc de contenu, MÊME usage
   répété) -> dédoublonnage par `message.id`. Les journaux peuvent faire 280 Mo : au premier passage on cherche par
   dichotomie la première ligne de la fenêtre, puis on ne lit que les nouveaux octets.

Fenêtre : celle de la jauge 5 h (réinitialisation - 5 h) si on la connaît, sinon les 5 dernières heures.
Histogramme : 30 tranches de 10 min glissantes jusqu'à maintenant (tokens produits), le plus ancien d'abord.

Installé et utilisé par publier_ia.py (voir README.md dans ce dossier) ; le format publié est décrit dans
CONTRAT_MQTT.md, section « Page IA ».
"""
import glob
import json
import os
import time
from collections import deque
from datetime import datetime

EVENEMENTS = os.path.expanduser("~/.local/state/claude-activite/evenements.log")
FENETRE = 5 * 3600
TRANCHE, NB_TRANCHES = 600, 30
SESSION_ACTIVE = 300          # s : session « active » si un événement depuis 5 min (comme Clawdmeter)
SILENCE_MAX = 600             # s : un prompt sans Stop n'est plus « en cours » après 10 min sans événement
OUTIL_RECENT = 60             # s : un outil terminé depuis moins d'1 min après le dernier Stop = encore au travail
ALERTE_MAX = 3600             # s : une alerte jamais levée (session fermée sans répondre) s'éteint au bout d'1 h
ROTATION = 4 * 1024 * 1024    # octets : evenements.log -> .1 au-delà


def _ts(iso):
    try:
        return datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp()
    except (AttributeError, ValueError):
        return None


class Evenements:
    """Lit evenements.log au fil de l'eau (offset), garde les 5 dernières heures."""

    def __init__(self, chemin=EVENEMENTS):
        self.chemin = chemin
        self.offset = None
        self.ev = deque()

    def lire(self, maintenant):
        """Lit les nouvelles lignes ; renvoie la liste des événements nouveaux (pour réagir aux Stop)."""
        try:
            taille = os.path.getsize(self.chemin)
        except OSError:
            return []
        nouveaux = []
        if self.offset is None or taille < self.offset:   # premier passage ou fichier recréé
            self.offset = max(0, taille - 1024 * 1024)      # 1 Mo ~ 7000 événements : bien plus que 5 h
            premier = self.offset > 0
        else:
            premier = False
        if taille > self.offset:
            with open(self.chemin, "rb") as f:
                f.seek(self.offset)
                bloc = f.read(taille - self.offset)
            fin = bloc.rfind(b"\n") + 1                       # une ligne en cours d'écriture attend le tour suivant
            lignes = bloc[:fin].split(b"\n")
            if premier and lignes:
                lignes = lignes[1:]                           # ligne coupée par le saut
            self.offset += fin
            for l in lignes:
                try:
                    e = json.loads(l)
                    e["t"] = float(e["t"])
                except (ValueError, KeyError, TypeError):
                    continue
                self.ev.append(e)
                nouveaux.append(e)
        while self.ev and self.ev[0]["t"] < maintenant - FENETRE - TRANCHE:
            self.ev.popleft()
        if self.offset >= ROTATION and self.offset == taille:  # tout est lu : on fait tourner
            try:
                os.replace(self.chemin, self.chemin + ".1")
                self.offset = 0
            except OSError:
                pass
        return nouveaux

    def etat(self, maintenant, debut):
        prompts = outils = 0
        sessions_recentes = set()
        par_session = {}
        for e in self.ev:
            t, nom, s = e["t"], e.get("e"), e.get("s") or "?"
            if t >= debut:
                if nom == "UserPromptSubmit":
                    prompts += 1
                elif nom == "PostToolUse":
                    outils += 1
            if t >= maintenant - SESSION_ACTIVE:
                sessions_recentes.add(s)
            p = par_session.setdefault(s, {"prompt": 0, "stop": 0, "outil": 0, "dernier": 0, "alerte": None})
            p["dernier"] = t
            if nom == "UserPromptSubmit":
                p["prompt"], p["alerte"] = t, None
            elif nom == "PostToolUse":
                p["outil"], p["alerte"] = t, None
            elif nom == "Stop":
                p["stop"], p["alerte"] = t, None
            elif nom == "PreToolUse" and e.get("o") == "AskUserQuestion":
                p["alerte"] = ("question", t)
            elif nom == "Notification":
                n = e.get("n")
                if n in (None, "permission_prompt", "elicitation_dialog"):
                    p["alerte"] = ("autorisation", t)
                # idle_prompt (« j'attends depuis 60 s ») : pas une alerte (Clawdmeter l'éteint aussi par défaut)
        alerte, travaille = "", False
        for p in par_session.values():
            if p["alerte"] and maintenant - p["alerte"][1] < ALERTE_MAX:
                if alerte != "question":                  # une question l'emporte sur une autorisation
                    alerte = p["alerte"][0]
            elif (p["prompt"] > p["stop"] and maintenant - p["dernier"] < SILENCE_MAX) or \
                    (p["outil"] > p["stop"] and maintenant - p["outil"] < OUTIL_RECENT):
                travaille = True
        etat = "attente" if alerte else "reflechit" if travaille else "repos"
        return {"etat": etat, "alerte": alerte, "outils": outils, "prompts": prompts,
                "sessions": len(sessions_recentes)}


class Tokens:
    """Tokens produits (output_tokens) d'un dossier de config Claude Code, sur la fenêtre."""

    def __init__(self, dossier):
        self.racine = os.path.join(os.path.expanduser(dossier), "projects")
        self.offsets = {}          # journal -> octets déjà lus
        self.reponses = {}         # message.id -> (heure, output_tokens) ; la dernière ligne lue gagne

    @staticmethod
    def _debut_fenetre(f, taille, debut):
        """Premier offset dont la ligne horodatée est >= debut (dichotomie : les journaux sont chronologiques)."""
        bas, haut = 0, taille
        while haut - bas > 65536:
            milieu = (bas + haut) // 2
            f.seek(milieu)
            f.readline()                                   # fin de la ligne coupée
            t = None
            for _ in range(200):                           # quelques lignes n'ont pas d'horodatage
                l = f.readline()
                if not l:
                    break
                i = l.find(b'"timestamp":"')
                if i >= 0:
                    t = _ts(l[i + 13:l.find(b'"', i + 13)].decode("ascii", "replace"))
                    if t is not None:
                        break
            if t is None or t >= debut:
                haut = milieu
            else:
                bas = milieu
        return bas

    def lire(self, maintenant, debut):
        for chemin in glob.glob(os.path.join(self.racine, "**", "*.jsonl"), recursive=True):
            try:
                st = os.stat(chemin)
            except OSError:
                continue
            if st.st_mtime < debut - TRANCHE and chemin not in self.offsets:
                continue
            off = self.offsets.get(chemin)
            if off is not None and off > st.st_size:       # journal réécrit
                off = None
            try:
                with open(chemin, "rb") as f:
                    if off is None:
                        off = self._debut_fenetre(f, st.st_size, debut - TRANCHE)
                        f.seek(off)
                        if off:
                            off += len(f.readline())
                    if st.st_size > off:
                        f.seek(off)
                        bloc = f.read(st.st_size - off)
                        fin = bloc.rfind(b"\n") + 1
                        self._analyser(bloc[:fin])
                        off += fin
            except OSError:
                continue
            self.offsets[chemin] = off
        limite = debut - TRANCHE
        for k in [k for k, (t, _) in self.reponses.items() if t < limite]:
            del self.reponses[k]

    def _analyser(self, bloc):
        for l in bloc.split(b"\n"):
            if b'"output_tokens"' not in l or b'"assistant"' not in l:
                continue
            try:
                o = json.loads(l)
            except ValueError:
                continue
            m = o.get("message")
            if o.get("type") != "assistant" or not isinstance(m, dict):
                continue
            u, t = m.get("usage") or {}, _ts(o.get("timestamp"))
            if t is None or not m.get("id"):
                continue
            self.reponses[m["id"]] = (t, int(u.get("output_tokens") or 0))

    def resume(self, maintenant, debut):
        total, histo = 0, [0] * NB_TRANCHES
        origine = maintenant - NB_TRANCHES * TRANCHE
        for t, n in self.reponses.values():
            if t >= debut:
                total += n
            if t >= origine:
                histo[min(NB_TRANCHES - 1, int((t - origine) // TRANCHE))] += n
        return total, histo


class Activite:
    """Assemble le message dashboard_guition/pc/ia_activite pour un dossier de config donné."""

    def __init__(self, dossier="~/.claude"):
        self.evenements = Evenements()
        self.dossier = os.path.expanduser(dossier)
        self.tokens = Tokens(dossier)
        self.t_tokens = 0
        self.dernier = None

    def changer_dossier(self, dossier):
        dossier = os.path.expanduser(dossier)
        if dossier != self.dossier:
            self.dossier, self.tokens, self.t_tokens = dossier, Tokens(dossier), 0

    def tour(self, maintenant, reset_5h=0):
        """Toutes les secondes. Renvoie (message ou None s'il n'a pas changé, événements nouveaux)."""
        debut = reset_5h - FENETRE if reset_5h and reset_5h > maintenant else maintenant - FENETRE
        nouveaux = self.evenements.lire(maintenant)
        nouveaux = [e for e in nouveaux if self._du_dossier(e)]
        if nouveaux or maintenant - self.t_tokens >= 15:   # journaux relus à chaque événement, sinon toutes les 15 s
            self.tokens.lire(maintenant, min(debut, maintenant - FENETRE))   # l'histogramme couvre 5 h glissantes
            self.t_tokens = maintenant
        e = self.evenements.etat(maintenant, debut)
        total, histo = self.tokens.resume(maintenant, debut)
        msg = dict(e, tokens=total, histo=histo)
        if msg == self.dernier and not nouveaux:
            return None, nouveaux
        self.dernier = msg
        return dict(msg, maj=int(maintenant)), nouveaux

    def _du_dossier(self, e):
        tp = e.get("tp") or ""
        return not tp or tp.startswith(self.dossier + os.sep)


if __name__ == "__main__":                              # essai à la main : python3 claude_activite.py
    a = Activite()
    m, _ = a.tour(time.time())
    print(json.dumps(m, ensure_ascii=False))
