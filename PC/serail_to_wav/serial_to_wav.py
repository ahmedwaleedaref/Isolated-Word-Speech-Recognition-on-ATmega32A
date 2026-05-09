import argparse
import struct
import sys
import wave

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
    wait_for_marker(ser, START_MARKER)

    # Read dc_bias sent by MCU as 2 big-endian bytes immediately after START marker
    dc_bias = struct.unpack(">H", read_exact(ser, 2))[0]

    payload_size = frame_samples * ADC_SAMPLE_BYTES
    payload = read_exact(ser, payload_size)

    end_marker = read_exact(ser, len(END_MARKER))
    if end_marker != END_MARKER:
        raise ValueError("Frame end marker mismatch. UART stream is out of sync.")

    # MCU sends each sample as hi byte then lo byte -> big-endian 16-bit
    raw_samples = struct.unpack(f">{frame_samples}H", payload)
    # Center using the dc_bias the MCU measured during silence
    centered_samples = [int(s) - dc_bias for s in raw_samples]
    return centered_samples


def write_wav(path, samples_pcm16, sample_rate):
    with wave.open(path, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(int(sample_rate))
        wav_file.writeframes(struct.pack(f"<{len(samples_pcm16)}h", *samples_pcm16))


def main():
    parser = argparse.ArgumentParser(description="Receive one UART frame from MCU and save it as WAV.")
    parser.add_argument("--port", required=True, help="Serial port (example: /dev/ttyUSB0)")
    parser.add_argument("--output", default="captured.wav", help="Output WAV file path (default: captured.wav)")
    parser.add_argument("--baud", type=int, default=230400, help="UART baud rate (default: 230400)")
    parser.add_argument("--frame-samples", type=int, default=8000, help="Number of ADC samples per frame (default: 8000)")
    parser.add_argument("--sample-rate", type=float, default=8000.0, help="WAV sampling rate in Hz (default: 8000)")
    parser.add_argument("--timeout", type=float, default=1.0, help="Serial timeout in seconds (default: 1.0)")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
            print("Waiting for START ...")
            centered_samples = read_frame(ser, args.frame_samples)
    except (serial.SerialException, TimeoutError, ValueError) as exc:
        print(f"Serial/frame error: {exc}", file=sys.stderr)
        raise SystemExit(1)

    write_wav(args.output, centered_samples, args.sample_rate)
    print(f"Saved {len(centered_samples)} samples to {args.output}")


if __name__ == "__main__":
    main()
