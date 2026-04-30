from pathlib import Path

import librosa
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
        
        # Integer ZCE: Strictly check signs
        # (val >= 0) is 1, (val < 0) is 0. Difference means a crossing.
        binary_signs = (frames[i] >= 0).astype(np.int8)
        current_zce_count = np.sum(np.diff(binary_signs) != 0)

        if prev_ste is not None:
            # Combine current and previous (matches your overlap logic)
            # Both are already scaled, sum fits in uint16
            ste_array[i - 1] = ( (current_ste + prev_ste) >> 8 )

            combined_count = current_zce_count + prev_zce_count
            
            # Boundary check between frames
            if (frames[i-1][-1] >= 0) != (frames[i][0] >= 0):
                combined_count += 1
            
            zce_array[i - 1] = combined_count # Raw count (0-250)

        prev_ste = current_ste
        prev_zce_count = current_zce_count

    return ste_array, zce_array


def extract_feature_vector_from_file(file_path: str | Path) -> np.ndarray:
    signal, _ = librosa.load(str(file_path), sr=SAMPLE_RATE, mono=True) 
    # 2. Scale to the 1.25V "swing" of your ADC (256 units)
    # This turns 1.0 into 256 and -1.0 into -256
    mcu_signal = signal * 256.0
    # 3. Quantization: The ADC cannot see decimals.
    # We round to simulate the 10-bit discrete levels.
    ste, zce = extract_ste_zce_integer(mcu_signal)
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
