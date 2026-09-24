# -*- coding: utf-8 -*-
# Rebuild i18n main: full tree from 39988c7 with repaired catalog/VERSION.
import base64, json, os, subprocess, sys

REPO = "unixwin/winuxcmd-i18n"
GOOD = "39988c756ec0a4d5febc53a588f47b3aee5bcffa"
TMP = os.environ.get("TEMP")

def gh_api(args, stdin=None):
    r = subprocess.run(["gh", "api"] + args, capture_output=True, text=True,
                       input=stdin)
    if r.returncode != 0:
        sys.exit("gh fail: " + r.stderr)
    return json.loads(r.stdout)

def api(path, payload=None):
    if payload is None:
        return gh_api(["repos/" + REPO + "/" + path])
    return gh_api(["repos/" + REPO + "/" + path, "--input", "-"],
                  json.dumps(payload))

# 1. all files from the last known-good complete tree
good = api("git/trees/" + GOOD + "?recursive=1")
entries = [{"path": e["path"], "mode": e["mode"], "type": "blob",
            "sha": e["sha"]}
           for e in good["tree"]
           if e["type"] == "blob" and e["path"] not in
           ("catalogs/zh-CN/catalog.json", "VERSION")]
print("files restored from", GOOD[:10], ":", len(entries))

# 2. repaired blobs (they live on the current main tree)
main_sha = api("git/ref/heads/main")["object"]["sha"]
main_commit = api("git/commits/" + main_sha)
main_tree = api("git/trees/" + main_commit["tree"]["sha"])
blob_map = {}
def walk(tree_sha, prefix):
    for e in api("git/trees/" + tree_sha)["tree"]:
        if e["type"] == "blob":
            blob_map[prefix + e["path"]] = e["sha"]
        elif e["type"] == "tree":
            walk(e["sha"], prefix + e["path"] + "/")
walk(main_tree["sha"], "")
entries.append({"path": "catalogs/zh-CN/catalog.json", "mode": "100644",
                "type": "blob", "sha": blob_map["catalogs/zh-CN/catalog.json"]})
entries.append({"path": "VERSION", "mode": "100644", "type": "blob",
                "sha": blob_map["VERSION"]})
print("total entries:", len(entries))

# 3. full tree (base_tree keeps everything else identical to GOOD)
tree = api("git/trees", {"base_tree": GOOD, "tree": entries})["sha"]
print("tree:", tree[:10])

# 4. commit on top of current main, force-update
msg = ("restore repository files (README, packaging script, CI workflows) "
       "accidentally dropped by earlier API-tree commits; keep repaired "
       "0.6.16 catalog")
nc = api("git/commits", {"message": msg, "tree": tree,
                         "parents": [main_sha]})["sha"]
print("commit:", nc[:10])
print("main ->", api("git/refs/heads/main",
                     {"sha": nc, "force": True})["object"]["sha"][:10])
