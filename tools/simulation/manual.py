"""Typed manual access to generated scalar simulation records."""
import math


class Fields:
    def __init__(self, names, timeout, pv_factory):
        self.timeout, self.factory = timeout, pv_factory
        self.pvs, self.readbacks, self.writers, self.limits = {}, {}, {}, {}
        self.writable = set()
        self.owned = []
        for name in names:
            self.pvs[name] = self.pv(name)

    def pv(self, name):
        pv = self.factory(name, auto_monitor=False)
        self.owned.append(pv)
        return pv

    def connect(self):
        for name, pv in list(self.pvs.items()):
            if not pv.wait_for_connection(timeout=min(.05, self.timeout)):
                del self.pvs[name]
                continue
            record = self.pv(name + '.RTYP')
            if not record.wait_for_connection(timeout=self.timeout):
                raise RuntimeError('cannot connect to ' + record.pvname)
            actual = record.get(as_string=True, use_monitor=False, timeout=self.timeout)
            output_types = {'mbboDirect': 'mbbiDirect', 'longout': 'longin',
                            'int64out': 'int64in', 'ao': 'ai'}
            input_types = set(output_types.values())
            if actual not in output_types and actual not in input_types:
                raise RuntimeError(f'{name}: unsupported native scalar record {actual!r}')
            injection = actual in output_types
            readback = self.pv(name + '-RB') if injection else pv
            if not readback.wait_for_connection(timeout=self.timeout) or not readback.read_access:
                raise RuntimeError('cannot read ' + readback.pvname)
            self.readbacks[name] = readback
            writer = pv if injection else self.pv(name + '-Set')
            if not writer.wait_for_connection(timeout=min(.05, self.timeout)) or not writer.write_access:
                continue  # Older IOC: keep readback without claiming a write reached the buffer.
            write_type = actual if injection else next(key for key, value in output_types.items() if value == actual)
            writer_record = self.pv(writer.pvname + '.RTYP')
            if not writer_record.wait_for_connection(timeout=self.timeout) or writer_record.get(
                    as_string=True, use_monitor=False, timeout=self.timeout) != write_type:
                raise RuntimeError('incompatible write record: ' + writer.pvname)
            if write_type == 'mbboDirect':
                bits_pv = self.pv(writer.pvname + '.NOBT')
                if not bits_pv.wait_for_connection(timeout=self.timeout):
                    raise RuntimeError('cannot read bit width: ' + writer.pvname)
                bits = bits_pv.get(use_monitor=False, timeout=self.timeout)
                if bits is None or int(bits) != bits or not 1 <= bits <= 32:
                    raise RuntimeError('invalid bit width: ' + writer.pvname)
                limits = (0, (1 << int(bits)) - 1)
            elif write_type == 'longout':
                limits = (-(1 << 31), (1 << 31) - 1)
            elif write_type == 'int64out':
                # Both signed values and exact U64 bit patterns are accepted.
                limits = (-(1 << 63), (1 << 64) - 1)
            else:
                limits = None
            self.limits[name] = limits
            self.writers[name] = writer
            self.writable.add(name)
        if not self.pvs:
            raise RuntimeError('no supported scalar simulation fields found')

    def read(self):
        values = {}
        for name, pv in self.readbacks.items():
            value = pv.get(use_monitor=False, timeout=self.timeout)
            if value is None or not math.isfinite(value):
                raise RuntimeError('cannot read scalar field: ' + pv.pvname)
            values[name] = value
        return values

    def write(self, name, value):
        if name not in self.writable:
            raise ValueError('field has no writable simulation record: ' + str(name))
        try:
            finite = math.isfinite(value)
        except (OverflowError, TypeError):
            finite = False
        if not finite:
            raise ValueError('value must be finite')
        limits = self.limits[name]
        if limits is not None:
            if int(value) != value or not limits[0] <= value <= limits[1]:
                raise ValueError(f'expected integer in [{limits[0]}, {limits[1]}]')
            value = int(value)
            if limits[1] == (1 << 64) - 1 and value >= 1 << 63:
                value -= 1 << 64
        pv = self.writers[name]
        if not pv.connected or pv.put(value, wait=True, timeout=self.timeout) != 1:
            raise RuntimeError('manual write failed: ' + pv.pvname)

    def snapshot(self):
        return {name: (value, name if name in self.writable else None)
                for name, value in self.read().items()}

    def close(self):
        for pv in self.owned:
            pv.disconnect()
