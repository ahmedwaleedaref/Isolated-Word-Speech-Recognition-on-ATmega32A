from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
from sklearn.cluster import KMeans

EXPECTED_SAMPLES_PER_WORD = 10
DEFAULT_TEMPLATES_PER_WORD = 2
RANDOM_STATE = 42
UINT16_MIN = 0
UINT16_MAX = int(np.iinfo(np.uint16).max)
MCU_WORD_LABEL_ORDER = ["ON", "OFF", "START", "STOP", "UP", "DOWN", "LEFT", "RIGHT"]


def load_features_csv(csv_path: Path) -> tuple[np.ndarray, np.ndarray, list[str]]:
    labels: list[str] = []
    rows: list[list[float]] = []

    with csv_path.open("r", encoding="utf-8", newline="") as csv_file:
        reader = csv.reader(csv_file)
        header = next(reader, None)
        if header is None:
            raise RuntimeError(f"CSV is empty: {csv_path}")
        if len(header) < 2:
            raise RuntimeError("CSV must contain label + feature columns")

        feature_headers = header[1:]

        for row in reader:
            labels.append(row[0])
            rows.append([float(value) for value in row[1:]])

    if not rows:
        raise RuntimeError(f"No samples found in CSV: {csv_path}")

    features = np.asarray(rows, dtype=np.float32)
    return features, np.asarray(labels), feature_headers


