import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 8000
TOTAL_SAMPLES = 4000
FRAMES_PER_SAMPLE = 32
FRAME_SIZE = TOTAL_SAMPLES // FRAMES_PER_SAMPLE  # 125
OVERLAP_WINDOWS = FRAMES_PER_SAMPLE - 1  # 31

# Must match MCU macros
STE_SILENCE_THRESHOLD = 100
MIN_FRAMES_BEFORE_STOP = 10

# Must match MCU goertzel.h — Goertzel bins at 8 kHz, N=125 samples
# k values: [6, 16, 31, 52] → frequencies: [384, 1024, 1984, 3328] Hz
GOERTZEL_NUM_BINS = 4
GOERTZEL_COEFFS_Q14 = [31289, 22730, 412, -28309]  # round(2*cos(2π*k/125)*16384)

# A frame is truly silent only if BOTH STE and Goertzel are below their thresholds.
# Mirrors MCU FRICATIVE_GOERTZEL_THRESHOLD (>>20 scale).
FRICATIVE_GOERTZEL_THRESHOLD = 1


def _normalize_length(signal: np.ndarray, target_samples: int = TOTAL_SAMPLES) -> np.ndarray:
    if signal.size < target_samples:
        return np.pad(signal, (0, target_samples - signal.size))
    if signal.size > target_samples:
        return signal[:target_samples]
    return signal


def _goertzel_frame_power(frame: np.ndarray) -> list[int]:
    """
    Compute Goertzel power for all bins over one frame using Q14 integer math
    (mirrors MCU goertzel.c exactly).
    Returns list of GOERTZEL_NUM_BINS int64-range values.
    """
    s1 = [0] * GOERTZEL_NUM_BINS
    s2 = [0] * GOERTZEL_NUM_BINS
    for sample in frame:
        x = int(sample)
        for b in range(GOERTZEL_NUM_BINS):
            s0 = x + ((GOERTZEL_COEFFS_Q14[b] * s1[b]) >> 14) - s2[b]
            s2[b] = s1[b]
            s1[b] = s0
    powers = []
    for b in range(GOERTZEL_NUM_BINS):
        cross = (GOERTZEL_COEFFS_Q14[b] * s1[b] * s2[b]) >> 14
        power = s1[b] * s1[b] + s2[b] * s2[b] - cross
        powers.append(max(0, power))
    return powers


def extract_feature_vector(signal: np.ndarray) -> np.ndarray:
    """
    Single-pass extraction of all 186 features from a centered int16 signal.
    All three feature groups (STE, ZCE, Goertzel) share the same early-stop frame,
    so they always cover the same acoustic window.

    Early stop: a frame is counted as silent only if BOTH STE and max Goertzel power
    (>>20) are below their thresholds — /f/ before /t/ has high Goertzel → not silent.

    Feature layout: [STE×31 | ZCE×31 | G0×31 | G1×31 | G2×31 | G3×31] = 186
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

        # STE for this frame (MCU: sum of |x|, not squared)
        curr_ste = int(np.sum(np.abs(frame)))

        # ZCE for this frame
        binary_signs = (frame > 0).astype(np.int8)
        curr_zce = int(np.sum(np.diff(binary_signs) != 0))

        # Goertzel power for this frame (all bins)
        curr_goertzel = _goertzel_frame_power(frame)
        goertzel_q20 = max(curr_goertzel) >> 20  # >>20 scale for silence check

        if prev_ste is not None:
            # Overlapping features (same as MCU)
            ste_array[i - 1] = (curr_ste + prev_ste) >> 8
            zce_array[i - 1] = curr_zce + prev_zce
            for b in range(GOERTZEL_NUM_BINS):
                overlap = (curr_goertzel[b] + prev_goertzel[b]) >> 22
                goertzel_raw[b, i - 1] = min(255, overlap)

            # Silence check: STE AND Goertzel both must be low
            ste_silent = (curr_ste >> 7) <= STE_SILENCE_THRESHOLD
            goertzel_silent = goertzel_q20 <= FRICATIVE_GOERTZEL_THRESHOLD
            if ste_silent and goertzel_silent:
                consecutive_silent += 1
            else:
                consecutive_silent = 0

            if i >= MIN_FRAMES_BEFORE_STOP and consecutive_silent >= 8:
                break

        prev_ste = curr_ste
        prev_zce = curr_zce
        prev_goertzel = curr_goertzel

    # Normalize STE by its peak (mirrors MCU DONE block)
    ste_peak = int(np.max(ste_array))
    if ste_peak > 0:
        ste_array = np.round(ste_array.astype(np.float32) * 255.0 / ste_peak).astype(np.uint8)

    # Per-bin Goertzel normalization (mirrors MCU DONE block)
    for b in range(GOERTZEL_NUM_BINS):
        g_peak = int(np.max(goertzel_raw[b]))
        if g_peak > 0:
            goertzel_raw[b] = np.round(
                goertzel_raw[b].astype(np.float32) * 255.0 / g_peak
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
    print(f"Feature vector length: {len(features)}  (expected 186)")
    print("First 5 STE:", ste[:5])
    print("First 5 ZCE:", zce[:5])
    for b in range(GOERTZEL_NUM_BINS):
        print(f"First 5 G{b}:", goertzel[b, :5])
