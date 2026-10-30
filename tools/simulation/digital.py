"""Manual simulated digital I/O writes and readback; no auto loopback or forcing."""
import queue
import time


# Explicit layouts from ecmccfg's EX1004/1008/1016 and EX2004/2008 scripts.
LAYOUTS = {
    'EL1004': ('input', 4), 'EL1008': ('input', 8),
    'EL1808': ('input', 8), 'EL1809': ('input', 16),
    'EL2004': ('output', 4), 'EL2008': ('output', 8),
    'EL2808': ('output', 8),
}


class Terminal:
    def __init__(self, prefix, slave, timeout, pv_factory):
        self.direction, count = LAYOUTS[slave.hardware]
        self.timeout = timeout
        base = f'{prefix}ec{slave.master}:s{slave.position}:binary{self.direction.title()}'
        self.pvs = {f'{channel:02}': pv_factory(base + f'{channel:02}', auto_monitor=False)
                    for channel in range(1, count + 1)}
        self.types = {channel: pv_factory(pv.pvname + '.RTYP', auto_monitor=False)
                      for channel, pv in self.pvs.items()}
        # Input command records are output records; observe the actual buffer through -RB.
        self.readbacks = {channel: pv_factory(pv.pvname + '-RB', auto_monitor=False)
                          for channel, pv in self.pvs.items()} if self.direction == 'input' else self.pvs
        self.writers = self.pvs if self.direction == 'input' else {
            channel: pv_factory(pv.pvname + '-Set', auto_monitor=False) for channel, pv in self.pvs.items()}
        self.write_types = self.types if self.direction == 'input' else {
            channel: pv_factory(pv.pvname + '.RTYP', auto_monitor=False) for channel, pv in self.writers.items()}
        self.writable = set()

    def connect(self):
        self.writable.clear()
        expected = 'mbboDirect' if self.direction == 'input' else 'mbbiDirect'
        for channel, pv in self.pvs.items():
            record, readback = self.types[channel], self.readbacks[channel]
            for item in (pv, record, readback):
                if not item.wait_for_connection(timeout=self.timeout):
                    raise RuntimeError('cannot connect to ' + item.pvname)
            actual = record.get(as_string=True, use_monitor=False, timeout=self.timeout)
            if actual != expected:
                raise RuntimeError(f'{pv.pvname}: expected {expected}, got {actual!r}')
            if not readback.read_access or (self.direction == 'input' and not pv.write_access):
                raise RuntimeError('missing access to ' + pv.pvname)
            writer, write_type = self.writers[channel], self.write_types[channel]
            if writer.wait_for_connection(timeout=self.timeout):
                if not write_type.wait_for_connection(timeout=self.timeout):
                    raise RuntimeError('cannot connect to ' + write_type.pvname)
                actual = write_type.get(as_string=True, use_monitor=False, timeout=self.timeout)
                if actual != 'mbboDirect':
                    raise RuntimeError(f'{writer.pvname}: expected mbboDirect, got {actual!r}')
                if writer.write_access:
                    self.writable.add(channel)

    def read(self):
        values = {}
        for channel, pv in self.readbacks.items():
            value = pv.get(use_monitor=False, timeout=self.timeout)
            if value not in (0, 1):
                raise RuntimeError('cannot read digital bit: ' + pv.pvname)
            values[channel] = int(value)
        return values

    def write(self, channel, value):
        if channel not in self.writable or value not in (0, 1):
            raise ValueError('only writable digital channels accept 0 or 1')
        pv = self.writers[channel]
        if not pv.connected or pv.put(value, wait=True, timeout=self.timeout) != 1:
            raise RuntimeError('digital write failed: ' + pv.pvname)

    def close(self):
        for pv in list(self.pvs.values()) + list(self.types.values()):
            pv.disconnect()
        if self.direction == 'input':
            for pv in self.readbacks.values():
                pv.disconnect()
        else:
            for pv in list(self.writers.values()) + list(self.write_types.values()):
                pv.disconnect()


def run(io, path, stop, commands=None, report=None, duration=None):
    started = time.monotonic()
    write_error = None
    while not stop.is_set() and (duration is None or time.monotonic() - started < duration):
        # Bound each batch so a flood of clicks cannot starve readback or shutdown.
        for _ in range(32):
            if commands is None:
                break
            try:
                channel, value = commands.get_nowait()
            except queue.Empty:
                break
            try:
                io.write(channel, value)
                write_error = None
            except ValueError as error:
                write_error = str(error)
            except RuntimeError as error:
                if report:
                    report(path, 'write failed: ' + str(error), {})
                raise
        values = io.read()
        if report:
            fields = {io.pvs[channel].pvname: (value, channel if channel in io.writable else None)
                      for channel, value in values.items()}
            status = 'manual ' + io.direction + 's' if io.writable else 'read-only (no writable -Set PVs)'
            if write_error:
                status = 'write rejected: ' + write_error
            if getattr(io, 'notice', ''):
                status += '; ' + io.notice
            report(path, status, fields)
        stop.wait(.1)
