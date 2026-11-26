#!/usr/bin/env python3
"""Saglabā Nord Pool Latvijas cenas (šodiena + rītdiena) failā data/electricity.json.

Web versija vispirms lasa Elering API tieši. Šis fails ir rezerve, ja pārlūks to nevar
(piemēram, CORS vai Elering nav pieejams). Formāts ir tāds pats kā Elering atbildei,
tikai ar zonu "lv": {"updated": "...", "data": {"lv": [{"timestamp": 1764108000, "price": 85.9}]}}.
"""
from __future__ import annotations

import datetime as dt
import json
import sys
import urllib.request
from pathlib import Path
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "data" / "electricity.json"
URL = "https://dashboard.elering.ee/api/nps/price?start={start}&end={end}"


def iso(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")


def main() -> int:
    riga = ZoneInfo("Europe/Riga")
    midnight = dt.datetime.now(riga).replace(hour=0, minute=0, second=0, microsecond=0)
    url = URL.format(start=iso(midnight), end=iso(midnight + dt.timedelta(days=2)))
    request = urllib.request.Request(url, headers={"User-Agent": "S3supermini-Energ/1.0",
                                                   "Accept": "application/json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        payload = json.load(response)
    slots = [{"timestamp": int(e["timestamp"]), "price": float(e["price"])}
             for e in payload.get("data", {}).get("lv", [])]
    if not slots:
        print("Elering neatdeva Latvijas cenas; vecais fails paliek.", file=sys.stderr)
        return 1
    OUTPUT.parent.mkdir(exist_ok=True)
    feed = {"updated": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
            "data": {"lv": slots}}
    OUTPUT.write_text(json.dumps(feed, separators=(",", ":")) + "\n", "utf-8")
    print(f"{len(slots)} intervāli -> {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
