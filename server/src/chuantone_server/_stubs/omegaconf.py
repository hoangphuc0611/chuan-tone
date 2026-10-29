"""Thay cho omegaconf: demucs.states chỉ dùng OmegaConf khi lưu model lúc huấn luyện."""


class OmegaConf:
    @staticmethod
    def to_container(*args, **kwargs):
        raise NotImplementedError("omegaconf không có trong server (chỉ cần khi huấn luyện)")
