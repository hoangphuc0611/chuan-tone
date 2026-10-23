# Tài liệu tham khảo (clean-room)

Mọi thuật toán trong ToneCore được viết từ các nguồn công khai dưới đây. Khi thêm thuật toán mới, ghi nguồn vào đây trong cùng commit, kèm file mã nguồn dùng nó.

## Real-time audio & kiến trúc

| Nguồn | Dùng cho | File |
|---|---|---|
| R. Bencina, *Real-time audio programming 101: time waits for nothing*, 2011 (blog) | Quy tắc audio callback | toàn bộ `[RT]` |
| T. Doumler, các bài nói CppCon/ADC về lock-free cho audio | SPSC, atomic param | `core/spsc_ring.h`, `core/smoothed_param.h` |
| L. Lamport, *Specifying Concurrent Program Modules*, ACM TOPLAS 1983 | Hàng đợi SPSC không khóa | `core/spsc_ring.h` |
| Arm Architecture Reference Manual (FPCR.FZ); Intel SDM (MXCSR FTZ/DAZ) | Chống denormal | `core/denormal.h` |
| Android Developers – *Audio latency*; Oboe docs (*FullDuplexStream*, *Getting Started*) | Audio I/O Android | `io/android/oboe_backend.cpp` |
| Android NDK – *Media* (`AMediaExtractor`, `AMediaCodec`) | Giải mã beat Android | `decode/decoder_android.cpp` |
| Apple – *Audio Session Programming Guide*; *Audio Unit Hosting Guide for iOS* (RemoteIO) | Audio I/O iOS | `io/ios/remoteio_backend.mm` |
| Apple – *Extended Audio File Services Reference* | Giải mã beat iOS/macOS | `decode/decoder_apple.cpp` |

## Đo độ trễ & resample (M1)

| Nguồn | Dùng cho | File |
|---|---|---|
| D. Rife, J. Vanderkooy, *Transfer-Function Measurement with Maximum-Length Sequences*, JAES 1989 | Chuỗi MLS đo độ trễ | `latency/mls.cpp`, `latency/latency_probe.cpp` |
| Xilinx XAPP052, *Efficient Shift Registers, LFSR Counters, and Long Pseudo-Random Sequence Generators* | Bảng đa thức nguyên thủy | `latency/mls.cpp` |
| J. O. Smith, *Digital Audio Resampling Home Page* (CCRMA) | Resample sinc có cửa sổ | `dsp/resampler.cpp` |
| J. F. Kaiser, *Nonrecursive digital filter design using the I0-sinh window function*, 1974 | Cửa sổ Kaiser | `dsp/resampler.cpp` |

## Dò cao độ & chỉnh giọng (M2)

| Nguồn | Dùng cho | File |
|---|---|---|
| J. W. Cooley, J. W. Tukey, *An algorithm for the machine calculation of complex Fourier series*, Math. Comp. 1965; A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing*, 3rd ed. (ch. 9: FFT; mục 7.6: cửa sổ Kaiser) | FFT số thực radix-2, thiết kế FIR | `dsp/fft.cpp`, `dsp/kaiser.h`, `dsp/decimator.cpp` |
| R. Bristow-Johnson, *Cookbook formulae for audio EQ biquad filter coefficients* (W3C Audio EQ Cookbook) | Lọc thông cao 80 Hz, thông thấp | `dsp/biquad.h` |
| P. McLeod, G. Wyvill, *A Smarter Way to Find Pitch*, ICMC 2005 | Dò cao độ MPM (NSDF, đỉnh khoá, ngưỡng k) | `pitch/mpm.cpp` |
| A. de Cheveigné, H. Kawahara, *YIN, a fundamental frequency estimator for speech and music*, JASA 2002 | Đối chứng YIN | `pitch/yin.cpp` |
| M. Mauch, S. Dixon, *pYIN: A fundamental frequency estimator using probabilistic threshold distributions*, ICASSP 2014 | Ý tưởng phân loại hữu/vô thanh | `pitch/f0_tracker.cpp` |
| J. Salamon, E. Gómez, D. Ellis, G. Richard, *Melody Extraction from Polyphonic Music Signals*, IEEE SPM 2014 | Chỉ số VR/VFA/RPA/RCA/OA | `tools/pitch/eval_f0.py` |
| A. Rosenberg, *Effect of glottal pulse shape on the quality of natural vowels*, JASA 1971; G. Fant, *Acoustic Theory of Speech Production*, 1960; G. Peterson, H. Barney, *Control methods used in a study of the vowels*, JASA 1952 | Giọng tổng hợp cho test (nguồn–lọc, formant) | `tests/util/synth_voice.cpp` |
| E. Moulines, F. Charpentier, *Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones*, Speech Communication 1990 | TD-PSOLA | `pitch/psola.cpp` |
| C. Lameter, *Effective synchronization on Linux/NUMA systems* (seqlock), Gelato Conference 2005 | Gửi trạng thái cao độ audio thread → UI không khoá | `pitch/pitch_state.h` |
| U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley 2011 — chương pitch shifting | PSOLA, làm mượt đường chỉnh | `pitch/psola.cpp`, `pitch/corrector.cpp` |

