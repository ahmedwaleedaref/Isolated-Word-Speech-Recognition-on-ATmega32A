import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 8000
FRAMES_PER_SAMPLE = 32                             # 32 blocks → 31 overlap windows
FRAME_SIZE = 128                                   # must be power-of-2 for MCU >>7 shift
TOTAL_SAMPLES = FRAMES_PER_SAMPLE * FRAME_SIZE     # 4096
OVERLAP_WINDOWS = FRAMES_PER_SAMPLE - 1            # 31

# Must match MCU macros
STE_SILENCE_THRESHOLD = 50
MIN_FRAMES_BEFORE_STOP = 10

# 5 formant-aligned Goertzel bins — must match goertzel.h exactly
# coeff = round(2 * cos(2*pi*f/8000) * 16384)  [Q14]
GOERTZEL_NUM_BINS = 5
GOERTZEL_FREQS_HZ = [350, 900, 1700, 2700, 3500]
GOERTZEL_COEFFS_Q14 = [31539, 25093, 7852, -16854, -30274]
FRICATIVE_GOERTZEL_THRESHOLD = 2  # VAD silence check (feature-scale units)

# Per-band right-shift for the TWO-FRAME pseudo-magnitude sum → uint8.
# Must match GOERTZEL_SHIFTS[] in goertzel.c.
# Low-freq bands have more energy → larger shift.
# Tune: if a band is always 0, lower its shift by 2-3.
#        If always 255, raise by 2-3.
GOERTZEL_SHIFTS = [10, 8, 7, 6, 5]

# Feature layout: [STE×31 | ZCE×31 | G350×31 | G900×31 | G1700×31 | G2700×31 | G3500×31] = 217


def _normalize_length(signal: np.ndarray, target_samples: int = TOTAL_SAMPLES) -> np.ndarray:
    if signal.size < target_samples:
        return np.pad(signal, (0, target_samples - signal.size))
    if signal.size > target_samples:
        return signal[:target_samples]
    return signal


def _goertzel_frame_pseudo_magnitude(frame: np.ndarray) -> list[int]:
    """
    Goertzel pseudo-magnitude for all 5 bins using Q14 fixed-point
    (mirrors MCU goertzel.c).
    Returns raw uint16-range values (before per-band shift scaling).
    """
    s1 = [0] * GOERTZEL_NUM_BINS
    s2 = [0] * GOERTZEL_NUM_BINS
    for sample in frame:
        x = int(sample)
        for b in range(GOERTZEL_NUM_BINS):
            s0 = x + ((GOERTZEL_COEFFS_Q14[b] * s1[b]) >> 14) - s2[b]
            s2[b] = s1[b]
            s1[b] = s0
    magnitudes = []
    for b in range(GOERTZEL_NUM_BINS):
        a = abs(s1[b])
        c = abs(s2[b])
        mx = max(a, c)
        mn = min(a, c)
        mag = mx + (mn >> 1)  # alpha-max-plus-beta-min, beta≈0.5
        magnitudes.append(min(np.iinfo(np.uint16).max, mag))
    return magnitudes


