# SPDX-License-Identifier: GPL-3.0-or-later
# Writes the reference's folder: data.js, the page's assets, llms.txt and llms-full.txt.

import json
import shutil


def clear(out):
    """Empties the output folder; refuses one that holds files but no data.js, which is no reference."""
    out.mkdir(parents=True, exist_ok=True)
    children = list(out.iterdir())
    if children and not (out / "data.js").is_file():
        raise FileExistsError("%s is not empty and holds no data.js: pick an empty folder or an earlier output" % out)
    for child in children:
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def write_site(out, api, assets, index_txt, full_txt):
    """The browsable page (data.js with the model, then every file of `assets`) and the two text files."""
    clear(out)
    (out / "data.js").write_text("const WXL_API = " + json.dumps(api, ensure_ascii=False, separators=(",", ":"))
                                 + ";\n", encoding="utf-8")
    for asset in sorted(assets.iterdir()):
        if asset.is_file():
            shutil.copyfile(asset, out / asset.name)
    (out / "llms.txt").write_text(index_txt, encoding="utf-8")
    (out / "llms-full.txt").write_text(full_txt, encoding="utf-8")
