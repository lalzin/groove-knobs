#!/usr/bin/env python3
"""web-assets/versions.json, read by the plug-ins' update check (gk::UpdateCheck),
from web-assets/plugins.json. Publish it at https://www.lalzin.com/plugins/versions.json
each time a version changes (after the new installers are online).

    python3 scripts/make_versions_json.py
"""
import datetime, json, pathlib

root = pathlib.Path(__file__).resolve().parent.parent / "web-assets"
plugins = json.loads((root / "plugins.json").read_text())["plugins"]
out = {
    "updated": datetime.date.today().isoformat(),
    "plugins": {
        p["slug"]: {
            "version": p["version"],
            "url": f"https://www.lalzin.com/plugins/{p['slug']}",
        }
        for p in plugins
    },
}
(root / "versions.json").write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n")
print(json.dumps(out, indent=2))
