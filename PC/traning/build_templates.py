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
    OVERLAP_WINDOWS,
    extract_feature_vector_from_file,
)

WORD_FOLDERS = {
    "on": "ON",
    "off": "OFF",
    "start": "START",
    "stop": "STOP",
    "up": "UP",
    "down": "DOWN",
    "left": "LEFT",
    "right": "RIGHT",
}


def _list_wav_files(folder_path: Path) -> list[Path]:
    return sorted(path for path in folder_path.glob("*.wav") if path.is_file())


def build_dataset(data_dir: Path) -> tuple[np.ndarray, np.ndarray, list[str]]:
    all_features: list[np.ndarray] = []
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
            feature_vector = extract_feature_vector_from_file(wav_file)
            all_features.append(feature_vector)
            all_labels.append(label)
            all_files.append(str(wav_file))

    if not all_features:
        raise RuntimeError(f"No .wav files found under {data_dir}")

    return np.vstack(all_features), np.array(all_labels), all_files


def save_csv(features: np.ndarray, labels: np.ndarray, output_csv: Path) -> None:
    ste_headers = [f"STE_{i}" for i in range(OVERLAP_WINDOWS)]
    zce_headers = [f"ZCE_{i}" for i in range(OVERLAP_WINDOWS)]
    goertzel_headers = [
        f"G{GOERTZEL_FREQS_HZ[b]}_{i}"
        for b in range(GOERTZEL_NUM_BINS)
        for i in range(OVERLAP_WINDOWS)
    ]
    headers = ["label", *ste_headers, *zce_headers, *goertzel_headers]

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    with output_csv.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(headers)

        for feature_vector, label in zip(features, labels):
            writer.writerow([label, *feature_vector.tolist()])


def main() -> None:
    output_dir = Path(__file__).resolve().parent / "output"
    output_dir.mkdir(parents=True, exist_ok=True)

    features, labels, files = build_dataset(DEFAULT_DATA_DIR)

    np.savez(output_dir / "word_features.npz", X=features, y=labels, files=np.array(files))
    save_csv(features, labels, output_dir / "word_features.csv")

    print(f"Samples processed: {len(labels)}")
    print(
        f"Feature shape: {features.shape} "
        f"({OVERLAP_WINDOWS} STE + {OVERLAP_WINDOWS} ZCE + "
        f"{GOERTZEL_NUM_BINS}×{OVERLAP_WINDOWS} Goertzel = "
        f"{OVERLAP_WINDOWS * (2 + GOERTZEL_NUM_BINS)} total)"
    )
    print(f"Saved: {output_dir / 'word_features.npz'}")
    print(f"Saved: {output_dir / 'word_features.csv'}")


if __name__ == "__main__":
    main()
