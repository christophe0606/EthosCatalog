# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Exercise a mixed CPU/Ethos multi-method PTE through the real serializer."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from create_ai_layer import components, export_model


class ModelExport(unittest.TestCase):
    def test_named_cpu_and_ethos_methods_survive_serialization(self):
        import torch
        from executorch.backends.arm.ethosu import EthosUCompileSpec
        from executorch.backends.arm.quantizer import EthosUQuantizer, get_symmetric_a16w8_quantization_config
        from executorch.exir._serialize._program import deserialize_pte_binary
        from torchao.quantization.pt2e.quantize_pt2e import convert_pt2e, prepare_pt2e

        class Gain(torch.nn.Module):
            def forward(self, value):
                return value * 2.0

        class Lookup(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.register_buffer("table", torch.arange(12, dtype=torch.float32).reshape(3, 4))

            def forward(self, token):
                return torch.softmax(torch.nn.functional.embedding(token, self.table), dim=-1).clone()

        spec = EthosUCompileSpec(target="ethos-u85-256", system_config="Ethos_U85_SRAM_OSPI",
                                memory_mode="Shared_Sram", config_ini=str(ROOT / ".cmsis/ensemble_vela.ini"))
        sample = torch.linspace(-1, 1, 16).reshape(1, 1, 16, 1)
        graph = torch.export.export(Gain(), (sample,)).module(check_guards=False)
        quantizer = EthosUQuantizer(spec)
        quantizer.set_global(get_symmetric_a16w8_quantization_config(epsilon=2**-24))
        prepared = prepare_pt2e(graph, quantizer)
        prepared(sample)
        quantized = torch.export.export(convert_pt2e(prepared, fold_quantize=True), (sample,))
        lookup = torch.export.export(Lookup(), (torch.tensor([1]),))
        programs = {"backbone": quantized, "text_embedding": lookup}
        (ROOT / "tmp").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
            # Keep FlatBuffer compiler scratch files inside the test workspace.
            with patch.object(tempfile, "tempdir", directory):
                inspected = []
                payload, delegates = export_model(programs, spec, cpu_methods=("text_embedding",),
                                                  inspection_callback=lambda edge: inspected.append(edge.methods))
                decoded = deserialize_pte_binary(payload).program
                self.assertEqual({plan.name for plan in decoded.execution_plan}, set(programs))
                plans = {plan.name: plan for plan in decoded.execution_plan}
                self.assertEqual(len(plans["backbone"].delegates), 1)
                self.assertEqual(len(plans["text_embedding"].delegates), 0)
                self.assertEqual(delegates, 1)
                self.assertEqual(inspected, [set(programs)])
                pack = Path(directory) / "pack"
                pack.mkdir()
                # Component names intentionally differ from _softmax and
                # _clone_dim_order operator names in the serialized program.
                (pack / "fixture.pdsc").write_text("<components>" + "".join(
                    f'<component Csub="{name}"/>' for name in
                    ("Portable embedding", "Portable softmax", "Portable clone_dim_order",
                     "Portable clone", "Quantized quantize", "Quantized dequantize")
                ) + "</components>")
                _, operators = components(payload, pack)
                self.assertIn("Portable embedding", operators)
                self.assertIn("Portable softmax", operators)
                (pack / "fixture.pdsc").write_text('<component Csub="Portable embedding"/>')
                with self.assertRaisesRegex(ValueError, "_softmax"):
                    components(payload, pack)
                legacy, legacy_delegates = export_model(quantized, spec)
                legacy_plan = deserialize_pte_binary(legacy).program.execution_plan
                self.assertEqual([plan.name for plan in legacy_plan], ["forward"])
                self.assertEqual(legacy_delegates, 1)
                with self.assertRaisesRegex(ValueError, "Unknown CPU-only methods"):
                    export_model(programs, spec, cpu_methods=("typo",))
                with self.assertRaisesRegex(ValueError, "text_embedding"):
                    export_model({"text_embedding": lookup}, spec)


if __name__ == "__main__":
    unittest.main()
