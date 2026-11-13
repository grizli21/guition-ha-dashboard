# The "AI" page tooling — feeding `pc/ia` and `pc/ia_activite` from your PC

The rail's "AI" page ([CONTRAT_MQTT.md, "AI page" section](../../CONTRAT_MQTT.md)) reads two topics
published **by the PC**, not by Home Assistant. Without them the page stays empty. These tools
publish them from a PC running **Claude Code** — written for it, but the format is open.

## What each topic contains

| Topic (retained) | Contents | Source |
|---|---|---|
| `pc/ia` | `{"comptes":[{nom, h5, h5_reset, semaine, semaine_reset, maj}]}` — a single account, the **current** one: % used over the 5-hour and weekly windows, reset time of each window | Claude Code's usage endpoint (`/api/oauth/usage`), the very source its `/usage` command reads |
| `pc/ia_activite` | `{etat, alerte, tokens, outils, prompts, sessions, histo[30], maj}` — "reflechit / attente / repos", the alert (authorization, question), produced tokens, 30 ten-minute slices | Claude Code **hooks** + **local conversation logs**: no network call |

## Installation, step by step

1. **jq** (the hook uses it): `sudo apt install jq` (or your distribution's equivalent).
2. **The hook**: copy `claude-hook-activite` wherever you like and make it executable:
   `chmod +x claude-hook-activite`. It appends one JSON line per event to
   `~/.local/state/claude-activite/evenements.log`. It prints nothing and always exits 0: a
   failure never gets in Claude Code's way.
3. **Settings**: merge the `hooks` key of `settings.hooks.exemple.json` into
   `~/.claude/settings.json`, replacing the path with yours. The five events:
   `UserPromptSubmit`, `PostToolUse`, `Stop`, `Notification`, `PreToolUse` (matcher `AskUserQuestion`);
   `timeout: 5`. Without `PreToolUse(AskUserQuestion)`, the screen never shows "question for you".
4. **Configuration**: `cp config.exemple.json config.json`, then set the broker address, MQTT user
   and password (`chmod 600 config.json`). One Claude Code account = one **config directory**
   (`~/.claude` by default, or your `CLAUDE_CONFIG_DIR` when you run several); the displayed e-mail
   is read from that directory — it is never hard-coded here.
5. **Trial** (nothing is published): `python3 publier_ia.py --essai` — prints the two messages
   exactly as they would go out.
6. **As a service**: run `python3 publier_ia.py` at startup, or use the **user** systemd service:
   copy `publier-ia.service.exemple` to `~/.config/systemd/user/publier-ia.service`, adapt the
   paths, then `systemctl --user daemon-reload && systemctl --user enable --now publier-ia`.
   Logs: `journalctl --user -u publier-ia`.

## Guardrails (the same as the production agent)

- Usage is re-read **at most every 5 minutes** per account (2 minutes right after a `Stop` hook
  ended an answer, never less than 2 minutes apart). An HTTP 429 sets the account aside for its
  `Retry-After` (10 minutes otherwise).
- **The token stays on the PC**: never printed, never written anywhere, and only ever sent as the
  `Authorization` header, to the usage endpoint. When it expires (Claude Code renews it while it
  runs), reading stops and the screen keeps the last values with their time ("read ago …").

## ⚠️ Know this

`/api/oauth/usage` **is not a documented public API**: it is what Claude Code itself calls for
`/usage`. It may change or close without notice — as of 2026-10-31, refreshing the token yourself
already went through platform.claude.com, behind Cloudflare: refused to Python, abandoned. If the
usage gauge stops moving, check there first. The activity counters come from local files: they keep
working no matter what.
