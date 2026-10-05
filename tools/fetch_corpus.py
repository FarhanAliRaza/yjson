"""Download the 14 comparison inputs from a pinned simdjson-data revision."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import urllib.request

NAMES = "numbers mesh.pretty marine_ik mesh apache_builds random github_events citm_catalog twitter instruments gsoc-2018 twitterescaped canada update-center".split()
DEST = Path(__file__).resolve().parents[1] / "build" / "jsonexamples"


def main():
    with urllib.request.urlopen("https://api.github.com/repos/simdjson/simdjson-data/commits/master") as response:
        revision = json.load(response)["sha"]
    DEST.mkdir(parents=True, exist_ok=True)

    def fetch(name):
        url = f"https://raw.githubusercontent.com/simdjson/simdjson-data/{revision}/jsonexamples/{name}.json"
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
        json.loads(data)
        (DEST / f"{name}.json").write_bytes(data)
        print(f"Downloaded {name}: {len(data):,} bytes", flush=True)
        return {"name": name, "url": url, "sha256": hashlib.sha256(data).hexdigest()}

    with ThreadPoolExecutor(max_workers=4) as executor:
        files = list(executor.map(fetch, NAMES))
    (DEST / "manifest.json.txt").write_text(json.dumps({"revision": revision, "files": files}, indent=2) + "\n")
    print(f"Corpus: {DEST}")


if __name__ == "__main__":
    main()