def extract_feature_vector(signal: np.ndarray) -> np.ndarray:
    """
    217 features from a centered int16 signal.
    Early stop: 12 consecutive silent frames after minimum 10 recorded.
    Silence = STE AND Goertzel (VAD) both below threshold.

    Feature layout: [STE×31 | ZCE×31 | G350×31 | G900×31 | G1700×31 | G2700×31 | G3500×31]
    Per-band scaling: (curr_mag + prev_mag) >> GOERTZEL_SHIFTS[b], clipped to uint8.
    Global normalization applied per-band at utterance level for volume invariance.
    """
    signal = _normalize_length(signal, TOTAL_SAMPLES)
    signal = np.round(signal).astype(np.int16)
    frames = signal.reshape(FRAMES_PER_SAMPLE, FRAME_SIZE)

    ste_array = np.zeros(OVERLAP_WINDOWS, dtype=np.uint8)
    zce_array = np.zeros(OVERLAP_WINDOWS, dtype=np.uint8)
    goertzel_raw = np.zeros((GOERTZEL_NUM_BINS, OVERLAP_WINDOWS), dtype=np.uint8)

    prev_ste = None
    prev_zce = None
    prev_goertzel = None
    consecutive_silent = 0

    for i in range(FRAMES_PER_SAMPLE):
        frame = frames[i]

        curr_ste = int(np.sum(np.abs(frame)))
        binary_signs = (frame > 0).astype(np.int8)
        curr_zce = int(np.sum(np.diff(binary_signs) != 0))
        curr_goertzel = _goertzel_frame_pseudo_magnitude(frame)

        if prev_ste is not None:
            # (sum_curr + sum_prev) >> 8  matches MCU: two 128-sample sums, divide by 256
            ste_array[i - 1] = min(255, (curr_ste + prev_ste) >> 8)
            zce_array[i - 1] = curr_zce + prev_zce

            # Per-band scaling: (sum of two frames) >> GOERTZEL_SHIFTS[b]
            for b in range(GOERTZEL_NUM_BINS):
                overlap = (curr_goertzel[b] + prev_goertzel[b]) >> GOERTZEL_SHIFTS[b]
                goertzel_raw[b, i - 1] = min(255, overlap)

            ste_silent = (curr_ste >> 7) <= STE_SILENCE_THRESHOLD
            goertzel_vad = max(
                (curr_goertzel[b] >> GOERTZEL_SHIFTS[b]) for b in range(GOERTZEL_NUM_BINS)
            )
            goertzel_silent = goertzel_vad <= FRICATIVE_GOERTZEL_THRESHOLD
            if ste_silent and goertzel_silent:
                consecutive_silent += 1
            else:
                consecutive_silent = 0

            if i >= MIN_FRAMES_BEFORE_STOP and consecutive_silent >= 12:
                break

        prev_ste = curr_ste
        prev_zce = curr_zce
        prev_goertzel = curr_goertzel

    # Global normalization over STE: handles utterance-level volume
    ste_peak = int(np.max(ste_array))
    if ste_peak > 0:
        ste_array = np.round(ste_array.astype(np.float32) * 255.0 / ste_peak).astype(np.uint8)

    # Global normalization over ALL Goertzel bands together:
    # preserves spectral shape (inter-band ratios) while normalizing volume.
    # This works correctly because per-band shifts already equalize the bands' scales.
    g_peak = int(np.max(goertzel_raw))
    if g_peak > 0:
        goertzel_raw = np.round(
            goertzel_raw.astype(np.float32) * 255.0 / g_peak
        ).astype(np.uint8)

    return np.concatenate([ste_array, zce_array, goertzel_raw.flatten()]).astype(np.float32)


def extract_feature_vector_from_file(file_path: str | Path) -> np.ndarray:
    with wave.open(str(file_path), 'rb') as wf:
        raw = wf.readframes(wf.getnframes())
    centered = np.frombuffer(raw, dtype='<i2').astype(np.int16)
    return extract_feature_vector(centered)


if __name__ == "__main__":
    filename = Path(__file__).resolve().parents[2] / "data" / "on-recordings"
    first_file = sorted(filename.glob("*.wav"))[0]

    features = extract_feature_vector_from_file(first_file)
    ste = features[:OVERLAP_WINDOWS]
    zce = features[OVERLAP_WINDOWS:2 * OVERLAP_WINDOWS]
    goertzel = features[2 * OVERLAP_WINDOWS:].reshape(GOERTZEL_NUM_BINS, OVERLAP_WINDOWS)

    print(f"File: {first_file.name}")
    print(f"Feature vector length: {len(features)}  (expected 217)")
    print("First 5 STE:", ste[:5])
    print("First 5 ZCE:", zce[:5])
    for b in range(GOERTZEL_NUM_BINS):
        print(f"First 5 G{GOERTZEL_FREQS_HZ[b]}:", goertzel[b, :5])
