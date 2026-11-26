#!/usr/bin/env python3
"""Veido data/fuel.json ar Latvijas degvielas staciju cenām un koordinātām.

Latvijas tīkli (Circle K, Neste, Virši, Viada u.c.) savās lapās publicē katras
degvielas zemāko cenu un adreses, kurās tā ir spēkā. Šis skripts:

1. nolasa avotus no tools/fuel_sources.json;
2. katrā lapā meklē rindas ar degvielas nosaukumu, cenu (x,xxx) un adresēm;
3. adresēm atrod koordinātas ar OpenStreetMap Nominatim (kešs data/geocache.json);
4. pievieno data/manual_stations.json (paša ievadītas vai labotas stacijas);
5. ieraksta data/fuel.json formātā, ko lasa ESP.

Palaišana:  python3 tools/fuel_feed.py            (no S3supermini_Energ mapes)
            python3 tools/fuel_feed.py --offline  (bez ģeokodēšanas, tikai kešs)
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "tools" / "fuel_sources.json"
DATA = ROOT / "data"
OUTPUT = DATA / "fuel.json"
GEOCACHE = DATA / "geocache.json"
MANUAL = DATA / "manual_stations.json"

USER_AGENT = "S3supermini-Energ-fuel-feed/1.0 (+https://github.com/komuna7/egg1)"
FUEL_KEYS = ("lpg", "95", "98", "dd")
PRICE_RE = re.compile(r"(?<![\d.,])([0-3])[.,](\d{3})(?![\d])")
# Latvijā cena vienmēr ir 0,5–3 EUR/l.
PRICE_MIN, PRICE_MAX = 0.4, 3.5
LV_BOUNDS = (55.6, 58.1, 20.9, 28.3)  # lat min, lat max, lon min, lon max


def fold(text: str) -> str:
    """Mazie burti bez garumzīmēm salīdzināšanai."""
    text = unicodedata.normalize("NFKD", text.lower())
    return "".join(c for c in text if not unicodedata.combining(c))


def classify_fuel(label: str, rules: dict[str, str] | None = None) -> str | None:
    """Degvielas nosaukums -> 'lpg' | '95' | '98' | 'dd' | None."""
    text = fold(label)
    if rules:
        for key, pattern in rules.items():
            if re.search(pattern, text):
                return key if key in FUEL_KEYS else None
    if re.search(r"adblue|cng|elektr|kwh|lng|e85|biometan", text):
        return None
    if re.search(r"gaze|lpg|autogaz|propan", text):
        return "lpg"
    if re.search(r"\b98\b|98e|98 ?e\b", text):
        return "98"
    if re.search(r"\b95\b|95e|95 ?e\b", text):
        return "95"
    if re.search(r"dizel|diesel|\bdd\b|\bd\b|miles\+? ?d|futura d|\bd ?(?:miles|futura)", text):
        return "dd"
    return None


def find_price(text: str) -> float | None:
    for whole, frac in PRICE_RE.findall(text):
        value = float(f"{whole}.{frac}")
        if PRICE_MIN <= value <= PRICE_MAX:
            return value
    return None


ADDRESS_HINT = re.compile(
    r"\b(iela|ielа|gatve|šoseja|soseja|ceļš|cels|prospekts|bulvāris|bulvaris|laukums|"
    r"novads|pagasts|pag\.|nov\.|rīga|riga|daugavpils|liepāja|jelgava|jūrmala|ventspils|"
    r"rēzekne|valmiera|ogre|tukums|cēsis|sigulda|salaspils|\d+[a-z]?\b)",
    re.IGNORECASE,
)


def split_addresses(text: str) -> list[str]:
    parts = re.split(r"\n|;|•|\|", text)
    result = []
    for part in parts:
        part = re.sub(r"\s+", " ", part).strip(" ,.-–")
        if len(part) < 6 or not re.search(r"[A-Za-zĀ-ž]", part):
            continue
        if PRICE_RE.search(part) or classify_fuel(part):
            continue
        if ADDRESS_HINT.search(part):
            result.append(part)
    return result


class TableRows(HTMLParser):
    """Savāc <tr> rindas kā šūnu tekstu sarakstus; <br> kļūst par jaunu rindu."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.rows: list[list[str]] = []
        self._row: list[str] | None = None
        self._cell: list[str] | None = None
        self._skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self._skip += 1
        elif tag == "tr":
            self._row = []
        elif tag in ("td", "th") and self._row is not None:
            self._cell = []
        elif tag in ("br", "p", "li", "div") and self._cell is not None:
            self._cell.append("\n")
        elif tag == "img" and self._cell is not None:
            alt = dict(attrs).get("alt") or dict(attrs).get("title")
            if alt:
                self._cell.append(f" {alt} ")

    def handle_endtag(self, tag):
        if tag in ("script", "style"):
            self._skip = max(0, self._skip - 1)
        elif tag in ("td", "th") and self._cell is not None and self._row is not None:
            self._row.append("".join(self._cell).strip())
            self._cell = None
        elif tag == "tr" and self._row is not None:
            if self._cell is not None:
                self._row.append("".join(self._cell).strip())
                self._cell = None
            if self._row:
                self.rows.append(self._row)
            self._row = None

    def handle_data(self, data):
        if not self._skip and self._cell is not None:
            self._cell.append(data)


