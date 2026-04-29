import argparse
import struct
import sys

import matplotlib.pyplot as plt
import serial

START_MARKER = b"START\r\n"
END_MARKER = b"END\r\n"
ADC_SAMPLE_BYTES = 2


def read_exact(ser, size):
    data = bytearray()
    while len(data) < size:
        chunk = ser.read(size - len(data))
        if not chunk:
            raise TimeoutError(f"Timed out while reading {size} bytes from serial port.")
        data.extend(chunk)
    return bytes(data)


def wait_for_marker(ser, marker):
    marker_len = len(marker)
    window = bytearray()

    while True:
        chunk = ser.read(1)
        if not chunk:
            continue

        window.extend(chunk)
        if len(window) > marker_len:
            window = window[-marker_len:]

        if window == marker:
            return


def read_frame(ser, frame_samples):
    # Wait for the START marker (MCU sends "START\r\n" before binary samples)
    wait_for_marker(ser, START_MARKER)

    payload_size = frame_samples * ADC_SAMPLE_BYTES
    payload = read_exact(ser, payload_size)

    # MCU sends END marker as text after the binary payload ("END\r\n")
    end_marker = read_exact(ser, len(END_MARKER))
    if end_marker != END_MARKER:
        raise ValueError("Frame end marker mismatch. UART stream is out of sync.")

    # MCU sends each sample as hi byte then lo byte -> big-endian 16-bit
    return list(struct.unpack(f">{frame_samples}H", payload))


def plot_samples(samples, sample_rate):
    time_axis = [index / sample_rate for index in range(len(samples))]
    plt.figure(figsize=(10, 4))
    plt.plot(time_axis, samples, linewidth=1.0)
    plt.title("Waveform from ATmega32 (ADC over UART)")
    plt.xlabel("Time (s)")
    plt.ylabel("ADC value")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


def main():
    parser = argparse.ArgumentParser(description="Plot MCU waveform received over UART.")
    parser.add_argument("--port", required=True, help="Serial port (example: /dev/ttyUSB2)")
    parser.add_argument("--baud", type=int, default=230400, help="UART baud rate (default: 230400)")
    parser.add_argument("--frame-samples", type=int, default=8000, help="Number of ADC samples per frame (default: 8000)")
    parser.add_argument("--sample-rate", type=float, default=8000.0, help="Sampling rate in Hz (default: 8000)")
    parser.add_argument("--timeout", type=float, default=1.0, help="Serial timeout in seconds (default: 1.0)")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
            print("Waiting for START ...")
            samples = read_frame(ser, args.frame_samples)
    except (serial.SerialException, TimeoutError, ValueError) as exc:
        print(f"Serial/frame error: {exc}", file=sys.stderr)
        raise SystemExit(1)

    if not samples:
        print("No samples received between START and END.", file=sys.stderr)
        raise SystemExit(1)

    print(f"Received {len(samples)} samples.")
    plot_samples(samples, args.sample_rate)


if __name__ == "__main__":
    main()
