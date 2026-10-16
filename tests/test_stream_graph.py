"""Check sample-rate balancing using the production graph and generator."""
from collections import Counter
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
spec = importlib.util.spec_from_file_location('mram_app', ROOT/'projects/template_mram/app.py')
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)
from common.stream_graph import configuration
from common.nodes import FFT, SlidingBuffer


class StreamGraph(unittest.TestCase):
    def test_compatible_rates_balance_without_packet_types(self):
        for pcm_samples, executions in ((128, 4), (256, 2), (512, 1), (1024, 1)):
            with self.subTest(pcm_samples=pcm_samples):
                schedule = app.make_graph(pcm_samples).computeSchedule(config=configuration())
                counts = Counter(schedule.allNodes[i].nodeName for i in schedule.schedule)
                # At 1024 the converter consumes two microphone firings.
                passes = max(1, pcm_samples//512)
                self.assertEqual(counts['pcm'], executions)
                for name in ('microphone', 'gain', 'sliding', 'fft', 'magnitude', 'send'):
                    self.assertEqual(counts[name], passes)
                self.assertEqual(counts['lcd'], 0)
                self.assertEqual(schedule.scheduleLength, 8*passes+executions)
                for src, dst in schedule.edges:
                    self.assertIn(src.theType.ctype, ('int16_t', 'float'))
                    self.assertEqual(src.theType.ctype, dst.theType.ctype)

    def test_default_graph_memory_and_schedule(self):
        schedule = app.make_graph().computeSchedule(config=configuration())
        self.assertEqual(schedule.scheduleLength, 9)
        self.assertEqual(schedule.memory, 16384)

    def test_invalid_dsp_lengths_are_rejected(self):
        for samples in (0, -1, 17, 63, 8192):
            with self.subTest(samples=samples), self.assertRaises(ValueError):
                FFT('fft', samples)
        with self.assertRaises(ValueError):
            SlidingBuffer('history', 512, 256)


if __name__ == '__main__':
    unittest.main()
