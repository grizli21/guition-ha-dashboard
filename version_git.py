# 02/11/2026 : version du firmware = commit git (« +modifs » si des fichiers suivis diffèrent du commit), écrite dans
# include/version_git.h avant la compilation (hors git) ; réécrite seulement si elle change : pas de recompilation inutile
Import("env")
import os
import subprocess


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=env["PROJECT_DIR"], text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ""


version = git("rev-parse", "--short=7", "HEAD") or "inconnue"
if git("status", "--porcelain", "--untracked-files=no"):
    version += "+modifs"
contenu = '#pragma once\n#define UI_VERSION "%s"\n' % version
chemin = os.path.join(env["PROJECT_DIR"], "include", "version_git.h")
ancien = open(chemin).read() if os.path.exists(chemin) else ""
if ancien != contenu:
    with open(chemin, "w") as f:
        f.write(contenu)
print("version du firmware : " + version)
