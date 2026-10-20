# Tài liệu tham khảo (clean-room)

Mọi thuật toán trong ToneCore được viết từ các nguồn công khai dưới đây. Khi thêm thuật toán mới, ghi nguồn vào đây trong cùng commit, kèm file mã nguồn dùng nó.

## Real-time audio & kiến trúc

| Nguồn | Dùng cho | File |
|---|---|---|
| R. Bencina, *Real-time audio programming 101: time waits for nothing*, 2011 (blog) | Quy tắc audio callback | toàn bộ `[RT]` |
| T. Doumler, các bài nói CppCon/ADC về lock-free cho audio | SPSC, atomic param | `core/spsc_ring.h`, `core/smoothed_param.h` |
| L. Lamport, *Specifying Concurrent Program Modules*, ACM TOPLAS 1983 | Hàng đợi SPSC không khóa | `core/spsc_ring.h` |
| Arm Architecture Reference Manual (FPCR.FZ); Intel SDM (MXCSR FTZ/DAZ) | Chống denormal | `core/denormal.h` |
| Android Developers – *Audio latency*, tài liệu Oboe/AAudio | Audio I/O Android (M1) | |
| Apple – *Audio Session Programming Guide*, *Core Audio Overview* | Audio I/O iOS (M1) | |

## Dò cao độ & chỉnh giọng (M2)

- P. McLeod, G. Wyvill, *A Smarter Way to Find Pitch*, ICMC 2005 — MPM.
- A. de Cheveigné, H. Kawahara, *YIN, a fundamental frequency estimator for speech and music*, JASA 2002.
- M. Mauch, S. Dixon, *pYIN: A fundamental frequency estimator using probabilistic threshold distributions*, ICASSP 2014.
- E. Moulines, F. Charpentier, *Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones*, Speech Communication 1990 — PSOLA.
- U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley 2011 — chương pitch shifting.

## Đổi tone beat (M4)

- J. Laroche, M. Dolson, *Improved phase vocoder time-scale modification of audio*, IEEE TSAP 1999.
- Z. Průša, N. Holighaus, *Phase vocoder done right* (PGHI), EUSIPCO 2017.
- A. Röbel, *A new approach to transient processing in the phase vocoder*, DAFx 2003.

## Dò tone, BPM, loudness (M3)

- C. Krumhansl, *Cognitive Foundations of Musical Pitch*, OUP 1990.
- D. Temperley, *Music and Probability*, MIT Press 2007.
- Á. Faraldo et al., *Key Estimation in Electronic Dance Music*, ECIR 2016 — profile EDMA.
- D. Fitzgerald, *Harmonic/Percussive Separation using Median Filtering*, DAFx 2010.
- C. Schörkhuber, A. Klapuri, *Constant-Q transform toolbox for music processing*, SMC 2010.
- C. Harte, M. Sandler, *Automatic chord identification using a quantised chromagram*, AES 2005 — ước lượng tuning.
- J. P. Bello et al., *A Tutorial on Onset Detection in Music Signals*, IEEE TSAP 2005.
- G. Percival, G. Tzanetakis, *Streamlined Tempo Estimation Based on Autocorrelation and Cross-correlation With Pulses*, IEEE/ACM TASLP 2014.
- D. Ellis, *Beat Tracking by Dynamic Programming*, JNMR 2007.
- ITU-R BS.1770-4, *Algorithms to measure audio programme loudness and true-peak audio level*.
- MIREX Audio Key Detection – quy tắc chấm điểm.

## Hiệu ứng (M5)

- R. Bristow-Johnson, *Cookbook formulae for audio EQ biquad filter coefficients* (Audio EQ Cookbook).
- V. Zavalishin, *The Art of VA Filter Design*, 2018 — SVF/TPT.
- D. Giannoulis, M. Massberg, J. Reiss, *Digital Dynamic Range Compressor Design – A Tutorial and Analysis*, JAES 2012.
- J.-M. Jot, A. Chaigne, *Digital delay networks for designing artificial reverberators*, AES 1991 — FDN.

## Fingerprint

- A. Wang, *An Industrial-Strength Audio Search Algorithm*, ISMIR 2003 — cần ghi vào `patent-review.md`.
