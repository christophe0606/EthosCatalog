"""Production graph cadence and event-only display checks."""
from collections import Counter
from pathlib import Path
import sys, unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from projects.kws_micronet_l.app import make_graph
from common.stream_graph import configuration

class KwsGraph(unittest.TestCase):
    def test_feature_overlap_controls_inference_stride(self):
        for overlap in (0,24,48):
            with self.subTest(overlap=overlap):
                s=make_graph(overlap).computeSchedule(config=configuration())
                counts=Counter(s.allNodes[i].nodeName for i in s.schedule)
                for name in ('microphone','pcm','audio_window','mfcc','display_clock'):
                    self.assertEqual(counts[name],49-overlap)
                self.assertEqual(counts['mfcc_window'],1)
                self.assertEqual(counts['inference'],1)
                self.assertEqual(counts['lcd'],0)
                context=next(n for n in s.allNodes if n.nodeName=='mfcc_window')
                self.assertEqual(context.i.nbSamples,10*(49-overlap))
                self.assertEqual(context.o.nbSamples,490)
    def test_invalid_overlap(self):
        for overlap in (-1,49,True,1.5):
            with self.subTest(overlap=overlap),self.assertRaises(ValueError): make_graph(overlap)

if __name__=='__main__': unittest.main()
