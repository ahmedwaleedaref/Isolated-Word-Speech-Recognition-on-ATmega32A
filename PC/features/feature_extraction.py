from pathlib import Path

import librosa
import numpy as np

SAMPLE_RATE = 8000
TOTAL_SAMPLES = 8000
FRAMES_PER_SAMPLE = 64
FRAME_SIZE = TOTAL_SAMPLES // FRAMES_PER_SAMPLE  # 125
OVERLAP_WINDOWS = FRAMES_PER_SAMPLE - 1  # 63


def _normalize_length(signal: np.ndarray, target_samples: int = TOTAL_SAMPLES) -> np.ndarray:
    if signal.size < target_samples:
        return np.pad(signal, (0, target_samples - signal.size))
    if signal.size > target_samples:
        return signal[:target_samples]
    return signal


def extract_ste_zce(signal: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    signal = _normalize_length(signal, TOTAL_SAMPLES).astype(np.float32, copy=False)
    frames = signal.reshape(FRAMES_PER_SAMPLE, FRAME_SIZE)

    ste_array = np.zeros(OVERLAP_WINDOWS, dtype=np.float32)
    zce_array = np.zeros(OVERLAP_WINDOWS, dtype=np.float32)

    prev_ste = None
    prev_zce_count = None

    for i in range(FRAMES_PER_SAMPLE):
        current_ste = np.mean(frames[i] ** 2) * 0.5
        zero_crosses = np.where(np.diff(np.sign(frames[i])))[0]
        current_zce_count = len(zero_crosses)

        if prev_ste is not None:
            ste_array[i - 1] = current_ste + prev_ste

            combined_count = current_zce_count + prev_zce_count
            last_sample_prev = frames[i - 1][-1]
            first_sample_curr = frames[i][0]

            if np.sign(last_sample_prev) != np.sign(first_sample_curr) and np.sign(last_sample_prev) != 0:
                combined_count += 1

            zce_array[i - 1] = combined_count / 250.0

        prev_ste = current_ste
        prev_zce_count = current_zce_count

    return ste_array, zce_array


def extract_feature_vector_from_file(file_path: str | Path) -> np.ndarray:
    signal, _ = librosa.load(str(file_path), sr=SAMPLE_RATE, mono=True)
    ste, zce = extract_ste_zce(signal)
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
