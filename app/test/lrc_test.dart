import 'dart:convert';

import 'package:chuantone/data/lrc.dart';
import 'package:flutter_test/flutter_test.dart';

Duration _ms(int ms) => Duration(milliseconds: ms);

void main() {
  group('đọc LRC', () {
    test(
      'mốc dòng, thẻ tên bài / ca sĩ, dòng rỗng giữ lại, dòng rác bỏ qua',
      () {
        final l = parseLrc('''
[ti:Duyên Phận]
[ar: Ca sĩ A ]
[by:ai đó]
dòng không có mốc
[00:12.30]Quê em mùa nước lũ
[00:15.5]Câu thứ hai
[00:20]
[01:02.345]Câu cuối
''');
        expect(l.title, 'Duyên Phận');
        expect(l.artist, 'Ca sĩ A');
        expect(l.lines.map((x) => x.start), [
          _ms(12300),
          _ms(15500),
          _ms(20000),
          _ms(62345),
        ]);
        expect(l.lines.map((x) => x.text), [
          'Quê em mùa nước lũ',
          'Câu thứ hai',
          '',
          'Câu cuối',
        ]);
        expect(l.lines.first.words, isEmpty);
      },
    );

    test(
      'nhiều mốc một dòng (điệp khúc), xếp theo thời gian, cùng mốc giữ thứ tự',
      () {
        final l = parseLrc(
          '[00:30.00][01:30.00]Điệp khúc\n[00:10.00]Mở đầu\n[00:30.00]Bè\n',
        );
        expect(l.lines.map((x) => '${x.start.inSeconds} ${x.text}'), [
          '10 Mở đầu',
          '30 Điệp khúc',
          '30 Bè',
          '90 Điệp khúc',
        ]);
      },
    );

    test('thẻ offset: dương = lời sớm hơn; không âm', () {
      final l = parseLrc('[offset:+500]\n[00:00.20]A\n[00:02.00]B\n');
      expect(l.lines.map((x) => x.start), [Duration.zero, _ms(1500)]);
      final m = parseLrc('[offset:-250]\n[00:02.00]B\n');
      expect(m.lines.single.start, _ms(2250));
    });

    test('mốc từng chữ <mm:ss.xx>, mốc cuối là lúc hết chữ cuối', () {
      final l = parseLrc(
        '[00:01.00]<00:01.00>Đô <00:02.00>Rê <00:03.00>Mi<00:04.50>\n',
      );
      final line = l.lines.single;
      expect(line.text, 'Đô Rê Mi');
      expect(line.words.map((w) => (w.start, w.offset)), [
        (_ms(1000), 0),
        (_ms(2000), 3),
        (_ms(3000), 6),
      ]);
      expect(line.end, _ms(4500));
    });

    test('mốc chữ dạng [mm:ss] giữa dòng; điệp khúc dời mốc chữ theo', () {
      final l = parseLrc('[00:10.00][00:40.00]Một [00:11.00]hai\n');
      expect(l.lines, hasLength(2));
      expect(l.lines[0].text, 'Một hai');
      expect(l.lines[0].words.map((w) => w.start), [_ms(10000), _ms(11000)]);
      expect(l.lines[1].words.map((w) => w.start), [_ms(40000), _ms(41000)]);
    });

    test('BOM, CRLF, UTF-16 có BOM, byte hỏng', () {
      const src = '[ti:Lạc Trôi]\r\n[00:01.00]Người theo hương hoa\r\n';
      expect(
        decodeLrc([0xEF, 0xBB, 0xBF, ...utf8.encode(src)]).title,
        'Lạc Trôi',
      );
      final le = <int>[0xFF, 0xFE];
      final be = <int>[0xFE, 0xFF];
      for (final u in src.codeUnits) {
        le.addAll([u & 0xFF, u >> 8]);
        be.addAll([u >> 8, u & 0xFF]);
      }
      expect(decodeLrc(le).lines.single.text, 'Người theo hương hoa');
      expect(decodeLrc(be).lines.single.text, 'Người theo hương hoa');
      final bad = [...utf8.encode('[00:01.00]A'), 0xC3, 0x28];
      expect(decodeLrc(bad).lines.single.text, startsWith('A'));
    });

    test('không có mốc nào → lỗi định dạng', () {
      expect(
        () => parseLrc('Lời bài hát\nkhông có mốc'),
        throwsFormatException,
      );
      expect(() => parseLrc('[ti:Chỉ có tên]'), throwsFormatException);
      expect(() => parseLrc(''), throwsFormatException);
    });
  });

  group('dòng đang hát + tô màu', () {
    final l = parseLrc('''
[00:02.00]Câu một dài
[00:06.00]<00:06.00>Đô <00:07.00>Rê <00:08.00>Mi<00:09.00>
[00:30.00]Sau đoạn nhạc dạo
''');

    test('dòng theo thời điểm', () {
      expect(l.indexAt(Duration.zero), -1);
      expect(l.indexAt(_ms(1999)), -1);
      expect(l.indexAt(_ms(2000)), 0);
      expect(l.indexAt(_ms(5999)), 0);
      expect(l.indexAt(_ms(6000)), 1);
      expect(l.indexAt(const Duration(minutes: 5)), 2);
    });

    test('chỉ có mốc dòng: tô dần tới dòng sau', () {
      // "Câu một dài" 11 ký tự, 4 s tới dòng sau (giới hạn 0,5 + 0,35 × 11 = 4,35 s).
      expect(l.sungChars(0, _ms(2000)), 0);
      expect(l.sungChars(0, _ms(4000)), 6); // nửa đường → 5,5 → 6
      expect(l.sungChars(0, _ms(6000)), 11);
    });

    test('dòng trước đoạn nhạc dạo dài vẫn tô xong theo độ dài câu', () {
      // "Sau đoạn nhạc dạo" 17 ký tự → xong sau 0,5 + 0,35 × 17 = 6,45 s dù không có dòng sau.
      expect(l.sungChars(2, _ms(30000 + 6450)), 17);
      expect(l.sungChars(2, _ms(30000 + 3225)), lessThan(17));
    });

    test('mốc từng chữ: chữ đã qua tô hết, chữ đang hát tô dần', () {
      expect(l.sungChars(1, _ms(6000)), 0);
      expect(l.sungChars(1, _ms(6500)), 2); // giữa "Đô " (3 ký tự) → 1,5 → 2
      expect(l.sungChars(1, _ms(7000)), 3);
      expect(l.sungChars(1, _ms(8500)), 7); // giữa "Mi", hết lúc 9 s
      expect(l.sungChars(1, _ms(9000)), 8);
      expect(l.sungChars(5, _ms(9000)), 0);
    });

    test('không cắt giữa chữ cái và dấu rời', () {
      // "Mỹ" viết rời: M + y + dấu ngã (U+0303).
      final t = parseLrc('[00:00.00]Mỹ [00:01.00]\n');
      expect(t.lines.single.text, 'Mỹ');
      final n = t.sungChars(0, _ms(600));
      expect(n == 3 || n <= 1, isTrue);
    });

    test('nhãn lệch lời', () {
      expect(lyricsOffsetLabel(0), 'Lời đúng như file');
      expect(lyricsOffsetLabel(500), 'Lời sớm hơn 0,5 s');
      expect(lyricsOffsetLabel(-1000), 'Lời muộn hơn 1 s');
      expect(lyricsOffsetLabel(1500), 'Lời sớm hơn 1,5 s');
    });
  });
}
