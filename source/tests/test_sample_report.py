import importlib.util
import unittest

import paths

spec = importlib.util.spec_from_file_location('sample_report', paths.ROOT / 'tools' / 'sample_report.py')
sample_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sample_report)


class SampleReportTest(unittest.TestCase):
    def test_self_and_inclusive_counts(self):
        threads = sample_report.parse([
            '5;thread=gpu;theorder-probe.exe+0x10;theorder-probe.exe+0x20',
            '3;thread=gpu;theorder-probe.exe+0x20;theorder-probe.exe+0x20',  # recursion counts once
            '2;thread=gpu;guest offset 0x1234',
            '7;thread=rec;theorder-probe.exe+0x10',
            'not a sample',
        ])
        self.assertEqual(sorted(threads), ['gpu', 'rec'])
        samples = threads['gpu']
        names = {'theorder-probe.exe+0x10': 'Leaf', 'theorder-probe.exe+0x20': 'Caller'}
        own, inclusive = sample_report.aggregate(samples, lambda f: names.get(f) or sample_report.module_name(f))
        self.assertEqual(own, {'Leaf': 5, 'Caller': 3, 'guest': 2})
        self.assertEqual(inclusive, {'Leaf': 5, 'Caller': 8, 'guest': 2})


if __name__ == '__main__':
    unittest.main()
