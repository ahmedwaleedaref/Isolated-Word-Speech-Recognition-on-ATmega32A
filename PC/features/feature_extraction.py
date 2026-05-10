import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 8000
TOTAL_SAMPLES = 4000
FRAMES_PER_SAMPLE = 32
FRAME_SIZE = TOTAL_SAMPLES // FRAMES_PER_SAMPLE  # 125
OVERLAP_WINDOWS = FRAMES_PER_SAMPLE - 1  # 31

# Must match MCU macros in main.c
STE_SILENCE_THRESHOLD = 100
ZCE_SILENCE_THRESHOLD = 15
MIN_FRAMES_BEFORE_STOP = 10

# Must match MCU goertzel.h — Goertzel bins at 8 kHz, N=125 samples
# k values: [6, 16, 31, 52] → frequencies: [384, 1024, 1984, 3328] Hz
GOERTZEL_NUM_BINS = 4
GOERTZEL_COEFFS_Q14 = [31289, 22730, 412, -28309]  # round(2*cos(2π*k/125)*16384)


def _normalize_length(signal: np.ndarray, target_samples: int = TOTAL_SAMPLES) -> np.ndarray:
    if signal.size < target_samples:
        return np.pad(signal, (0, target_samples - signal.size))
    if signal.size > target_samples:
        return signal[:target_samples]
    return signal


def _goertzel_frame_power(frame: np.ndarray) -> np.ndarray:
    """
    Compute Goertzel power for all GOERTZEL_NUM_BINS bins over one frame.
    Uses the same Q14 integer arithmetic as the MCU (mirrored in Python int).
    Returns array of shape (GOERTZEL_NUM_BINS,) with uint32-range values.
    """
    s1 = [0] * GOERTZEL_NUM_BINS
    s2 = [0] * GOERTZEL_NUM_BINS

    for sample in frame:
        x = int(sample)
        for b in range(GOERTZEL_NUM_BINS):
            s0 = x + ((GOERTZEL_COEFFS_Q14[b] * s1[b]) >> 14) - s2[b]
            s2[b] = s1[b]
            s1[b] = s0

    powers = np.zeros(GOERTZEL_NUM_BINS, dtype=np.int64)
    for b in range(GOERTZEL_NUM_BINS):
        cross = (GOERTZEL_COEFFS_Q14[b] * s1[b] * s2[b]) >> 14
        power = s1[b] * s1[b] + s2[b] * s2[b] - cross
        powers[b] = max(0, power)
    return powers


def extract_ste_zce_integer(signal: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    # 1. Ensure signal is centered integers [-256, 256]
    signal = _normalize_length(signal, TOTAL_SAMPLES)
    signal = np.round(signal).astype(np.int16)

    frames = signal.reshape(FRAMES_PER_SAMPLE, FRAME_SIZE)

    ste_array = np.zeros(OVERLAP_WINDOWS, dtype=np.uint8)
    zce_array = np.zeros(OVERLAP_WINDOWS, dtype=np.uint8)

    def calculate_mcu_frame_ste(frame):
        return np.sum(np.abs(frame))

    prev_ste = None
    prev_zce_count = None
    consecutive_silent = 0

    for i in range(FRAMES_PER_SAMPLE):
        current_ste = calculate_mcu_frame_ste(frames[i])
        binary_signs = (frames[i] > 0).astype(np.int8)
        current_zce_count = np.sum(np.diff(binary_signs) != 0)

        if prev_ste is not None:
            ste_array[i - 1] = (current_ste + prev_ste) >> 8
            zce_array[i - 1] = current_zce_count + prev_zce_count

            feature_idx = i
            if int(current_ste) >> 7 <= STE_SILENCE_THRESHOLD:
                consecutive_silent += 1
            else:
                consecutive_silent = 0

            if feature_idx >= MIN_FRAMES_BEFORE_STOP and consecutive_silent >= 8:
                break

        prev_ste = current_ste
        prev_zce_count = current_zce_count

    # Normalize STE by peak
    ste_peak = int(np.max(ste_array))
    if ste_peak > 0:
        ste_array = np.round(ste_array.astype(np.float32) * 255.0 / ste_peak).astype(np.uint8)

    return ste_array, zce_array


def extract_goertzel(signal: np.ndarray) -> np.ndarray:
    """
    Compute overlapping Goertzel features: shape (GOERTZEL_NUM_BINS, OVERLAP_WINDOWS).
    Mirrors MCU logic exactly: overlapping = (power[i] + power[i-1]) >> 22, then per-bin
    normalization by peak to 0–255.
    """
    signal = _normalize_length(signal, TOTAL_SAMPLES)
    signal = np.round(signal).astype(np.int16)

    frames = signal.reshape(FRAMES_PER_SAMPLE, FRAME_SIZE)

    goertzel_raw = np.zeros((GOERTZEL_NUM_BINS, OVERLAP_WINDOWS), dtype=np.uint8)

    prev_power = None
    for i in range(FRAMES_PER_SAMPLE):
        curr_power = _goertzel_frame_power(frames[i])
        if prev_power is not None:
            for b in range(GOERTZEL_NUM_BINS):
                overlap = int(curr_power[b] + prev_power[b]) >> 22
                goertzel_raw[b, i - 1] = min(255, overlap)
        prev_power = curr_power

    # Per-bin normalization by peak (mirrors MCU DONE block)
    for b in range(GOERTZEL_NUM_BINS):
        g_peak = int(np.max(goertzel_raw[b]))
        if g_peak > 0:
            goertzel_raw[b] = np.round(
                goertzel_raw[b].astype(np.float32) * 255.0 / g_peak
            ).astype(np.uint8)

    return goertzel_raw


def extract_feature_vector_from_file(file_path: str | Path) -> np.ndarray:
    with wave.open(str(file_path), 'rb') as wf:
        raw = wf.readframes(wf.getnframes())
    centered = np.frombuffer(raw, dtype='<i2').astype(np.int16)
    ste, zce = extract_ste_zce_integer(centered)
    goertzel = extract_goertzel(centered)  # shape (4, 31)
    # Layout: [STE×31 | ZCE×31 | G0×31 | G1×31 | G2×31 | G3×31] = 186
    return np.concatenate([ste, zce, goertzel.flatten()]).astype(np.float32)


if __name__ == "__main__":
    filename = Path(__file__).resolve().parents[2] / "data" / "on-recordings"
    first_file = sorted(filename.glob("*.wav"))[0]

    features = extract_feature_vector_from_file(first_file)
    ste = features[:OVERLAP_WINDOWS]
    zce = features[OVERLAP_WINDOWS:2 * OVERLAP_WINDOWS]
    goertzel = features[2 * OVERLAP_WINDOWS:].reshape(GOERTZEL_NUM_BINS, OVERLAP_WINDOWS)

    print(f"File: {first_file.name}")
    print(f"Feature vector length: {len(features)}")
    print(f"STE shape: {ste.shape}, ZCE shape: {zce.shape}, Goertzel shape: {goertzel.shape}")
    print("First 5 STE values:", ste[:5])
    print("First 5 ZCE values:", zce[:5])
    for b in range(GOERTZEL_NUM_BINS):
        print(f"First 5 G{b} values:", goertzel[b, :5])
