"""Point the built in update source at the repository this firmware is built from.

The firmware update page reads
raw.githubusercontent.com/<owner>/<repo>/<branch>/<dir>/manifest.json,
and the defaults for owner/repo come from the git remote so a fork updates from
itself. Everything can be changed at runtime on the update page, or here:

    build_flags = -DFW_UPDATE_OWNER='"someone"' -DFW_UPDATE_BRANCH='"fw"'
"""

import re
import subprocess

Import("env")

BRANCH = "fw"    # the branch the publish-fw workflow pushes the update files to


def github_owner_repo():
    try:
        url = subprocess.check_output(
            ["git", "remote", "get-url", "origin"],
            text=True, stderr=subprocess.DEVNULL, timeout=10).strip()
    except Exception:
        return None
    match = re.search(r"github\.com[:/]+([^/]+)/(.+?)(?:\.git)?$", url)
    if not match:
        return None
    return match.group(1), match.group(2)


already_set = set()
for define in env.get("CPPDEFINES", []):
    already_set.add(define[0] if isinstance(define, (list, tuple)) else define)

owner_repo = github_owner_repo()
if owner_repo and "FW_UPDATE_OWNER" not in already_set and "FW_UPDATE_REPO" not in already_set:
    owner, repo = owner_repo
    print(f"fw update source: {owner}/{repo} branch {BRANCH}")
    env.Append(CPPDEFINES=[
        ("FW_UPDATE_OWNER", env.StringifyMacro(owner)),
        ("FW_UPDATE_REPO", env.StringifyMacro(repo)),
    ])
if "FW_UPDATE_BRANCH" not in already_set:
    env.Append(CPPDEFINES=[("FW_UPDATE_BRANCH", env.StringifyMacro(BRANCH))])
