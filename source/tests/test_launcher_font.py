"""The launcher's titles use the serif font: no title face ships with it."""
import sys
import unittest

from paths import ROOT
sys.path.insert(0, str(ROOT / 'launcher'))
import port_launcher_win as launcher  # noqa: E402


class HeadingFontTest(unittest.TestCase):
    def test_titles_use_the_serif_font_or_the_fallback(self):
        self.assertEqual(launcher.heading_font(12), (launcher.SERIF_FONT, 12))
        self.assertEqual(launcher.heading_font(11, 'Segoe UI'), ('Segoe UI', 11))
        self.assertEqual(launcher.heading_font(15, every_language=True), (launcher.SERIF_FONT, 15))

    def test_headings_keep_their_text(self):
        self.assertEqual(launcher.heading_text('GRÁFICOS'), 'GRÁFICOS')
        self.assertEqual(launcher.heading_text('Графика'), 'Графика')

    def test_windows_package_ships_no_title_font(self):
        script = (ROOT / 'packaging' / 'windows' / 'package.sh').read_text(encoding='utf-8')
        self.assertNotIn('.ttf', script)


if __name__ == '__main__':
    unittest.main()
