#!/usr/bin/env python
import asyncio
import struct
import json
import re
import errno


QEMU_ADDR_QMP = '/tmp/qemu'
QEMU_ADDR_AT91_SPI0 = '/tmp/qemu_at91_spi0'


IOX_CAT_DATA = 0x01
IOX_CAT_FAULT = 0x02

IOX_CID_DATA_IN = 0x01
IOX_CID_DATA_OUT = 0x02

IOX_CID_FAULT_MODF = 0x01
IOX_CID_FAULT_OVRES = 0x02


class QmpException(Exception):
    """An exception caused by the QML/QEMU as response to a failed command"""

    def __init__(self, ret, *args, **kwargs):
        Exception.__init__(self, f'QMP error: {ret}')
        self.ret = ret  # the 'return' structure provided by QEMU/QML


class QmpConnection:
    """A connection to a QEMU machine via QMP"""

    def __init__(self, addr=QEMU_ADDR_QMP):
        self.transport = None
        self.addr = addr
        self.dataq = asyncio.Queue()
        self.initq = asyncio.Queue()
        self.proto = None

    def _protocol(self):
        """The underlying transport protocol"""

        if self.proto is None:
            self.proto = QmpProtocol(self)

        return self.proto

    async def _wait_check_return(self):
        """
        Wait for the status return of a command and raise an exception if it
        indicates a failure
        """

        resp = await self.dataq.get()
        if resp['return']:
            raise QmpException(resp['return'])

    async def open(self):
        """
        Open this connection. Connect to the machine ensure that the
        connection is ready to use after this call.
        """

        loop = asyncio.get_event_loop()
        await loop.create_unix_connection(self._protocol, self.addr)

        # wait for initial capabilities and version
        init = await self.initq.get()
        print(init)

        # negotioate capabilities
        cmd = '{ "execute": "qmp_capabilities" }'
        self.transport.write(bytes(cmd, 'utf-8'))
        await self._wait_check_return()

        return self

    def close(self):
        """Close this connection"""

        if self.transport is not None:
            self.transport.close()
            self.transport = None
            self.proto = None

    async def __aenter__(self):
        await self.open()
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()

    async def cont(self):
        """Continue machine execution if it has been paused"""

        cmd = '{ "execute": "cont" }'
        self.transport.write(bytes(cmd, 'utf-8'))
        await self._wait_check_return()

    async def stop(self):
        """Stop/pause machine execution"""

        cmd = '{ "execute": "stop" }'
        self.transport.write(bytes(cmd, 'utf-8'))
        await self._wait_check_return()

    async def quit(self):
        """
        Quit the emulation. This causes the emulator to (non-gracefully)
        shut down and close.
        """

        cmd = '{ "execute": "quit" }'
        self.transport.write(bytes(cmd, 'utf-8'))
        await self._wait_check_return()


class QmpProtocol(asyncio.Protocol):
    """The QMP transport protocoll implementation"""

    def __init__(self, conn):
        self.conn = conn

    def connection_made(self, transport):
        self.conn.transport = transport

    def connection_lost(self, exc):
        self.conn.transport = None
        self.conn.proto = None

    def data_received(self, data):
        data = str(data, 'utf-8')
        decoder = json.JSONDecoder()
        nows = re.compile(r'[^\s]')

        pos = 0
        while True:
            match = nows.search(data, pos)
            if not match:
                return

            pos = match.start()
            obj, pos = decoder.raw_decode(data, pos)

            if 'return' in obj:
                self.conn.dataq.put_nowait(obj)
            elif 'QMP' in obj:
                self.conn.initq.put_nowait(obj)
            elif 'event' in obj:
                pass
            else:
                print("qmp:", obj)


class DataFrame:
    """
    Basic protocol unit for communication via the IOX API introduced for
    external device emulation
    """

    def __init__(self, seq, cat, id, data=None):
        self.seq = seq
        self.cat = cat
        self.id = id
        self.data = data

    def bytes(self):
        """Convert this protocol unit to raw bytes"""
        data = self.data if self.data is not None else []
        return bytes([self.seq, self.cat, self.id, len(data)]) + bytes(data)

    def __repr__(self):
        return f'{{ seq: 0x{self.seq:02x}, cat: 0x{self.cat:02x}, id: 0x{self.id:02x}, data: {self.data} }}'


def parse_dataframes(buf):
    """Parse a variable number of DataFrames from the given byte buffer"""

    while len(buf) >= 4 and len(buf) >= 4 + buf[3]:
        frame = DataFrame(buf[0], buf[1], buf[2], buf[4:4+buf[3]])
        buf = buf[4+buf[3]:]
        yield buf, frame

    return buf, None


class SpiStatusException(Exception):
    """An exception returned by the SPI send command"""
    def __init__(self, errn, *args, **kwargs):
        Exception.__init__(self, f'SPI error: {errno.errorcode[errn]}')
        self.errno = errn   # a UNIX error code indicating the reason


