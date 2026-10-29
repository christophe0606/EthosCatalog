# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import numpy as np
import tensorflow as tf
P=Path(__file__).resolve().parents[1]
# Existing independent PCM fixture repeated to exercise three overlapping frames.
import re
s=(P/'model/frontend_test_data.h').read_text();vals=[int(x) for x in re.search(r'pcm\[400\] = \{([^}]+)',s)[1].split(',')]
pcm=np.resize(np.array(vals,dtype=np.int16),800)
x=tf.convert_to_tensor(pcm.astype(np.float32)/32768)
stft=tf.signal.stft(x,400,160,512,window_fn=tf.signal.hann_window,pad_end=False)
mel=tf.signal.linear_to_mel_weight_matrix(64,257,16000,125.,7500.)
logs=tf.math.log(tf.matmul(tf.abs(stft),mel)+.001).numpy()
header='// Copyright 2026 Arm Limited and/or its affiliates.\n// SPDX-License-Identifier: Apache-2.0\n// Independent TensorFlow STFT windows at samples0,160,320.\n#pragma once\n#include <stdint.h>\n'
header+='static const int16_t stream_pcm[800]={'+','.join(map(str,pcm))+'};\n'
header+='static const float stream_expected[3][64]={'+','.join('{'+','.join(f'{v:.9e}f' for v in row)+'}' for row in logs)+'};\n'
(P/'model/frontend_stream_test.h').write_text(header)
