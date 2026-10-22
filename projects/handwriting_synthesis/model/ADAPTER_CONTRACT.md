# Handwriting model adapter contract

All five ExecuTorch methods use one contiguous **float32** tensor of shape
`[1,N]` and return one float32 `[1,M]` tensor. Quantize/dequantize occur at the
method boundary; all five dense matrices are intended for Ethos-U85. Methods
are ordered by state dependency, not by their directory order.

| Method | N | M | Concatenated input, in this exact order |
| --- | ---: | ---: | --- |
| `lstm1` | 476 | 1600 | old window w[73], previous stroke [dx,dy,pen_up], old h1[400] |
| `attention` | 476 | 30 | old window w[73], previous stroke [3], **new h1[400]** |
| `lstm2` | 876 | 1600 | previous stroke [3], **new h1[400]**, **new w[73]**, old h2[400] |
| `lstm3` | 876 | 1600 | previous stroke [3], **new h2[400]**, **new w[73]**, old h3[400] |
| `gmm` | 400 | 121 | **new h3[400]** |

Use `model_test_data.h` for independent dense-method self-tests. It contains
`handwriting_<method>_input` and `handwriting_<method>_expected`, reference
float outputs of the saved a16w8 graph on actual held-out trajectory inputs.
`HANDWRITING_<METHOD>_OUTPUT_SCALE` gives one output LSB.
`HANDWRITING_<METHOD>_BOARD_ATOL` permits four output LSBs for Vela's fixed-point
requantization vs host fake quantization; it is not a float-model accuracy bound.
On-board mismatches beyond this bound require investigation.

## Recurrent state and update order

Persist h1,c1,h2,c2,h3,c3, each 400 float32 values; kappa[10]; window w[73].
Reset all to zero at the beginning of every sentence. Alpha, beta and phi are
scratch. Initial previous stroke is `[0,0,1]`.

After each LSTM method, split gates into contiguous i,j,f,o blocks of 400.
`c_new = sigmoid(f+1)*c_old + sigmoid(i)*tanh(j)`;
`h_new = tanh(c_new)*sigmoid(o)`. The `+1` forget bias is essential.

After attention, apply stable softplus `max(z,0)+log1p(exp(-abs(z)))` to all 30
values. Split into alpha[10], beta[10], delta[10].
`kappa_new = kappa_old + delta/25`; `beta=max(beta,0.01)`.
For character position u,
`phi[u] = sum_j alpha[j]*exp(-(kappa[j]-u)^2/beta[j])`.
`w[c] = sum_(u where encoded[u]==c) phi[u]`. The last encoded character is a null
terminator with alphabet index 0 and participates in this sum. No one-hot matrix
is needed on the MCU. Positions beyond the encoded sequence are masked out.

## Decoder, sampling and termination

GMM121 order is **pi logits[20], log_sigma_x[20], log_sigma_y[20], rho logits[20],
mu_x[20], mu_y[20], pen logit[1]**. For a sampling bias b:

- pi=softmax(pi_logits*(1+b)), zero pi values below 0.01, then renormalize.
- sigma=exp(log_sigma-b), lower bound 0.0001.
- rho=tanh(rho_logits), clip to (-1+1e-8,1-1e-8).
- pen_probability=sigmoid(pen_logit), clip to (1e-8,1-1e-8), set to0 if below 0.01.
- Select k categorically from pi and independent standard normals z1,z2.
  dx=mu_x[k]+sigma_x[k]*z1;
  dy=mu_y[k]+sigma_y[k]*(rho[k]*z1+sqrt(1-rho[k]^2)*z2).
  Draw pen_up from the Bernoulli pen_probability. Feed all three back next step.

The interactive demo uses b=1.0 by default and accepts `:bias 0..2` and
`:seed 1..4294967295` for the next submitted sentence. Both values travel in
the immutable sentence snapshot. Engine::begin and historical calibration,
conversion fixtures and benchmarks retain b=0.5 unless explicitly overridden.
Changing bias is a CPU sampling operation and requires no new PTE.

The source checks termination with an **independent Bernoulli pen draw** when
argmax(phi) reaches the null terminator; stop before emitting a final point.
The other upstream branch, argmax(phi)>=encoded_length, cannot occur when phi
has exactly encoded_length entries. The upstream bound is **40*sentence length**
generated steps, up to 3000 for 75 input characters. The 1200 array in demo.py is a
style-priming buffer, not a maximum generated-point count. Ensure storage bounds
and report truncation if the app imposes a smaller cap.

Stochastic generation uses the same probability laws; the NumPy and TensorFlow
random-number streams do not produce identical sample sequences. Finite-step
forced-input comparisons are the suitable numerical equivalence check.

## Alphabet and rendering

Alphabet 73, in exact index order (the first is NUL):

```text
\0 !"#'(),-.0123456789:;?ABCDEFGHIJKLMNOPRSTUVWYabcdefghijklmnopqrstuvwxyz
```

At most 75 supported input characters, followed by NUL. Q,X,Z are absent in
uppercase, as are many punctuation and non-ASCII characters. Firmware should
normalize unsupported uppercase letters to lowercase where possible and reject
or visibly report other unsupported input, rather than silently changing text.
Buffer UART input and start on Return; reset recurrent state, clear the previous
drawing and animate **white** pen strokes on a **black** 800x480 landscape LCD.
A point's pen_up flag controls whether the **following** point starts a new
stroke. Plot cumsum(dx,dy); invert y for LCD coordinates. The source SVG uses
optional alignment/denoising. The LCD uses fractional endpoints and a round
antialiased 1.8-pixel pen; gray RGB565 edge coverage is combined with max
coverage at joins. The renderer does not change the stroke fed back to the
model. Finishing punctuation may use the soft right margin; the hard viewport
bound remains. Raw model paths remain valid handwriting.

## Benchmark units

Report five separate dense invocation times and the full recurrent step:
**lstm1 + CPU gates + attention + CPU window + lstm2 + CPU gates + lstm3 + CPU
gates + gmm + CPU mixture sampling/termination**. Sum of NPU calls is not the full
step, and a complete sentence contains hundreds or thousands of steps. Timing
must use a Release board build, real OSPI weights and hardware NPU execution.