class SpiUnit():
    """
    The data transmission unit of the SPI interface. This structure contains
    information about the chip the unit is sent to (pcs), the number of
    valid bits the transmitted character contains, and the transmitted
    character itself.
    """
    def __init__(self, pcs, bits, char):
        self.pcs = pcs      # peripheral chip select
        self.bits = bits    # number of bits in char
        self.char = char    # acutal transmitted character

    @classmethod
    def from_bytes(cls, data):
        """Parse one SpiUnit from bytes"""
        unit = struct.unpack('<I', data)[0]
        return cls(unit >> 24 & 0x0F, ((unit >> 16) & 0x0F) + 8, unit & 0xFFFF)

    @classmethod
    def parse_multiple(cls, data):
        """Parse multiple SpiUnits from bytes"""
        return [cls.from_bytes(data[i:i+4]) for i in range(0, len(data), 4)]

    @staticmethod
    def multiple_to_bytes(units):
        """Convert multiple SpiUnits to bytes"""
        data = bytes()
        for u in units:
            data += u.to_bytes()

        return data

    def __repr__(self):
        return f'{{ pcs: {self.pcs}, bits: {self.bits}, char: 0x{self.char} }}'

    def to_bytes(self):
        """Convert this SpiUnit to bytes"""
        unit = ((self.pcs & 0xF) << 24) | (((self.bits - 8) & 0x0F) << 16) | (self.char & 0xFFFF)
        return struct.pack('<I', unit)


class Spi:
    """Connection to emulate a SPI device for a given QEMU/At91 instance"""

    def __init__(self, addr):
        self.addr = addr
        self.dataq = asyncio.Queue()
        self.transport = None
        self.proto = None
        self.seq = 0

    def _protocol(self):
        """The underlying transport protocol"""

        if self.proto is None:
            self.proto = SpiProtocol(self)

        return self.proto

    async def open(self):
        """Open this connection"""

        loop = asyncio.get_event_loop()
        await loop.create_unix_connection(self._protocol, self.addr)
        return self

    def close(self):
        """Close this connection"""

        if self.transport is not None:
            self.transport.close()
            self.transport = None
            self.proto = None

    async def __aenter__(self):
        await self.open()
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()

    def _send_new_frame(self, cat, cid, data=None):
        """
        Send a DataFrame with the given parameters and auto-increase the
        sequence counter. Return its sequence number.
        """
        self.seq = (self.seq + 1) & 0x7f

        frame = DataFrame(self.seq, cat, cid, data)
        self.transport.write(frame.bytes())

        return frame.seq

    async def read_units(self):
        frame = await self.dataq.get()
        return SpiUnit.parse_multiple(frame.data)

    def write_units(self, units):
        data = SpiUnit.multiple_to_bytes(units)
        self._send_new_frame(IOX_CAT_DATA, IOX_CID_DATA_IN, data)

    def inject_overrun_error(self):
        """Inject an overrun error (set SR_OVRES)"""
        self._send_new_frame(IOX_CAT_FAULT, IOX_CID_FAULT_OVRES)

    def inject_mode_fault(self):
        """Inject a mode fault (set SR_MODF)"""
        self._send_new_frame(IOX_CAT_FAULT, IOX_CID_FAULT_MODF)


class SpiProtocol(asyncio.Protocol):
    """The SPI transport protocoll implementation"""

    def __init__(self, conn):
        self.conn = conn
        self.buf = bytes()

    def connection_made(self, transport):
        self.conn.transport = transport

    def connection_lost(self, exc):
        self.conn.transport = None
        self.conn.proto = None

    def data_received(self, data):
        self.buf += data

        for buf, frame in parse_dataframes(self.buf):
            self.buf = buf

            if frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_DATA_OUT:
                self.conn.dataq.put_nowait(frame)


async def main():
    machine = QmpConnection()
    spi = Spi(QEMU_ADDR_AT91_SPI0)

    await machine.open()
    try:
        await spi.open()
        await machine.cont()

        while True:
            # Read as many units as we can.
            units = await spi.read_units()

            # Extract the received characters.
            chars = [chr(x.char) for x in units]
            print(f"Received: \"{''.join(chars)}\"")

            # Convert the characters to upper case and package them as units.
            # Important: Use the same peripheral chip select (PCS) and number
            # of bits in the same order as we've received them. SPI is a full
            # duplex protocol, meaning if some unit has been sent in time-step
            # t, another unit has to be recieved simultaneously. Here we
            # simulate this by firs recieveing data from some N time-steps
            # sent by the onboard SPI, for which we then have to send back the
            # data it receives during these N time-steps. Execution of the
            # emulator will block until we have done this.
            for i in range(len(units)):
                units[i].char = ord(chars[i].upper())

            # Actually send the data.
            spi.write_units(units)

    finally:
        await machine.quit()
        spi.close()
        machine.close()


if __name__ == '__main__':
    asyncio.run(main())
