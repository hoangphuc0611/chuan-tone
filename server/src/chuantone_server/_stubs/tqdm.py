"""Thay cho tqdm (MIT + MPL-2.0, ngoài danh sách license cho phép). Khi có trên sys.path, demucs.apply
và torch.hub (tải trọng số) dùng nó để in tiến độ → bản này không in gì: đi qua iterable, nhận
update() / close() / with."""


class tqdm:  # noqa: N801 — giữ đúng tên của tqdm
    def __init__(self, iterable=None, *args, **kwargs):
        self.iterable = iterable
        self.n = 0

    def __iter__(self):
        return iter(self.iterable if self.iterable is not None else ())

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def update(self, n=1):
        self.n += n

    def close(self):
        pass
