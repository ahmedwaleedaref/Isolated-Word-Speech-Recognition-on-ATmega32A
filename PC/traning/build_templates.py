import csv
from pathlib import Path
import sys

import numpy as np

PROJECT_PC_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = PROJECT_PC_ROOT.parent
DEFAULT_DATA_DIR = PROJECT_ROOT / "data"

if str(PROJECT_PC_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_PC_ROOT))

from features.feature_extraction import (
    GOERTZEL_FREQS_HZ,
    GOERTZEL_NUM_BINS,
    MAX_FEATURE_FRAMES,
    extract_feature_vector_from_file_with_length,
)

WORD_FOLDERS = {
    "on": "ON",
    "off": "OFF",
    "close": "CLOSE",
    "stop": "STOP",
    "up": "UP",
    "down": "DOWN",
    "left": "LEFT",
    "right": "RIGHT",
}


def _list_wav_files(folder_path: Path) -> list[Path]:
    return sorted(path for path in folder_path.glob("*.wav") if path.is_file())


def build_dataset(data_dir: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str]]:
    all_features: list[np.ndarray] = []
    all_lengths: list[int] = []
    all_labels: list[str] = []
    all_files: list[str] = []

    for folder_name, label in WORD_FOLDERS.items():
        folder_path = data_dir / folder_name
        if not folder_path.exists():
            raise FileNotFoundError(f"Missing folder: {folder_path}")

        wav_files = _list_wav_files(folder_path)
        if not wav_files:
            raise RuntimeError(f"No .wav samples found in {folder_path}")

        for wav_file in wav_files:
            feature_vector, feature_len = extract_feature_vector_from_file_with_length(wav_file)
            all_features.append(feature_vector)
            all_lengths.append(feature_len)
            all_labels.append(label)
            all_files.append(str(wav_file))

    if not all_features:
        raise RuntimeError(f"No .wav files found under {data_dir}")

    return (
        np.vstack(all_features),
        np.array(all_labels),
        np.array(all_lengths, dtype=np.int32),
        all_files,
    )


def save_csv(features: np.ndarray, labels: np.ndarray, lengths: np.ndarray, output_csv: Path) -> None:
    ste_headers = [f"STE_{i}" for i in range(MAX_FEATURE_FRAMES)]
    zce_headers = [f"ZCE_{i}" for i in range(MAX_FEATURE_FRAMES)]
    goertzel_headers = [
        f"G{GOERTZEL_FREQS_HZ[b]}_{i}"
        for b in range(GOERTZEL_NUM_BINS)
        for i in range(MAX_FEATURE_FRAMES)
    ]
    headers = ["label", "frames", *ste_headers, *zce_headers, *goertzel_headers]

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    with output_csv.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(headers)

        for feature_vector, label, feature_len in zip(features, labels, lengths):
            writer.writerow([label, int(feature_len), *feature_vector.tolist()])


def main() -> None:
    output_dir = Path(__file__).resolve().parent / "output"
    output_dir.mkdir(parents=True, exist_ok=True)

    features, labels, lengths, files = build_dataset(DEFAULT_DATA_DIR)

    np.savez(
        output_dir / "word_features.npz",
        X=features,
        y=labels,
        lengths=lengths,
        files=np.array(files),
    )
    save_csv(features, labels, lengths, output_dir / "word_features.csv")

    print(f"Samples processed: {len(labels)}")
    print(
        f"Feature shape: {features.shape} "
        f"(max {MAX_FEATURE_FRAMES} STE + {MAX_FEATURE_FRAMES} ZCE + "
        f"{GOERTZEL_NUM_BINS}×{MAX_FEATURE_FRAMES} Goertzel = "
        f"{MAX_FEATURE_FRAMES * (2 + GOERTZEL_NUM_BINS)} flattened total)"
    )
    print(f"Saved: {output_dir / 'word_features.npz'}")
    print(f"Saved: {output_dir / 'word_features.csv'}")


if __name__ == "__main__":
    main()