def parse_price_table(page: str, rules: dict[str, str] | None = None) -> list[dict]:
    """Atgriež [{'fuel': '95', 'price': 1.559, 'addresses': [...]}, ...]."""
    parser = TableRows()
    parser.feed(page)
    found = []
    for cells in parser.rows:
        fuel = price = None
        addresses: list[str] = []
        for cell in cells:
            if fuel is None and (kind := classify_fuel(cell, rules)) and not find_price(cell):
                fuel = kind
                continue
            if price is None and (value := find_price(cell)) is not None and len(cell) < 40:
                price = value
                continue
            addresses.extend(split_addresses(cell))
        if fuel and price is not None:
            found.append({"fuel": fuel, "price": price, "addresses": addresses})
    return found


def fetch(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT,
                                                   "Accept-Language": "lv,en;q=0.5"})
    with urllib.request.urlopen(request, timeout=30) as response:
        charset = response.headers.get_content_charset() or "utf-8"
        return response.read().decode(charset, errors="replace")


class Geocoder:
    """Nominatim ar kešu; ne biežāk kā 1 pieprasījums sekundē (Nominatim noteikumi)."""

    def __init__(self, path: Path, offline: bool) -> None:
        self.path = path
        self.offline = offline
        self.cache: dict[str, list[float] | None] = (
            json.loads(path.read_text("utf-8")) if path.exists() else {})
        self.last = 0.0
        self.changed = False

    def locate(self, address: str) -> tuple[float, float] | None:
        key = fold(address)
        if key in self.cache:
            hit = self.cache[key]
            return tuple(hit) if hit else None
        if self.offline:
            return None
        wait = 1.1 - (time.time() - self.last)
        if wait > 0:
            time.sleep(wait)
        self.last = time.time()
        query = urllib.parse.urlencode({"q": address, "countrycodes": "lv", "format": "json",
                                        "limit": 1})
        try:
            result = json.loads(fetch("https://nominatim.openstreetmap.org/search?" + query))
        except Exception as error:  # tīkla kļūda: nekešo, mēģinās nākamreiz
            print(f"  ģeokods neizdevās: {address}: {error}", file=sys.stderr)
            return None
        point = None
        if result:
            lat, lon = float(result[0]["lat"]), float(result[0]["lon"])
            if LV_BOUNDS[0] <= lat <= LV_BOUNDS[1] and LV_BOUNDS[2] <= lon <= LV_BOUNDS[3]:
                point = (round(lat, 5), round(lon, 5))
        self.cache[key] = list(point) if point else None
        self.changed = True
        return point

    def save(self) -> None:
        if self.changed:
            self.path.write_text(json.dumps(self.cache, ensure_ascii=False, indent=0,
                                            sort_keys=True) + "\n", "utf-8")


def merge_station(stations: dict, brand: str, address: str, lat: float, lon: float,
                  prices: dict[str, float]) -> None:
    key = (fold(brand), round(lat, 4), round(lon, 4))
    entry = stations.setdefault(key, {"b": brand, "a": address, "lat": lat, "lon": lon, "p": {}})
    for fuel, price in prices.items():
        old = entry["p"].get(fuel)
        entry["p"][fuel] = price if old is None else min(old, price)


def build(offline: bool = False, sources_path: Path = SOURCES) -> dict:
    sources = json.loads(sources_path.read_text("utf-8"))
    geocoder = Geocoder(GEOCACHE, offline)
    stations: dict = {}
    report = []
    for source in sources:
        if not source.get("enabled", True):
            continue
        brand = source["brand"]
        try:
            rows = parse_price_table(fetch(source["url"]), source.get("fuel_rules"))
        except Exception as error:
            report.append(f"{brand}: kļūda {error}")
            continue
        located = 0
        for row in rows:
            for address in row["addresses"]:
                query = address if re.search(r"latvij", fold(address)) else f"{address}, Latvija"
                point = geocoder.locate(query)
                if not point:
                    continue
                located += 1
                merge_station(stations, brand, address, point[0], point[1],
                              {row["fuel"]: row["price"]})
        report.append(f"{brand}: {len(rows)} cenas, {located} adreses ar koordinātām")
    if MANUAL.exists():
        for item in json.loads(MANUAL.read_text("utf-8")):
            prices = {k: float(v) for k, v in item.get("p", {}).items() if k in FUEL_KEYS and v}
            merge_station(stations, item["b"], item.get("a", ""), float(item["lat"]),
                          float(item["lon"]), prices)
        report.append(f"manuālās stacijas: {MANUAL.name}")
    geocoder.save()
    for line in report:
        print(line)
    ordered = sorted(stations.values(), key=lambda s: (s["lat"], s["lon"]))
    # "updated" jābūt pirms "stations": ESP lasa failu kā plūsmu.
    return {"updated": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
            "stations": ordered}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--offline", action="store_true", help="neģeokodēt jaunas adreses")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    DATA.mkdir(exist_ok=True)
    feed = build(args.offline)
    if not feed["stations"] and args.output.exists():
        print("Neviena stacija netika atrasta; vecais fails paliek.", file=sys.stderr)
        return 1
    text = json.dumps(feed, ensure_ascii=False, separators=(",", ":"))
    # Pa vienai stacijai rindā, lai git izmaiņas būtu pārskatāmas.
    text = text.replace('},{"b"', '},\n{"b"')
    args.output.write_text(text + "\n", "utf-8")
    print(f"{len(feed['stations'])} stacijas -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
