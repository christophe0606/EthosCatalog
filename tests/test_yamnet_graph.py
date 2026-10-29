# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""The expensive classifier must remain outside the 10 ms dataflow schedule."""
from collections import Counter
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from common.stream_graph import configuration
from projects.yamnet.app import make_graph

class YAMNetGraphTests(unittest.TestCase):
    def test_cpu_postprocessing_separates_network_from_lcd(self):
        graph = make_graph()
        connections = {(source.owner.nodeName, destination.owner.nodeName)
                       for source, destination in graph._eventConnections}
        self.assertEqual(connections, {
            ('send', 'network'),
            ('network', 'send'),
            ('network', 'postprocess'),
            ('postprocess', 'lcd'),
            ('postprocess', 'network'),
        })

    def test_audio_schedule_excludes_inference_and_display(self):
        schedule = make_graph().computeSchedule(config=configuration())
        counts = Counter(schedule.allNodes[i].nodeName for i in schedule.schedule)
        self.assertEqual(counts, dict(microphone=1, pcm=1, audio_window=1, log_mel=1, send=1))
        names = {node.nodeName for node in schedule.allNodes}
        self.assertIn('network', names)
        self.assertIn('postprocess', names)
        self.assertIn('lcd', names)
        self.assertEqual(schedule.memory, 2240)

if __name__ == '__main__':
    unittest.main()