## Đổi tone beat (M4)

- J. Laroche, M. Dolson, *Improved phase vocoder time-scale modification of audio*, IEEE TSAP 1999.
- Z. Průša, N. Holighaus, *Phase vocoder done right* (PGHI), EUSIPCO 2017.
- A. Röbel, *A new approach to transient processing in the phase vocoder*, DAFx 2003.

## Dò tone, BPM, loudness (M3)

- C. Krumhansl, *Cognitive Foundations of Musical Pitch*, OUP 1990 — thuật toán Krumhansl–Schmuckler (tương quan chroma với profile xoay vòng, `key/key_detect.cpp`).
- C. Krumhansl, E. Kessler, *Tracing the dynamic changes in perceived tonal organization in a spatial representation of musical keys*, Psychological Review 1982 — profile "kk".
- D. Temperley, *What's key for key? The Krumhansl–Schmuckler key-finding algorithm reconsidered*, Music Perception 1999 — profile "temperley".
- D. Temperley, *Music and Probability*, MIT Press 2007 — profile "temperley-kp" (kho Kostka–Payne).
- Á. Faraldo et al., *Key Estimation in Electronic Dance Music*, ECIR 2016 — profile EDMA.
- D. Fitzgerald, *Harmonic/Percussive Separation using Median Filtering*, DAFx 2010.
- C. Schörkhuber, A. Klapuri, *Constant-Q transform toolbox for music processing*, SMC 2010 — CQT theo từng quãng 8 + hạ mẫu ÷2 (`key/cqt.cpp`).
- J. C. Brown, *Calculation of a constant Q spectral transform*, JASA 1991; J. C. Brown, M. S. Puckette, *An efficient algorithm for the calculation of a constant Q transform*, JASA 1992 — kernel phổ thưa.
- J. B. Allen, L. R. Rabiner, *A unified approach to short-time Fourier analysis and synthesis*, Proc. IEEE 1977 — ISTFT overlap-add có cửa sổ (`key/hpss.cpp`).
- C. Harte, M. Sandler, *Automatic chord identification using a quantised chromagram*, AES 2005 — ước lượng tuning (`key/tuning.cpp`, làm trên đỉnh STFT thay vì CQT).
- J. O. Smith, X. Serra, *PARSHL: An Analysis/Synthesis Program for Non-Harmonic Sounds Based on a Sinusoidal Representation*, ICMC 1987 — nội suy parabol đỉnh phổ (QIFFT).
- M. Müller, *Fundamentals of Music Processing*, Springer 2015 (bản 2, 2021), mục 3.1 — chroma, nén log.
- K. Noland, M. Sandler, *Key estimation using a hidden Markov model*, ISMIR 2006 — HMM 24 tone.
- H. Papadopoulos, G. Peeters, *Local key estimation from an audio signal relying on harmony and metrical structures*, IEEE TASLP 2012 — tone cục bộ / chuyển tone.
- L. R. Rabiner, *A tutorial on hidden Markov models and selected applications in speech recognition*, Proc. IEEE 1989 — Viterbi, forward–backward (`key/key_timeline.cpp`).
- J. P. Bello et al., *A Tutorial on Onset Detection in Music Signals*, IEEE TSAP 2005 — spectral flux trên log biên độ.
- G. Percival, G. Tzanetakis, *Streamlined Tempo Estimation Based on Autocorrelation and Cross-correlation With Pulses*, IEEE/ACM TASLP 2014 — tự tương quan onset + răng lược bội chu kỳ.
- D. Ellis, *Beat Tracking by Dynamic Programming*, JNMR 2007 — prior tempo log-Gauss (`key/tempo.cpp`).
- ITU-R BS.1770-4, *Algorithms to measure audio programme loudness and true-peak audio level* — K-weighting, cổng (`key/loudness.cpp`).
- EBU Tech 3341, *Loudness Metering: "EBU Mode" metering to supplement EBU R 128 loudness normalization* — ca test tuân thủ (`tests/unit/loudness_test.cpp`).
- MIREX Audio Key Detection – quy tắc chấm điểm. Cài đặt tham chiếu: C. Raffel et al., *mir_eval: A Transparent Implementation of Common MIR Metrics*, ISMIR 2014 (`mir_eval.key.weighted_score`, dùng để kiểm chéo `tools/keytest/mirex.py`).

## Hiệu ứng (M5)

- R. Bristow-Johnson, *Cookbook formulae for audio EQ biquad filter coefficients* (Audio EQ Cookbook).
- V. Zavalishin, *The Art of VA Filter Design*, 2018 — SVF/TPT.
- D. Giannoulis, M. Massberg, J. Reiss, *Digital Dynamic Range Compressor Design – A Tutorial and Analysis*, JAES 2012.
- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators*, AES 1991 — FDN.

## Fingerprint

- A. Wang, *An Industrial-Strength Audio Search Algorithm*, ISMIR 2003 — cần ghi vào `patent-review.md`.
