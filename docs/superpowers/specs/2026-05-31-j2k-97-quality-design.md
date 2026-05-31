# JPEG 2000 9/7 Quality Mode Design

## Context

The current `code/lib/j2k` implementation writes and reads a constrained JPEG 2000 Part 1 reversible profile:

- 8-bit gray/RGB images.
- No component subsampling.
- LRCP progression.
- Maximum precinct signalling.
- SOP/EPH packet markers.
- Reversible 5/3 DWT.
- RCT for RGB.
- Reversible QCD/QCC syntax.
- EBCOT/MQ packetized code-block data.

ROI Maxshift currently exists only as an encoder-side reversible path. It builds a coefficient-domain shift map and shifts selected coefficient magnitudes before EBCOT. The decoder does not restore ROI-scaled coefficients as a full lossless ROI profile. This design does not extend ROI.

## Goal

Add a standard-compatible minimum JPEG 2000 irreversible path: 9/7 DWT plus scalar quantization controlled by `Q=1..100`, while preserving the current lossless path with `Q=-1`.

## Scope

This first phase includes:

- Add 9/7 irreversible DWT under `code/lib/wavelet`.
- Add ICT for RGB irreversible coding.
- Extend existing J2K/JP2 image write APIs with a `quality` parameter.
- Use `quality == -1` for the existing 5/3 reversible lossless path.
- Use `quality` in `[1, 100]` for the new 9/7 irreversible lossy path.
- Write standard COD/QCD/QCC marker syntax for irreversible coding.
- Decode the project-written irreversible subset from raw J2K and JP2.
- Update final project CLI encode commands to accept optional `Q`.
- Add focused unit tests and smoke tests.

This first phase excludes:

- ROI on the 9/7 irreversible path.
- Non-maximum precinct codestream output.
- POC or progression orders other than LRCP.
- PPM/PPT/PLM/PLT/TLM markers.
- Arbitrary bit depth, signed components, or component subsampling.
- Full external JPEG 2000 conformance beyond this subset.

## Public API

Reuse the existing encode entry points by adding a quality parameter:

```c
dic_status j2k_write_image_codestream(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
);

dic_status j2k_write_image_jp2(
    const char *path,
    const dic_image_u8 *image,
    int requested_levels,
    int quality
);
```

Quality semantics:

- `quality == -1`: existing reversible lossless path.
- `1 <= quality <= 100`: irreversible lossy path.
- Any other value returns `DIC_STATUS_INVALID_ARGUMENT`.

The ROI and tiled functions remain reversible-only in this phase:

```c
j2k_write_image_codestream_roi(...)
j2k_write_image_jp2_roi(...)
j2k_write_image_codestream_tiled(...)
j2k_write_image_jp2_tiled(...)
```

They keep their existing signatures and behavior.

## CLI

Update final project commands:

```text
finalproj j2k-encode <input.pgm|input.ppm> <output.j2k> [Q]
finalproj jp2-encode <input.pgm|input.ppm> <output.jp2> [Q]
```

CLI behavior:

- If `Q` is omitted, pass `quality = -1` and keep current lossless output.
- If `Q` is present, require `1..100`.
- Invalid `Q` rejects the command.

The CLI does not need to expose `-1`; omission is the user-facing lossless mode.

## Architecture

### Wavelet Layer

Create:

- `code/lib/wavelet/dic_dwt97.h`
- `code/lib/wavelet/dic_dwt97.c`

Responsibilities:

- Implement Annex F irreversible 9/7 lifting on `double` planes.
- Provide forward and inverse plane functions.
- Reuse `dic_dwt53_low_size`, `dic_dwt53_high_size`, and `dic_dwt53_validate_levels` for the shared dyadic decomposition geometry.
- Validate decomposition levels consistently with `dic_dwt53_validate_levels`.

Planned public functions:

```c
dic_status dic_dwt97_forward_plane(
    double *plane,
    int width,
    int height,
    int levels
);

dic_status dic_dwt97_inverse_plane(
    double *plane,
    int width,
    int height,
    int levels
);
```

### Component Transform

Create:

- `code/lib/j2k/j2k_ict.h`
- `code/lib/j2k/j2k_ict.c`

Responsibilities:

- Implement forward ICT for RGB before 9/7 DWT.
- Implement inverse ICT after inverse 9/7 DWT.
- Leave RCT unchanged for the 5/3 reversible path.

Mode split:

- `quality == -1`: level shift -> RCT -> 5/3.
- `quality in 1..100`: level shift -> ICT -> 9/7.

