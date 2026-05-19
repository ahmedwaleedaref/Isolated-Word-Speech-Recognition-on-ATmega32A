from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
from sklearn.cluster import KMeans

DEFAULT_TEMPLATES_PER_WORD = 5
RANDOM_STATE = 42
UINT8_MAX = int(np.iinfo(np.uint8).max)
MCU_WORD_LABEL_ORDER = ["ON", "OFF", "CLOSE", "STOP", "UP", "DOWN", "LEFT", "RIGHT"]


def load_features_csv(csv_path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str]]:
    labels: list[str] = []
    lengths: list[int] = []
    rows: list[list[float]] = []

    with csv_path.open("r", encoding="utf-8", newline="") as csv_file:
        reader = csv.reader(csv_file)
        header = next(reader, None)
        if header is None:
            raise RuntimeError(f"CSV is empty: {csv_path}")
        if len(header) < 3:
            raise RuntimeError("CSV must contain label + frames + feature columns")

        if header[1] != "frames":
            raise RuntimeError("Second CSV column must be 'frames'")

        feature_headers = header[2:]

        for row in reader:
            labels.append(row[0])
            lengths.append(int(row[1]))
            rows.append([float(value) for value in row[2:]])

    if not rows:
        raise RuntimeError(f"No samples found in CSV: {csv_path}")

    features = np.asarray(rows, dtype=np.float32)
    return features, np.asarray(labels), np.asarray(lengths, dtype=np.int32), feature_headers


