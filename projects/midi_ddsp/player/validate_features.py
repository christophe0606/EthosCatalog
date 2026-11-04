# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Compare score preprocessing against selected original upstream functions.

AST extraction executes the preserved function bodies without importing the
original training package or its optional MIDI/SoundFont dependency tree.
"""
import ast
import hashlib
import json
from pathlib import Path
import numpy as np
import pandas as pd
import tensorflow as tf
from score_features import make_features

HERE = Path(__file__).resolve().parent
REFERENCE = HERE.parent / 'model/reference'
KEYS = ['volume', 'vol_fluc', 'vibrato', 'brightness', 'attack', 'vol_peak_pos']


def functions(path, names, namespace):
    tree = ast.parse(path.read_text(encoding='utf-8'))
    selected = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    if {node.name for node in selected} != set(names):
        raise ValueError(f'Missing reference functions: {path}')
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(path), 'exec'), namespace)


def main():
    namespace = {'np': np, 'pd': pd, 'tf': tf, 'CONDITIONING_KEYS': KEYS}
    functions(REFERENCE/'inference_utils.py', [
        'scale_expression_generator_output',
        'expression_generator_output_to_conditioning_df',
        'conditioning_df_to_midi_features', 'conditioning_df_to_dict'], namespace)
    functions(REFERENCE/'ddsp_nn.py', ['get_note_mask_from_onset'], namespace)
    cases = [
        ('notes_rests_and_one_frame_regions', [60, 0, 60, 72, 0], [7, 3, 1, 12, 5]),
        ('leading_rest', [0, 48, 0], [2, 9, 1]),
        ('all_rest', [0], [1000]),
        ('region_mask_limit', [60 if i % 3 else 0 for i in range(102)], [1]*102),
    ]
    results = []
    generator = np.random.default_rng(20261104)
    for name, pitches, lengths in cases:
        pitches = np.asarray(pitches, dtype=np.int64)
        lengths = np.asarray(lengths, dtype=np.int64)
        expression = generator.uniform(-.5, 1.5, (len(pitches), 6)).astype(np.float32)
        expression[pitches == 0] = 0.
        duration, mapping, actual = make_features(pitches, lengths, expression)
        dataframe = namespace['expression_generator_output_to_conditioning_df'](
            tf.constant(expression[None]),
            {'note_pitch': tf.constant(pitches[None]), 'note_length': tf.constant(duration[None])})
        q_pitch, _, _, onsets, offsets = namespace['conditioning_df_to_midi_features'](dataframe)
        conditioning = namespace['conditioning_df_to_dict'](dataframe)
        mask = namespace['get_note_mask_from_onset'](q_pitch, onsets)
        index = tf.cumsum(mask, axis=1) * tf.cast(mask != 0, tf.float32)
        length = tf.reduce_max(index, axis=1, keepdims=True) * tf.cast(index > 0, tf.float32)
        relative = tf.math.divide_no_nan(tf.reduce_sum(index, axis=-1)[..., None],
                                       tf.reduce_sum(length, axis=-1)[..., None])
        expected = tf.concat([*[conditioning[key] for key in KEYS],
                              tf.constant(q_pitch)/127., tf.cast(onsets[..., None], tf.float32),
                              tf.cast(offsets[..., None], tf.float32), relative], axis=-1).numpy()[0]
        error = float(np.max(np.abs(actual - expected)))
        np.testing.assert_allclose(actual, expected, rtol=0, atol=1e-7)
        if len(mapping) != int(lengths.sum()):
            raise AssertionError('Frame count changed')
        results.append({'case': name, 'notes_including_rests': len(pitches),
                        'frames': len(mapping), 'maximum_absolute_error': error})
    report = {'status': 'PASS', 'scope': 'Contiguous integer-frame monophonic regions only; no MIDI time rounding, neural or audio validation',
              'reference_sha256': {name: hashlib.sha256((REFERENCE/name).read_bytes()).hexdigest()
                                   for name in ('inference_utils.py', 'ddsp_nn.py')},
              'cases': results}
    (HERE/'feature_validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
