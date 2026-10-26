"""Named NID data must hash back to its symbol, regardless of library scope."""
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from prepare import nid


class ImportNames(unittest.TestCase):
    def test_named_nids_match_their_contract(self):
        rows = re.findall(r'\{"([^"]+)","([^"]+)"\}',
                          (ROOT / "src/import_names.inc").read_text())
        checked = 0
        for encoded, symbol in rows:
            if symbol.startswith(("order_stub_", "Func_", "sceDiscMap_")):
                continue  # explicitly opaque reverse-engineering labels
            with self.subTest(encoded=encoded, symbol=symbol):
                self.assertEqual(encoded.split("#")[0], nid(symbol))
            checked += 1
        self.assertGreater(checked, 2900)


if __name__ == "__main__":
    unittest.main()