### Quantization

Create:

- `code/lib/j2k/j2k_quant.h`
- `code/lib/j2k/j2k_quant.c`

Responsibilities:

- Convert `Q=1..100` to subband quantization step sizes.
- Encode the steps as JPEG 2000 irreversible SPqcd/SPqcc exponent/mantissa values.
- Quantize transformed coefficients into signed integer code-block coefficients for the existing EBCOT path.
- Decode QCD/QCC and dequantize integer coefficients back to double coefficients before inverse 9/7.

The decoder must not depend on the original `Q`. It reads only COD/QCD/QCC marker data.

Initial Q mapping:

```text
base_step = 2 ^ ((50 - Q) / 16)
```

Then apply subband-dependent scaling through the standard irreversible quantization step representation. The exact mantissa/exponent conversion lives in the quantization helper and is tested directly.

### Codestream Syntax

Extend `j2k_basic_params` and marker writer/parser state enough to represent:

- Transform type: reversible 5/3 vs irreversible 9/7.
- Quantization style: reversible vs irreversible.
- Per-subband quantization step metadata from QCD/QCC.

Marker behavior:

- `quality == -1`: keep current reversible COD transform value and reversible QCD/QCC syntax.
- `quality in 1..100`: write COD transform value for 9/7 irreversible and write irreversible QCD/QCC step sizes.

### Image Encoder

Refactor `j2k_image_encode_payload` into an internal mode-aware encoder:

- Keep the existing reversible path behavior unchanged.
- Add an irreversible path that:
  - Converts samples to double component planes.
  - Applies level shift.
  - Applies ICT for RGB.
  - Runs 9/7 DWT.
  - Quantizes each subband to integer coefficients.
  - Reuses existing EBCOT packetization.
  - Writes irreversible marker parameters.

The file should remain readable. If `j2k_image.c` grows too large, split helpers into focused files in `code/lib/j2k`.

### Image Decoder

Extend `j2k_decode.c` to choose reconstruction path from COD/QCD:

- Reversible path remains the current integer 5/3 path.
- Irreversible path:
  - Parses irreversible QCD/QCC.
  - Decodes EBCOT quantized integer coefficients.
  - Dequantizes subbands to double planes.
  - Runs inverse 9/7.
  - Applies inverse ICT for RGB.
  - Applies inverse level shift and clamps to 8-bit output.

Unsupported combinations fail loudly with existing or new `DIC_J2K_*` status codes.

## Error Handling

Validation rules:

- `quality` must be `-1` or `[1, 100]`.
- Irreversible path supports only 1 or 3 components.
- Irreversible RGB uses ICT only.
- ROI functions do not accept a lossy option in this phase.
- Decoder rejects unsupported transform, quantization style, bit depth, subsampling, or missing marker data.

No silent clamping is allowed except final pixel reconstruction to unsigned 8-bit, which already exists as output-domain saturation.

## Testing

Add or update these tests:

- `lib_wavelet_test_dwt97`: verify 9/7 forward+inverse approximately reconstructs small planes.
- `lib_j2k_test_codestream`: verify COD transform value and irreversible QCD syntax for lossy mode.
- `lib_j2k_test_parse`: verify irreversible marker metadata is parsed.
- `lib_j2k_test_image`: verify:
  - `Q=-1` remains bit-exact with current lossless tests.
  - `Q=90` and `Q=50` encode/decode gray and RGB images.
  - Decoded dimensions and channels match input.
  - `Q=90` PSNR is higher than `Q=50`.
  - Invalid `Q` values fail.
- final project CLI smoke:
  - `jp2-encode input.ppm output.jp2` remains lossless mode.
  - `jp2-encode input.ppm output.jp2 75` produces a decodable lossy JP2.

Run:

```text
cmake --build build --target project_tests
ctest --test-dir build --output-on-failure
```

## Documentation

Update `report.html` after implementation to document:

- `Q=-1` vs `Q=1..100`.
- 5/3/RCT/reversible path.
- 9/7/ICT/irreversible path.
- ROI remains reversible-only in this phase.

## Success Criteria

- Existing reversible tests pass.
- New 9/7 wavelet tests pass.
- New lossy J2K/JP2 encode/decode tests pass.
- CLI accepts optional `Q` for `j2k-encode` and `jp2-encode`.
- `Q=90` produces better PSNR than `Q=50` on the test fixture.
- `ctest --test-dir build --output-on-failure` passes.
