import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 8000
FRAME_SIZE = 128                                   # must be power-of-2 for MCU >>7 shift
MAX_RECORD_SAMPLES = 7000
MAX_RECORD_BLOCKS = MAX_RECORD_SAMPLES // FRAME_SIZE  # 54 blocks = 6912 samples
MAX_FEATURE_FRAMES = MAX_RECORD_BLOCKS - 1            # overlap features

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

# Feature layout (dynamic):
# [STE×L | ZCE×L | G350×L | G900×L | G1700×L | G2700×L | G3500×L], 0 <= L <= MAX_FEATURE_FRAMES


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


def _prepare_frames(signal: np.ndarray) -> np.ndarray:
    signal = np.round(signal).astype(np.int16)
    if signal.size > MAX_RECORD_SAMPLES:
        signal = signal[:MAX_RECORD_SAMPLES]

    usable_samples = (signal.size // FRAME_SIZE) * FRAME_SIZE
    if usable_samples == 0:
        return np.zeros((0, FRAME_SIZE), dtype=np.int16)

    frames = signal[:usable_samples].reshape(-1, FRAME_SIZE)
    if frames.shape[0] > MAX_RECORD_BLOCKS:
        frames = frames[:MAX_RECORD_BLOCKS]
    return frames


def extract_feature_channels(signal: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Dynamic feature extraction mirroring MCU logic.
    Returns (STE[L], ZCE[L], G[5][L]) where 0 <= L <= MAX_FEATURE_FRAMES.
    """
    frames = _prepare_frames(signal)

    ste_vals: list[int] = []
    zce_vals: list[int] = []
    goertzel_vals: list[list[int]] = [[] for _ in range(GOERTZEL_NUM_BINS)]

    prev_ste = None
    prev_zce = None
    prev_goertzel = None
    consecutive_silent = 0
    feature_count = 0

    for frame in frames:
        curr_ste = int(np.sum(np.abs(frame)))
        binary_signs = (frame > 0).astype(np.int8)
        curr_zce = int(np.sum(np.diff(binary_signs) != 0))
        curr_goertzel = _goertzel_frame_pseudo_magnitude(frame)

        if prev_ste is not None:
            ste_vals.append(min(255, (curr_ste + prev_ste) >> 8))
            zce_vals.append((curr_zce + prev_zce) & 0xFF)

            for b in range(GOERTZEL_NUM_BINS):
                overlap = (curr_goertzel[b] + prev_goertzel[b]) >> GOERTZEL_SHIFTS[b]
                goertzel_vals[b].append(min(255, overlap))

            ste_silent = (curr_ste >> 7) <= STE_SILENCE_THRESHOLD
            goertzel_vad = max(
                (curr_goertzel[b] >> GOERTZEL_SHIFTS[b]) for b in range(GOERTZEL_NUM_BINS)
            )
            goertzel_silent = goertzel_vad <= FRICATIVE_GOERTZEL_THRESHOLD
            if ste_silent and goertzel_silent:
                consecutive_silent += 1
            else:
                consecutive_silent = 0

            feature_count += 1
            if (feature_count >= MIN_FRAMES_BEFORE_STOP and consecutive_silent >= 12) or (
                feature_count >= MAX_FEATURE_FRAMES
            ):
                break

        prev_ste = curr_ste
        prev_zce = curr_zce
        prev_goertzel = curr_goertzel

    ste_array = np.asarray(ste_vals, dtype=np.uint8)
    zce_array = np.asarray(zce_vals, dtype=np.uint8)
    goertzel_array = np.asarray(goertzel_vals, dtype=np.uint8)

    if ste_array.size > 0:
        ste_peak = int(np.max(ste_array))
        if ste_peak > 0:
            ste_array = np.round(ste_array.astype(np.float32) * 255.0 / ste_peak).astype(np.uint8)

    if goertzel_array.size > 0:
        g_peak = int(np.max(goertzel_array))
        if g_peak > 0:
            goertzel_array = np.round(
                goertzel_array.astype(np.float32) * 255.0 / g_peak
            ).astype(np.uint8)

    return ste_array, zce_array, goertzel_array


def flatten_feature_channels(
    ste: np.ndarray, zce: np.ndarray, goertzel: np.ndarray
) -> tuple[np.ndarray, int]:
    feature_len = int(ste.size)
    flat_size = MAX_FEATURE_FRAMES * (2 + GOERTZEL_NUM_BINS)
    flat = np.zeros(flat_size, dtype=np.float32)

    if feature_len == 0:
        return flat, 0

    flat[0:feature_len] = ste.astype(np.float32)
    flat[MAX_FEATURE_FRAMES:MAX_FEATURE_FRAMES + feature_len] = zce.astype(np.float32)

    base = 2 * MAX_FEATURE_FRAMES
    for b in range(GOERTZEL_NUM_BINS):
        start = base + b * MAX_FEATURE_FRAMES
        flat[start:start + feature_len] = goertzel[b].astype(np.float32)

    return flat, feature_len


def extract_feature_vector(signal: np.ndarray) -> np.ndarray:
    ste, zce, goertzel = extract_feature_channels(signal)
    flat, _ = flatten_feature_channels(ste, zce, goertzel)
    return flat


def extract_feature_vector_with_length(signal: np.ndarray) -> tuple[np.ndarray, int]:
    ste, zce, goertzel = extract_feature_channels(signal)
    return flatten_feature_channels(ste, zce, goertzel)


def extract_feature_vector_from_file(file_path: str | Path) -> np.ndarray:
    with wave.open(str(file_path), 'rb') as wf:
        raw = wf.readframes(wf.getnframes())
    centered = np.frombuffer(raw, dtype='<i2').astype(np.int16)
    return extract_feature_vector(centered)


def extract_feature_vector_from_file_with_length(file_path: str | Path) -> tuple[np.ndarray, int]:
    with wave.open(str(file_path), 'rb') as wf:
        raw = wf.readframes(wf.getnframes())
    centered = np.frombuffer(raw, dtype='<i2').astype(np.int16)
    return extract_feature_vector_with_length(centered)


if __name__ == "__main__":
    filename = Path(__file__).resolve().parents[2] / "data" / "on-recordings"
    first_file = sorted(filename.glob("*.wav"))[0]

    features, feature_len = extract_feature_vector_from_file_with_length(first_file)
    ste = features[:MAX_FEATURE_FRAMES][:feature_len]
    zce = features[MAX_FEATURE_FRAMES:2 * MAX_FEATURE_FRAMES][:feature_len]
    goertzel = features[2 * MAX_FEATURE_FRAMES:].reshape(GOERTZEL_NUM_BINS, MAX_FEATURE_FRAMES)[:, :feature_len]

    print(f"File: {first_file.name}")
    print(f"Flattened feature vector length: {len(features)}")
    print(f"Dynamic frame count: {feature_len}")
    print("First 5 STE:", ste[:5])
    print("First 5 ZCE:", zce[:5])
    for b in range(GOERTZEL_NUM_BINS):
        print(f"First 5 G{GOERTZEL_FREQS_HZ[b]}:", goertzel[b, :5])
