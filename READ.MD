Project: Embedded Isolated Word Speech Recognizer on ATmega32A
What is it?
This is a real-time, isolated-word speech recognition system built entirely from scratch on a bare-metal ATmega32A microcontroller (an 8-bit AVR CPU running at 11.0592 MHz with 2 KB of RAM and 32 KB of flash). It listens for one of 8 command words spoken into a microphone, and displays the recognized word on an LCD — with no operating system, no floating-point unit, and no external AI chip.

The 8 vocabulary words are: ON, OFF, CLOSE, STOP, UP, DOWN, LEFT, RIGHT

The Big Picture: What You Built
The system is split into two sides:


   PC side (Python)                        MCU side (C / avr-gcc)
  ──────────────────                       ─────────────────────
  1. Record .wav samples                   1. Capture audio via ADC (ISR)
  2. Extract features                      2. Voice Activity Detection (VAD)
  3. Select templates (K-Means)            3. Feature extraction (STE, ZCE, Goertzel)
  4. Generate C arrays → flash to MCU      4. DTW classification against templates
                                           5. Display result on 16×2 LCD
PC Side: Training Pipeline
Data collection — You recorded ~310 .wav samples across the 8 words (25–60 samples each at 8 kHz) and stored them in data/.

Feature extraction (PC/features/feature_extraction.py) — The Python code mirrors the MCU's signal processing exactly, producing three channels per recording:

Feature	What it captures
STE (Short-Time Energy)	Loudness envelope — when the word is loud vs. silent
ZCE (Zero-Crossing Events)	Noisiness / fricative content
Goertzel (5 frequency bins)	Spectral shape at 350, 900, 1700, 2700, 3500 Hz (formant-aligned)
Audio is processed in 128-sample frames with overlapping (each feature point is the average of two consecutive frames), giving up to 53 feature frames per word. The features are quantized to uint8 (0–255) and peak-normalized.

Template selection (PC/traning/extract_templates.py) — For each word, K-Means clustering (k=5) is run on all samples to find 5 representative templates covering the natural variation in how the word is spoken. The closest real sample to each centroid is chosen as the template (not a synthetic average).

Code generation — The script then auto-generates MCU/External_libraries/word_templates_data.c and its header — a C array containing all 8×5=40 templates stored in AVR PROGMEM (flash, not RAM), so the limited 2 KB SRAM is not consumed.

MCU Side: Real-Time Recognition
Audio capture (MCU/main.c) — The ADC is triggered by Timer1 Compare B at exactly 8 kHz (OCR1A=172, prescaler=8). An ISR reads each 10-bit ADC sample, removes the DC bias using an exponential moving average (EMA), and fills a double-buffered ping-pong buffer (2 × 128 samples) so the main loop and ISR never collide.

Voice Activity Detection (VAD) — In the IDLE state, each 128-sample block is checked with two independent detectors:

STE threshold: average |sample| > 70 → speech detected
ZCE dead-zone threshold: zero-crossings outside a noise-adaptive dead-zone > 15 → speech detected (handles fricative onsets like /s/ in "stop")
Feature extraction during recording — Once speech is detected, the MCU extracts exactly the same STE, ZCE, and 5-bin Goertzel features as the Python training code, using Q14 fixed-point arithmetic (no floating point needed). The Goertzel algorithm uses pre-computed Q14 coefficients and an alpha-max-plus-beta-min approximation for the magnitude.

End-of-speech detection — Recording stops when 12 consecutive silent frames are seen (or 53 frames maximum is reached).

Peak normalization — STE and Goertzel features are normalized to 0–255 relative to their own peak, making the classifier robust to microphone distance and speaker volume.

DTW Classification (MCU/External_libraries/dtw.c) — The live recording is compared to all 40 stored templates using Dynamic Time Warping with a Sakoe-Chiba band of ±5 frames. DTW handles natural variation in speaking speed (e.g., "CLOSE" spoken fast vs. slow). A length-ratio guard skips templates that are more than 2× longer/shorter than the query to save computation. The word with the lowest DTW distance wins.

Display — The result is shown on a 16×2 HD44780-compatible LCD.

Key Engineering Challenges You Solved
Problem	Solution
DC bias from microphone	EMA-based adaptive DC removal in the ISR
ISR/main buffer race	Double-buffering (ping-pong)
Fricative words not detected ("stop", "off")	Added ZCE dead-zone VAD alongside STE
UP/ON confusion	Goertzel spectral features discriminate the vowels /ʌ/ vs /ɒ/
CLOSE/RIGHT overlap	Careful word selection + 5-template clustering
No float on MCU	All math in Q14 fixed-point, integer shifts
2 KB RAM limit	Templates stored in PROGMEM, read byte-by-byte via pgm_read_byte
Variable speaking speed	DTW with Sakoe-Chiba band
Noisy silence detection	Adaptive noise floor EMA + Goertzel-based fricative check
PC↔MCU pipeline integrity	Python feature extraction is a bit-exact mirror of MCU C code
Technology Stack
MCU: ATmega32A (AVR 8-bit, 11.0592 MHz, 2 KB RAM, 32 KB flash)
Toolchain: avr-gcc, avrdude, USBasp programmer
PC training: Python 3 with NumPy, scikit-learn (K-Means)
Algorithm: DTW (Dynamic Time Warping) with Sakoe-Chiba band
Features: STE + ZCE + 5-bin Goertzel pseudo-magnitude
Training data: ~310 .wav recordings at 8 kHz
Project Evolution (from git history)
Basic pipeline: STE + ZCE only, 2-word classifier
Added Goertzel spectral features → more words
Overlapping frame extraction, DC bias removal
Normalized STE (relative, not absolute energy)
Dynamic ZCE dead-zone for better VAD
Double-buffering with overlapping feature calculation
Switched to pseudo-magnitude Goertzel (much more expressive than power)
Dynamic timing vector (variable-length DTW)
Solved UP/ON and other word conflicts
Added "close" — final 8-word vocabulary locked in