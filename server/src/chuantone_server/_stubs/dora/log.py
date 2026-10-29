"""dora.log tối thiểu cho demucs.pretrained / demucs.states."""


def fatal(*args, **kwargs):
    raise RuntimeError(" ".join(str(a) for a in args))


def bold(text):
    return text
