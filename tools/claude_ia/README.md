# Outillage de la page « IA » — faire vivre `pc/ia` et `pc/ia_activite` depuis son PC

La page « IA » du rail ([CONTRAT_MQTT.md, section Page IA](../../CONTRAT_MQTT.md)) lit deux sujets
publiés **par le PC**, pas par Home Assistant. Sans eux la page reste vide. Ces outils les publient
depuis un PC qui fait tourner **Claude Code** — écrits pour ça, mais le format est ouvert.

## Ce que chaque sujet contient

| Sujet (retenu) | Contenu | D'où vient-il |
|---|---|---|
| `pc/ia` | `{"comptes":[{nom, h5, h5_reset, semaine, semaine_reset, maj}]}` — un seul compte, celui **en cours** : % utilisés sur les fenêtres 5 h et semaine, heure de réinitialisation de chaque fenêtre | l'adresse d'usage de Claude Code (`/api/oauth/usage`), la même source que sa commande `/usage` |
| `pc/ia_activite` | `{etat, alerte, tokens, outils, prompts, sessions, histo[30], maj}` — « réfléchit / attente / repos », l'alerte (autorisation, question), les tokens produits, 30 tranches de 10 min | les **hooks** Claude Code + les **journaux de conversation locaux** : aucun appel réseau |

## Installation, pas à pas

1. **jq** (le hook l'utilise) : `sudo apt install jq` (ou l'équivalent de votre distribution).
2. **Le hook** : copiez `claude-hook-activite` où vous voulez et rendez-le exécutable :
   `chmod +x claude-hook-activite`. Il écrit une ligne JSON par événement dans
   `~/.local/state/claude-activite/evenements.log`. Il n'affiche rien et sort toujours 0 :
   un échec ne gêne jamais Claude Code.
3. **Les settings** : fusionnez la clé `hooks` de `settings.hooks.exemple.json` dans
   `~/.claude/settings.json`, en remplaçant le chemin par le vôtre. Les cinq événements :
   `UserPromptSubmit`, `PostToolUse`, `Stop`, `Notification`, `PreToolUse` (matcher `AskUserQuestion`) ;
   `timeout: 5`. Sans `PreToolUse(AskUserQuestion)`, l'écran n'affiche jamais « question pour toi ».
4. **La config** : `cp config.exemple.json config.json` puis adaptez l'adresse du broker, le compte
   et le mot de passe MQTT (`chmod 600 config.json`). Un compte Claude Code = un **dossier de
   config** (`~/.claude` par défaut, ou votre `CLAUDE_CONFIG_DIR` quand vous en utilisez plusieurs) ;
   l'adresse e-mail affichée est lue dans le dossier, elle n'est jamais codée ici.
5. **Essai** (rien n'est publié) : `python3 publier_ia.py --essai` — imprime les deux messages tels
   qu'ils partiraient.
6. **En service** : `python3 publier_ia.py` au démarrage, ou le service **utilisateur** systemd :
   copiez `publier-ia.service.exemple` dans `~/.config/systemd/user/publier-ia.service`, adaptez les
   chemins, puis `systemctl --user daemon-reload && systemctl --user enable --now publier-ia`.
   Logs : `journalctl --user -u publier-ia`.

## Garde-fous (les mêmes que l'agent de production)

- L'usage n'est relu **que toutes les 5 min** par compte (2 min si un hook `Stop` vient de finir une
  réponse, jamais à moins de 2 min d'écart). Un HTTP 429 met le compte de côté pour son
  `Retry-After` (10 min sinon).
- **Le jeton reste sur le PC** : il n'est jamais affiché, jamais écrit, et ne part qu'en en-tête
  `Authorization`, vers l'adresse d'usage. S'il expire (Claude Code le renouvelle quand il tourne),
  la lecture s'arrête et l'écran garde les dernières valeurs avec leur heure (« lu il y a … »).

## ⚠️ À savoir

`/api/oauth/usage` **n'est pas une API publique documentée** : c'est ce que Claude Code appelle
lui-même pour `/usage`. Elle peut changer ou se fermer sans prévenir — le 31/10/2026, renouveler
soi-même le jeton passait déjà par platform.claude.com, derrière Cloudflare : refusé à Python,
abandonné. Si l'usage ne monte plus, vérifiez d'abord là. Les compteurs d'activité, eux, viennent
de fichiers locaux : ils continuent de fonctionner quoi qu'il arrive.
