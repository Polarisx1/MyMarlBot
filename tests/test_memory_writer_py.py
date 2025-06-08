import os
import sys
import pytest

if os.name != "nt":
    pytest.skip("Windows only tests", allow_module_level=True)

from rlmarlbot.memory_writer_py import MemoryWriter


def test_open_process_partial_match():
    import psutil

    current = psutil.Process(os.getpid())
    exe = os.path.basename(current.exe())
    if len(exe) < 3:
        pytest.skip("Executable name too short for substring test")

    partial = exe[:-1]  # remove last char to force substring search
    writer = MemoryWriter()
    assert writer.open_process(partial) is True
    writer.stop()