def extract_kmeans_templates(
    features: np.ndarray,
    labels: np.ndarray,
    templates_per_word: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    unique_labels = sorted(np.unique(labels))

    all_templates: list[np.ndarray] = []
    all_template_labels: list[str] = []
    all_template_ids: list[int] = []
    representative_indices: list[int] = []

    for label in unique_labels:
        label_indices = np.where(labels == label)[0]
        label_features = features[label_indices]

        if label_features.shape[0] != EXPECTED_SAMPLES_PER_WORD:
            raise RuntimeError(
                f"Expected {EXPECTED_SAMPLES_PER_WORD} samples for '{label}', found {label_features.shape[0]}"
            )
        if label_features.shape[0] < templates_per_word:
            raise RuntimeError(
                f"Cannot extract {templates_per_word} templates from {label_features.shape[0]} samples for '{label}'"
            )

        kmeans = KMeans(
            n_clusters=templates_per_word,
            random_state=RANDOM_STATE,
            n_init=10,
        )
        cluster_ids = kmeans.fit_predict(label_features)
        centroids = kmeans.cluster_centers_.astype(np.float32)

        for template_id, centroid in enumerate(centroids):
            cluster_member_local_indices = np.where(cluster_ids == template_id)[0]
            if cluster_member_local_indices.size == 0:
                raise RuntimeError(f"Empty cluster for '{label}' template {template_id}")

            cluster_members = label_features[cluster_member_local_indices]
            nearest_local_idx = int(
                cluster_member_local_indices[
                    np.argmin(np.linalg.norm(cluster_members - centroid, axis=1))
                ]
            )

            all_templates.append(centroid)
            all_template_labels.append(label)
            all_template_ids.append(template_id)
            representative_indices.append(int(label_indices[nearest_local_idx]))

    return (
        np.vstack(all_templates),
        np.asarray(all_template_labels),
        np.asarray(all_template_ids, dtype=np.int32),
        np.asarray(representative_indices, dtype=np.int32),
    )


def save_templates_csv(
    output_csv: Path,
    templates: np.ndarray,
    template_labels: np.ndarray,
    template_ids: np.ndarray,
    representative_indices: np.ndarray,
    feature_headers: list[str],
) -> None:
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    with output_csv.open("w", encoding="utf-8", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(
            ["label", "template_id", "representative_sample_index", *feature_headers]
        )

        for label, template_id, sample_idx, template in zip(
            template_labels, template_ids, representative_indices, templates
        ):
            writer.writerow(
                [label, int(template_id), int(sample_idx), *[int(value) for value in template]]
            )


def quantize_templates_to_uint16(templates: np.ndarray) -> np.ndarray:
    rounded = np.rint(templates)

    rounded_min = int(np.min(rounded))
    rounded_max = int(np.max(rounded))
    if rounded_min < UINT16_MIN or rounded_max > UINT16_MAX:
        raise RuntimeError(
            "Template value out of uint16_t range after rounding: "
            f"min={rounded_min}, max={rounded_max}"
        )

    return rounded.astype(np.uint16)


def build_word_template_map(
    templates: np.ndarray,
    template_labels: np.ndarray,
    template_ids: np.ndarray,
    templates_per_word: int,
) -> dict[str, list[np.ndarray]]:
    label_set = set(template_labels.tolist())
    expected_label_set = set(MCU_WORD_LABEL_ORDER)

    missing_labels = expected_label_set - label_set
    unexpected_labels = label_set - expected_label_set
    if missing_labels:
        raise RuntimeError(f"Missing labels for MCU export: {sorted(missing_labels)}")
    if unexpected_labels:
        raise RuntimeError(f"Unexpected labels for MCU export: {sorted(unexpected_labels)}")

    grouped_templates: dict[str, list[np.ndarray]] = {}
    for label in MCU_WORD_LABEL_ORDER:
        label_mask = template_labels == label
        label_templates = templates[label_mask]
        label_template_ids = template_ids[label_mask]

        if label_templates.shape[0] != templates_per_word:
            raise RuntimeError(
                f"Expected {templates_per_word} templates for '{label}', found {label_templates.shape[0]}"
            )

        sort_indices = np.argsort(label_template_ids)
        sorted_ids = label_template_ids[sort_indices]
        expected_ids = np.arange(templates_per_word, dtype=sorted_ids.dtype)
        if not np.array_equal(sorted_ids, expected_ids):
            raise RuntimeError(
                f"Template IDs for '{label}' must be 0..{templates_per_word - 1}, found {sorted_ids.tolist()}"
            )

        grouped_templates[label] = [label_templates[index] for index in sort_indices]

    return grouped_templates


def save_word_templates_c_files(
    output_header: Path,
    output_source: Path,
    templates: np.ndarray,
    template_labels: np.ndarray,
    template_ids: np.ndarray,
    feature_headers: list[str],
    templates_per_word: int,
) -> None:
    feature_count = len(feature_headers)
    ste_feature_count = sum(1 for name in feature_headers if name.startswith("STE_"))
    zce_feature_count = sum(1 for name in feature_headers if name.startswith("ZCE_"))
    if ste_feature_count + zce_feature_count != feature_count:
        raise RuntimeError("Feature headers must be STE_* and ZCE_* only")

    grouped_templates = build_word_template_map(
        templates=templates,
        template_labels=template_labels,
        template_ids=template_ids,
        templates_per_word=templates_per_word,
    )

    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_source.parent.mkdir(parents=True, exist_ok=True)

    header_lines = [
        "#ifndef WORD_TEMPLATES_DATA_H",
        "#define WORD_TEMPLATES_DATA_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define WORD_COUNT {len(MCU_WORD_LABEL_ORDER)}",
        f"#define TEMPLATES_PER_WORD {templates_per_word}",
        f"#define STE_FEATURE_COUNT {ste_feature_count}",
        f"#define ZCE_FEATURE_COUNT {zce_feature_count}",
        f"#define FEATURE_COUNT {feature_count}",
        "",
        "extern const char *const WORD_LABELS[WORD_COUNT];",
        "extern const uint16_t WORD_TEMPLATES[WORD_COUNT][TEMPLATES_PER_WORD][FEATURE_COUNT];",
        "",
        "#endif",
    ]
    output_header.write_text("\n".join(header_lines) + "\n", encoding="utf-8")

    source_lines = [
        '#include "word_templates_data.h"',
        "",
        "const char *const WORD_LABELS[WORD_COUNT] = {",
    ]
    source_lines.extend(f'    "{label}",' for label in MCU_WORD_LABEL_ORDER)
    source_lines.extend(
        [
            "};",
            "",
            "const uint16_t WORD_TEMPLATES[WORD_COUNT][TEMPLATES_PER_WORD][FEATURE_COUNT] = {",
        ]
    )

    values_per_line = 10
    for word_index, label in enumerate(MCU_WORD_LABEL_ORDER):
        source_lines.append(f"    /* {word_index}: {label} */")
        source_lines.append("    {")
        for template in grouped_templates[label]:
            source_lines.append("        {")
            values = [int(value) for value in template]
            for start in range(0, len(values), values_per_line):
                chunk = values[start : start + values_per_line]
                has_more_values = start + values_per_line < len(values)
                suffix = "," if has_more_values else ""
                formatted_chunk = ", ".join(f"{value}u" for value in chunk)
                source_lines.append(f"            {formatted_chunk}{suffix}")
            source_lines.append("        },")
        source_lines.append("    },")

    source_lines.append("};")
    output_source.write_text("\n".join(source_lines) + "\n", encoding="utf-8")


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent.parent
    output_dir = script_dir / "output"

    parser = argparse.ArgumentParser(
        description="Extract K-Means centroid templates from each word's 20 samples."
    )
    parser.add_argument(
        "--input-csv",
        type=Path,
        default=output_dir / "word_features.csv",
        help="Input features CSV generated by build_templates.py",
    )
    parser.add_argument(
        "--output-csv",
        type=Path,
        default=output_dir / "word_templates.csv",
        help="Output templates CSV",
    )
    parser.add_argument(
        "--output-npz",
        type=Path,
        default=output_dir / "word_templates.npz",
        help="Output templates NPZ",
    )
    parser.add_argument(
        "--templates-per-word",
        type=int,
        default=DEFAULT_TEMPLATES_PER_WORD,
        help="How many templates to extract per word label",
    )
    parser.add_argument(
        "--output-c-header",
        type=Path,
        default=project_root / "MCU" / "External_libraries" / "word_templates_data.h",
        help="Generated MCU header path",
    )
    parser.add_argument(
        "--output-c-source",
        type=Path,
        default=project_root / "MCU" / "External_libraries" / "word_templates_data.c",
        help="Generated MCU source path",
    )

    args = parser.parse_args()

    features, labels, feature_headers = load_features_csv(args.input_csv)
    templates, template_labels, template_ids, representative_indices = extract_kmeans_templates(
        features=features,
        labels=labels,
        templates_per_word=args.templates_per_word,
    )
    templates_uint16 = quantize_templates_to_uint16(templates)

    save_templates_csv(
        output_csv=args.output_csv,
        templates=templates_uint16,
        template_labels=template_labels,
        template_ids=template_ids,
        representative_indices=representative_indices,
        feature_headers=feature_headers,
    )

    args.output_npz.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        args.output_npz,
        templates=templates_uint16,
        labels=template_labels,
        template_ids=template_ids,
        representative_sample_indices=representative_indices,
        feature_names=np.asarray(feature_headers),
    )
    save_word_templates_c_files(
        output_header=args.output_c_header,
        output_source=args.output_c_source,
        templates=templates_uint16,
        template_labels=template_labels,
        template_ids=template_ids,
        feature_headers=feature_headers,
        templates_per_word=args.templates_per_word,
    )

    print(f"Input samples: {len(labels)}")
    print(f"Templates extracted: {len(template_labels)}")
    print(f"Templates per word: {args.templates_per_word}")
    print(f"Saved: {args.output_csv}")
    print(f"Saved: {args.output_npz}")
    print(f"Saved: {args.output_c_header}")
    print(f"Saved: {args.output_c_source}")


if __name__ == "__main__":
    main()
