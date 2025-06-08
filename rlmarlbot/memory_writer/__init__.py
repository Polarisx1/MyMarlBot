try:
    from .memory_writer import MemoryWriter
except Exception:
    from ..memory_writer_py import MemoryWriter

__all__ = ["MemoryWriter"]
