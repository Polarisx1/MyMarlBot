import logging

log = logging.getLogger("rlmarlbot.memory")
log.setLevel(logging.DEBUG)

# Try to import the native C++ extension
try:
    import memory_writer
    _native = memory_writer
    log.info("memory_writer.pyd loaded successfully.")
except ImportError as e:
    log.warning("Could not import memory_writer.pyd: %s", e)
    _native = None


class MemoryWriter:
    def __init__(self, dry_run: bool = False):
        """
        :param dry_run: if True, do not attempt to write into game memory
        """
        self.dry_run = dry_run or (_native is None)
        if self.dry_run:
            log.warning("MemoryWriter running in DRY-RUN mode.")

    def write_inputs(self, inputs: dict):
        """
        Write controller inputs into Rocket League memory.
        Inputs is a dict like:
          {"throttle": 1.0, "steer": 0.0, "jump": 1, "boost": 0}
        """
        if self.dry_run:
            log.debug("[DRY-RUN] Would write inputs: %s", inputs)
            return

        try:
            _native.write(inputs)
            log.debug("Wrote inputs: %s", inputs)
        except Exception as e:
            log.error("Memory write failed: %s", e)
            log.warning("Switching to DRY-RUN mode.")
            self.dry_run = True