def extract_kmeans_templates(
    features: np.ndarray,
    labels: np.ndarray,
    lengths: np.ndarray,
    templates_per_word: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    unique_labels = sorted(np.unique(labels))

    all_templates: list[np.ndarray] = []
    all_template_labels: list[str] = []
    all_template_ids: list[int] = []
    representative_indices: list[int] = []
    all_template_lengths: list[int] = []

    for label in unique_labels:
        label_indices = np.where(labels == label)[0]
        label_features = features[label_indices]
        label_lengths = lengths[label_indices]

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

            representative_feature = label_features[nearest_local_idx]
            representative_length = label_lengths[nearest_local_idx]

            all_templates.append(representative_feature)
            all_template_labels.append(label)
            all_template_ids.append(template_id)
            representative_indices.append(int(label_indices[nearest_local_idx]))
            all_template_lengths.append(int(representative_length))

    return (
        np.vstack(all_templates),
        np.asarray(all_template_labels),
        np.asarray(all_template_ids, dtype=np.int32),
        np.asarray(representative_indices, dtype=np.int32),
        np.asarray(all_template_lengths, dtype=np.int32),
    )


def save_templates_csv(
    output_csv: Path,
    templates: np.ndarray,
    template_lengths: np.ndarray,
    template_labels: np.ndarray,
    template_ids: np.ndarray,
    representative_indices: np.ndarray,
    feature_headers: list[str],
) -> None:
    output_csv.parent.mkdir(parents=True, exist_ok=True)

    with output_csv.open("w", encoding="utf-8", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(
            ["label", "template_id", "representative_sample_index", "frames", *feature_headers]
        )

        for label, template_id, sample_idx, template_len, template in zip(
            template_labels, template_ids, representative_indices, template_lengths, templates
        ):
            writer.writerow(
                [label, int(template_id), int(sample_idx), int(template_len), *[int(value) for value in template]]
            )


def quantize_templates_to_uint8(templates: np.ndarray) -> np.ndarray:
    rounded = np.rint(templates)

    rounded_min = int(np.min(rounded))
    rounded_max = int(np.max(rounded))
    if rounded_min < 0 or rounded_max > UINT8_MAX:
        raise RuntimeError(
            "Template value out of uint8_t range after rounding: "
            f"min={rounded_min}, max={rounded_max}"
        )

    return rounded.astype(np.uint8)


def build_word_template_map(
    templates: np.ndarray,
    template_lengths: np.ndarray,
    template_labels: np.ndarray,
    template_ids: np.ndarray,
    templates_per_word: int,
) -> dict[str, list[tuple[np.ndarray, int]]]:
    label_set = set(template_labels.tolist())
    expected_label_set = set(MCU_WORD_LABEL_ORDER)

    missing_labels = expected_label_set - label_set
    unexpected_labels = label_set - expected_label_set
    if missing_labels:
        raise RuntimeError(f"Missing labels for MCU export: {sorted(missing_labels)}")
    if unexpected_labels:
        raise RuntimeError(f"Unexpected labels for MCU export: {sorted(unexpected_labels)}")

    grouped_templates: dict[str, list[tuple[np.ndarray, int]]] = {}
    for label in MCU_WORD_LABEL_ORDER:
        label_mask = template_labels == label
        label_templates = templates[label_mask]
        label_lengths = template_lengths[label_mask]
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

        grouped_templates[label] = [
            (label_templates[index], int(label_lengths[index]))
            for index in sort_indices
        ]

    return grouped_templates


def save_word_templates_c_files(
    output_header: Path,
    output_source: Path,
    templates: np.ndarray,
    template_lengths: np.ndarray,
    template_labels: np.ndarray,
    template_ids: np.ndarray,
    feature_headers: list[str],
    templates_per_word: int,
) -> None:
    feature_count = len(feature_headers)
    ste_feature_count = sum(1 for name in feature_headers if name.startswith("STE_"))
    zce_feature_count = sum(1 for name in feature_headers if name.startswith("ZCE_"))
    # Goertzel bins: headers named G<freq>_<window> (e.g. G350_0 .. G3500_30)
    goertzel_bins: list[str] = sorted(
        {name.split("_")[0] for name in feature_headers if name.startswith("G") and "_" in name},
        key=lambda s: int(s[1:]) if s[1:].isdigit() else 0,
    )
    goertzel_num_bins = len(goertzel_bins)
    goertzel_features_per_bin = (
        sum(1 for name in feature_headers if name.startswith(f"{goertzel_bins[0]}_"))
        if goertzel_num_bins > 0 else 0
    )
    total_goertzel = goertzel_num_bins * goertzel_features_per_bin
    expected = ste_feature_count + zce_feature_count + total_goertzel
    if expected != feature_count:
        raise RuntimeError(
            f"Feature headers must be STE_*, ZCE_*, and G<freq>_*. "
            f"Got ste={ste_feature_count}, zce={zce_feature_count}, "
            f"goertzel={goertzel_num_bins}×{goertzel_features_per_bin}={total_goertzel}, "
            f"total={expected}, found={feature_count}"
        )

    grouped_templates = build_word_template_map(
        templates=templates,
        template_lengths=template_lengths,
        template_labels=template_labels,
        template_ids=template_ids,
        templates_per_word=templates_per_word,
    )

    if ste_feature_count != zce_feature_count or (
        goertzel_num_bins > 0 and goertzel_features_per_bin != ste_feature_count
    ):
        raise RuntimeError(
            "Expected equal per-channel max frame count across STE/ZCE/Goertzel headers."
        )

    max_feature_frames = ste_feature_count
    template_channel_count = 2 + goertzel_num_bins

    template_lengths_matrix: list[list[int]] = []
    template_values_matrix: list[list[list[list[int]]]] = []

    for label in MCU_WORD_LABEL_ORDER:
        row_lengths: list[int] = []
        row_templates: list[list[list[int]]] = []

        for template_vec, template_len in grouped_templates[label]:
            if template_len <= 0:
                raise RuntimeError(f"Template for '{label}' has invalid length: {template_len}")
            if template_len > max_feature_frames:
                raise RuntimeError(
                    f"Template for '{label}' has length {template_len}, exceeds max {max_feature_frames}"
                )

            vals = [int(value) for value in template_vec]
            channels: list[list[int]] = []

            ste_vals = vals[0:max_feature_frames]
            zce_vals = vals[max_feature_frames : 2 * max_feature_frames]
            if len(ste_vals) != max_feature_frames or len(zce_vals) != max_feature_frames:
                raise RuntimeError(
                    f"Template for '{label}' has malformed STE/ZCE channel data."
                )
            channels.append(ste_vals)
            channels.append(zce_vals)

            goertzel_base = 2 * max_feature_frames
            for b in range(goertzel_num_bins):
                start = goertzel_base + b * max_feature_frames
                goertzel_vals = vals[start : start + max_feature_frames]
                if len(goertzel_vals) != max_feature_frames:
                    raise RuntimeError(
                        f"Template for '{label}' has malformed Goertzel channel data."
                    )
                channels.append(goertzel_vals)

            for channel in channels:
                for frame_idx in range(template_len, max_feature_frames):
                    channel[frame_idx] = 0

            row_lengths.append(template_len)
            row_templates.append(channels)

        template_lengths_matrix.append(row_lengths)
        template_values_matrix.append(row_templates)

    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_source.parent.mkdir(parents=True, exist_ok=True)

    header_lines = [
        "#ifndef WORD_TEMPLATES_DATA_H",
        "#define WORD_TEMPLATES_DATA_H",
        "",
        "#include <stdint.h>",
        "#include <avr/pgmspace.h>",
        '#include "goertzel.h"',
        "",
        f"#define WORD_COUNT {len(MCU_WORD_LABEL_ORDER)}",
        f"#define TEMPLATES_PER_WORD {templates_per_word}",
        f"#define MAX_FEATURE_FRAMES {max_feature_frames}",
        f"#define STE_FEATURE_COUNT MAX_FEATURE_FRAMES",
        f"#define ZCE_FEATURE_COUNT MAX_FEATURE_FRAMES",
        f"#define GOERTZEL_FEATURE_COUNT_PER_BIN MAX_FEATURE_FRAMES",
        f"#define TOTAL_GOERTZEL_FEATURE_COUNT (GOERTZEL_NUM_BINS * GOERTZEL_FEATURE_COUNT_PER_BIN)",
        f"#define TEMPLATE_CHANNEL_COUNT {template_channel_count}",
        "",
        "extern const char *const WORD_LABELS[WORD_COUNT];",
        "extern const uint8_t WORD_TEMPLATE_LENGTHS[WORD_COUNT][TEMPLATES_PER_WORD] PROGMEM;",
        "extern const uint8_t WORD_TEMPLATES[WORD_COUNT][TEMPLATES_PER_WORD][TEMPLATE_CHANNEL_COUNT][MAX_FEATURE_FRAMES] PROGMEM;",
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
            "const uint8_t WORD_TEMPLATE_LENGTHS[WORD_COUNT][TEMPLATES_PER_WORD] PROGMEM = {",
        ]
    )

    for word_index, label in enumerate(MCU_WORD_LABEL_ORDER):
        source_lines.append(f"    /* {word_index}: {label} */")
        len_row = ", ".join(f"{value}u" for value in template_lengths_matrix[word_index])
        source_lines.append(f"    {{{len_row}}},")

    source_lines.extend(
        [
            "};",
            "",
            "const uint8_t WORD_TEMPLATES[WORD_COUNT][TEMPLATES_PER_WORD][TEMPLATE_CHANNEL_COUNT][MAX_FEATURE_FRAMES] PROGMEM = {",
        ]
    )

    for word_index, label in enumerate(MCU_WORD_LABEL_ORDER):
        source_lines.append(f"    /* {word_index}: {label} */")
        source_lines.append("    {")
        for tmpl_index, channels in enumerate(template_values_matrix[word_index]):
            source_lines.append(f"        /* template {tmpl_index} */")
            source_lines.append("        {")

            for channel_index, channel_values in enumerate(channels):
                if channel_index == 0:
                    channel_name = "STE"
                elif channel_index == 1:
                    channel_name = "ZCE"
                else:
                    channel_name = goertzel_bins[channel_index - 2]

                values_str = ", ".join(f"{value}u" for value in channel_values)
                source_lines.append(f"            /* {channel_name} */ {{{values_str}}},")

            source_lines.append("        },")
        source_lines.append("    },")

    source_lines.append("};")
    output_source.write_text("\n".join(source_lines) + "\n", encoding="utf-8")


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent.parent
    output_dir = script_dir / "output"

    parser = argparse.ArgumentParser(
        description="Extract K-Means-selected representative templates from each word's available samples."
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

    features, labels, lengths, feature_headers = load_features_csv(args.input_csv)
    templates, template_labels, template_ids, representative_indices, template_lengths = extract_kmeans_templates(
        features=features,
        labels=labels,
        lengths=lengths,
        templates_per_word=args.templates_per_word,
    )
    templates_uint8 = quantize_templates_to_uint8(templates)

    save_templates_csv(
        output_csv=args.output_csv,
        templates=templates_uint8,
        template_lengths=template_lengths,
        template_labels=template_labels,
        template_ids=template_ids,
        representative_indices=representative_indices,
        feature_headers=feature_headers,
    )

    args.output_npz.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        args.output_npz,
        templates=templates_uint8,
        template_lengths=template_lengths,
        labels=template_labels,
        template_ids=template_ids,
        representative_sample_indices=representative_indices,
        feature_names=np.asarray(feature_headers),
    )
    save_word_templates_c_files(
        output_header=args.output_c_header,
        output_source=args.output_c_source,
        templates=templates_uint8,
        template_lengths=template_lengths,
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
