# Kết quả đo độ trễ round-trip

Chưa có số đo trên máy thật. Cách thu thập:

1. Cài app, mở **Đo độ trễ**, bấm **Đo** (loa ngoài, nơi yên tĩnh; hoặc dongle loopback cho đường tai nghe).
2. Bấm **Sao chép kết quả** và gửi dòng CSV cho nhóm phát triển.
3. Gộp: `python3 tools/latency/aggregate.py ketqua*.csv > docs/latency-results.md`
