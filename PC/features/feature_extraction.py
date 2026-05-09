import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 8000
TOTAL_SAMPLES = 4000
FRAMES_PER_SAMPLE = 32
FRAME_SIZE = TOTAL_SAMPLES // FRAMES_PER_SAMPLE  # 125
OVERLAP_WINDOWS = FRAMES_PER_SAMPLE - 1  # 61


def _normalize_length(signal: np.ndarray, target_samples: int = TOTAL_SAMPLES) -> np.ndarray:
    if signal.size < target_samples:
        return np.pad(signal, (0, target_samples - signal.size))
    if signal.size > target_samples:
        return signal[:target_samples]
    return signal


def extract_ste_zce_integer(signal: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    # 1. Ensure signal is centered integers [-256, 256]
    signal = _normalize_length(signal, TOTAL_SAMPLES)
    signal = np.round(signal).astype(np.int16)
    
    frames = signal.reshape(FRAMES_PER_SAMPLE, FRAME_SIZE)

    # We use uint16 for memory efficiency on MCU
    ste_array = np.zeros(OVERLAP_WINDOWS, dtype=np.uint8)
    zce_array = np.zeros(OVERLAP_WINDOWS, dtype=np.uint8)

    # To calculate STE on MCU without 32-bit overflow:
    # We sum (sample^2) and bit-shift right to fit in 16 bits.
    def calculate_mcu_frame_ste(frame):
        # frame is 125 samples. Max sum = 125 * 256^2 = 8,192,000
        # To fit in 16 bits (max 65535), we must divide by at least 125
        # Dividing by 128 (shift >> 7) is faster for MCU
        return np.sum(np.abs(frame))

    prev_ste = None
    prev_zce_count = None

    for i in range(FRAMES_PER_SAMPLE):
        # Integer STE (Scaled down)
        current_ste = calculate_mcu_frame_ste(frames[i])
        
        # same sign convention as MCU: (centered > 0) ? 1 : 0  — zero is negative side
        binary_signs = (frames[i] > 0).astype(np.int8)
        current_zce_count = np.sum(np.diff(binary_signs) != 0)

        if prev_ste is not None:
            ste_array[i - 1] = (current_ste + prev_ste) >> 8

            # MCU just adds the two frame counts, no boundary check
            zce_array[i - 1] = current_zce_count + prev_zce_count

        prev_ste = current_ste
        prev_zce_count = current_zce_count

    # Normalize STE by peak — matches MCU normalization before classification
    ste_peak = int(np.max(ste_array))
    if ste_peak > 0:
        ste_array = np.round(ste_array.astype(np.float32) * 255.0 / ste_peak).astype(np.uint8)

    return ste_array, zce_array


def extract_feature_vector_from_file(file_path: str | Path) -> np.ndarray:
    with wave.open(str(file_path), 'rb') as wf:
        raw = wf.readframes(wf.getnframes())
    # raw ADC values stored as little-endian int16 (0–1023), subtract 256 same as MCU
    samples = np.frombuffer(raw, dtype='<i2').astype(np.int16)
    centered = (samples.astype(np.int32) - 256).astype(np.int16)
    ste, zce = extract_ste_zce_integer(centered)
    return np.concatenate([ste, zce]).astype(np.float32)


if __name__ == "__main__":
    filename = Path(__file__).resolve().parents[2] / "data" / "on-recordings"
    first_file = sorted(filename.glob("*.wav"))[0]

    features = extract_feature_vector_from_file(first_file)
    ste = features[:OVERLAP_WINDOWS]
    zce = features[OVERLAP_WINDOWS:]

    print(f"File: {first_file.name}")
    print(f"STE shape: {ste.shape}, ZCE shape: {zce.shape}")
    print("First 5 STE values:", ste[:5])
    print("First 5 ZCE values:", zce[:5])
