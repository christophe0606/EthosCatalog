# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Check model ownership and event routing in the production handwriting graph."""
from collections import Counter
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from common.stream_graph import configuration, mk_app
from projects.handwriting_synthesis.app import make_graph
from projects.handwriting_synthesis.nodes import HandwritingNetwork


class HandwritingGraph(unittest.TestCase):
    def test_each_method_has_its_own_node_and_ordered_connections(self):
        graph = make_graph()
        schedule = graph.computeSchedule(config=configuration())
        networks = {node.nodeName: node.params['method'] for node in schedule.allNodes
                    if isinstance(node, HandwritingNetwork)}
        self.assertEqual(networks, dict(lstm1=0, attention=1, lstm2=2, lstm3=3, gmm=4))
        connections = {(source.owner.nodeName, f'oev{source.name}',
                        destination.owner.nodeName, f'iev{destination.name}')
                       for source, destination in graph._eventConnections}
        self.assertEqual(connections, {
            ('send', 'oev0', 'synthesis', 'iev0'),
            ('synthesis', 'oev0', 'send', 'iev0'),
            ('synthesis', 'oev1', 'display', 'iev0'),
            ('synthesis', 'oev2', 'lstm1', 'iev0'),
            ('lstm1', 'oev0', 'attention', 'iev0'),
            ('attention', 'oev0', 'lstm2', 'iev0'),
            ('lstm2', 'oev0', 'lstm3', 'iev0'),
            ('lstm3', 'oev0', 'gmm', 'iev0'),
            ('gmm', 'oev0', 'synthesis', 'iev1'),
        })
        self.assertEqual(Counter(schedule.allNodes[i].nodeName for i in schedule.schedule),
                         dict(uart=1, editor=1, send=1))
        self.assertEqual(schedule.memory, 128)

    def test_reused_wrapper_has_one_parameter_type_and_selector_definition(self):
        temporary_root = (ROOT/'out/handwriting-tests').resolve()
        assert temporary_root.is_relative_to(ROOT)
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as directory:
            output = Path(directory)
            mk_app(make_graph(), output, globals={}, runtime={})
            params = (output/'app_params.h').read_text()
            scheduler = (output/'scheduler_app.cpp').read_text()
            self.assertEqual(params.count('} HandwritingNetworkParams;'), 1)
            self.assertEqual(scheduler.count('HandwritingNetwork::selectors ='), 1)
            for name in ('lstm1', 'attention', 'lstm2', 'lstm3', 'gmm'):
                self.assertIn(f'HandwritingNetworkParams {name};', params)
                self.assertIn(f'HandwritingNetwork(evtQueue,params->{name})', scheduler)


if __name__ == '__main__':
    unittest.main()
