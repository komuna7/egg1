"""Pārbauda lapu parsēšanu ar paraugu HTML (bez tīkla): python3 -m unittest tools/test_fuel_feed.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fuel_feed as ff  # noqa: E402

SAMPLE = """<html><body><script>var x='95 1.111';</script>
<table><tr><th>Degviela</th><th>Cena</th><th>Adrese</th></tr>
<tr><td><img alt="milesPLUS 98"></td><td>1,659 EUR</td><td>Rīga, Brīvības gatve 201<br>Jelgava, Rīgas iela 46</td></tr>
<tr><td>miles 95</td><td>1.579</td><td>Rīga, Krasta iela 101; Ogre, Rīgas iela 23</td></tr>
<tr><td>miles D</td><td>1.499</td><td>Daugavpils, Cietokšņa iela 60</td></tr>
<tr><td>Autogāze</td><td>0.819</td><td>Liepāja, Ganību iela 120</td></tr>
<tr><td>AdBlue</td><td>0.999</td><td>Rīga, Krasta iela 101</td></tr>
<tr><td>Elektroenerģija</td><td>0.390 EUR/kWh</td><td>Rīga</td></tr>
</table></body></html>"""


class ParseTest(unittest.TestCase):
    def test_classify(self):
        cases = {"Futura 95": "95", "95E": "95", "Neste Futura 98": "98", "Pro Diesel": "dd",
                 "DD": "dd", "miles D": "dd", "Gāze": "lpg", "LPG": "lpg", "AdBlue": None,
                 "CNG": None}
        for label, kind in cases.items():
            self.assertEqual(ff.classify_fuel(label), kind, label)

    def test_price(self):
        self.assertEqual(ff.find_price("1,579 EUR/l"), 1.579)
        self.assertIsNone(ff.find_price("12.345"))
        self.assertIsNone(ff.find_price("2026"))

    def test_table(self):
        rows = {r["fuel"]: r for r in ff.parse_price_table(SAMPLE)}
        self.assertEqual(set(rows), {"98", "95", "dd", "lpg"})
        self.assertEqual(rows["98"]["price"], 1.659)
        self.assertEqual(rows["98"]["addresses"], ["Rīga, Brīvības gatve 201", "Jelgava, Rīgas iela 46"])
        self.assertEqual(rows["95"]["addresses"], ["Rīga, Krasta iela 101", "Ogre, Rīgas iela 23"])
        self.assertEqual(rows["lpg"]["price"], 0.819)

    def test_merge_keeps_lowest(self):
        stations = {}
        ff.merge_station(stations, "Virši", "A", 56.9, 24.1, {"95": 1.6})
        ff.merge_station(stations, "Virši", "A", 56.9, 24.1, {"95": 1.55, "dd": 1.5})
        (entry,) = stations.values()
        self.assertEqual(entry["p"], {"95": 1.55, "dd": 1.5})


if __name__ == "__main__":
    unittest.main()
