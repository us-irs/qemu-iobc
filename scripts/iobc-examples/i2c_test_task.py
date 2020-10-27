#!/usr/bin/env python

"""
Example framework for testing the twiTestTask of the OBSW.

See usart_test_task.py for more information.
"""


import asyncio
import struct
import json
import re
import errno
import time

# Paths to Unix Domain Sockets used by the emulator
QEMU_ADDR_QMP = '/tmp/qemu'
QEMU_ADDR_AT91_TWI = '/tmp/qemu_at91_twi'

# Request/response category and command IDs
IOX_CAT_DATA = 0x01
IOX_CAT_FAULT = 0x02

IOX_CID_DATA_IN = 0x01
IOX_CID_DATA_OUT = 0x02
IOX_CID_CTRL_START = 0x03
IOX_CID_CTRL_STOP = 0x04

IOX_CID_FAULT_OVRE = 0x01
IOX_CID_FAULT_NACK = 0x02
IOX_CID_FAULT_ARBLST = 0x03


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


class I2cStatusException(Exception):
    """An exception returned by the TWI send command"""
    def __init__(self, errn, *args, **kwargs):
        Exception.__init__(self, f'TWI error: {errno.errorcode[errn]}')
        self.errno = errn   # a UNIX error code indicating the reason


class I2cStartFrame:
    def __init__(self, read, dadr, iadr):
        self.read = read != 0
        self.dadr = dadr
        self.iadr = iadr

    def __repr__(self):
        return f'{{ read: {self.read}, dadr: 0x{self.dadr:02x}, iadr: 0x{self.iadr:02x} }}'


class I2cSlave:
    """Connection to emulate a TWI/I2C device for a given QEMU/At91 instance"""

    def __init__(self, addr):
        self.addr = addr
        self.respd = dict()
        self.respc = asyncio.Condition()
        self.dataq = asyncio.Queue()
        self.transport = None
        self.proto = None
        self.seq = 0

    def _protocol(self):
        """The underlying transport protocol"""

        if self.proto is None:
            self.proto = I2cProtocol(self)

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

    async def read_frame(self):
        """Wait for a new data-frame and return it"""

        return await self.dataq.get()

    async def wait_start(self):
        """Wait for a new start frame from the I2C master"""

        while True:
            frame = await self.read_frame()

            if frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_CTRL_START:
                read = frame.data[0] & 0x80
                dadr = frame.data[0] & 0x7f
                iadr = frame.data[2] | (frame.data[3] << 8) | (frame.data[4] << 16)
                return I2cStartFrame(read, dadr, iadr)
            else:
                print(f"warning: discarding frame {frame}")

    async def wait_stop(self):
        """Wait for a stop frame from the I2C master"""

        while True:
            frame = await self.read_frame()

            if frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_CTRL_STOP:
                return
            else:
                print(f"warning: discarding frame {frame}")

    async def write(self, data):
        """
        Write data to the I2C master. Should only be called after a start
        frame has been received. Does not wait for a stop-frame.
        """

        seq = self._send_new_frame(IOX_CAT_DATA, IOX_CID_DATA_IN, data)

        async with self.respc:
            while seq not in self.respd.keys():
                await self.respc.wait()

            resp = self.respd[seq]
            del self.respd[seq]

        status = struct.unpack('I', resp.data)[0]
        if status != 0:
            raise I2cStatusException(status)

    async def read(self):
        """
        Wait for and read bytes sent by the I2C master until the master
        sends a stop frame. Should only be called after a start frame has
        been received.
        """

        buf = bytes()
        while True:
            frame = await self.read_frame()

            if frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_DATA_OUT:
                buf += frame.data
            elif frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_CTRL_STOP:
                return buf
            else:
                print(f"warning: unexpected frame {frame}")


class I2cProtocol(asyncio.Protocol):
    """The TWI/I2C transport protocoll implementation"""

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
                # data from CPU/board to device
                self.conn.dataq.put_nowait(frame)
            elif frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_CTRL_START:
                self.conn.dataq.put_nowait(frame)
            elif frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_CTRL_STOP:
                self.conn.dataq.put_nowait(frame)
            elif frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_DATA_IN:
                # response for data from device to CPU/board
                loop = asyncio.get_event_loop()
                loop.create_task(self._data_response_received(frame))

    async def _data_response_received(self, frame):
        async with self.conn.respc:
            self.conn.respd[frame.seq] = frame
            self.conn.respc.notify_all()


async def main():
    machine = QmpConnection()
    dev = I2cSlave(QEMU_ADDR_AT91_TWI)

    await machine.open()
    try:
        await dev.open()
        await machine.cont()

        while True:
            # Wait for start condition from master. We expect a write here.
            start = await dev.wait_start()
            assert start.read is False

            # Read data from master until stop condition
            data = await dev.read()
            print(f"READ[0x{start.dadr:02x}]:  {data}")

            # Wait for start condition from master. We expect a read here.
            start = await dev.wait_start()
            assert start.read is True

            # Write data to master.
            print(f"WRITE[0x{start.dadr:02x}]: {data}")
            await dev.write(data)

            # Wait for stop condition from master.
            await dev.wait_stop()

    finally:
        await machine.quit()

        dev.close()
        machine.close()


if __name__ == '__main__':
    asyncio.run(main())
