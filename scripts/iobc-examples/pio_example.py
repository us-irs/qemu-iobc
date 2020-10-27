#!/usr/bin/env python3
#
# Example for PIO peripheral simulation on the isis-obc board.
#
# Copyright (c) 2019-2020 KSat e.V. Stuttgart
#
# This work is licensed under the terms of the GNU GPL, version 2 or, at your
# option, any later version. See the COPYING file in the top-level directory.

import asyncio
import struct
import re
import json

QEMU_ADDR_QMP = '/tmp/qemu'
QEMU_ADDR_AT91_PIOA = '/tmp/qemu_at91_pioa'

IOX_CAT_PINSTATE = 0x01
IOX_CID_PINSTATE_ENABLE = 0x01
IOX_CID_PINSTATE_DISABLE = 0x02
IOX_CID_PINSTATE_OUT = 0x03
IOX_CID_PINSTATE_GET = 0x04


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


class Pio:
    """
    Connection to emulate GPIO/PIO devices for a given QEMU/At91 instance.
    The PIO contains GPIO 32 pins, the values of which are represented as 32
    bit bitflag.
    """

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
            self.proto = PioProtocol(self)

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

    async def wait_pin_change(self):
        """Wait for pin-state to change and return new value"""

        frame = await self.dataq.get()
        return struct.unpack('I', frame.data)[0]

    async def get_pin_state(self):
        """
        Query and return the current pin state. The values are returned as
        32 bit bitflag.
        """

        seq = self._send_new_frame(IOX_CAT_PINSTATE, IOX_CID_PINSTATE_GET)

        async with self.respc:
            while seq not in self.respd.keys():
                await self.respc.wait()

            resp = self.respd[seq]
            del self.respd[seq]

        return struct.unpack('I', resp.data)[0]

    def enable_pins(self, pins):
        """
        Enable the specified pins, setting their value to 1. The pins to
        enable are specified as 32 bit bitflag, with the bit of any pin to
        enable set to one. Multiple pins may be enabled at the same time.
        """

        self._send_new_frame(IOX_CAT_PINSTATE, IOX_CID_PINSTATE_ENABLE, struct.pack('I', pins))

    def disable_pins(self, pins):
        """
        Disable the specified pins, setting their value to 1. The pins to
        disable are specified as 32 bit bitflag, with the bit of any pin to
        disable set to one. Multiple pins may be disabled at the same time.
        """

        self._send_new_frame(IOX_CAT_PINSTATE, IOX_CID_PINSTATE_DISABLE, struct.pack('I', pins))


class PioProtocol(asyncio.Protocol):
    """The PIO transport protocoll implementation"""

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

            if frame.cat == IOX_CAT_PINSTATE and frame.id == IOX_CID_PINSTATE_OUT:
                self.conn.dataq.put_nowait(frame)
            elif frame.cat == IOX_CAT_PINSTATE and frame.id == IOX_CID_PINSTATE_GET:
                loop = asyncio.get_event_loop()
                loop.create_task(self._pinstate_get_response_received(frame))

    async def _pinstate_get_response_received(self, frame):
        async with self.conn.respc:
            self.conn.respd[frame.seq] = frame
            self.conn.respc.notify_all()


async def main():
    machine = QmpConnection()
    pio = Pio(QEMU_ADDR_AT91_PIOA)
    pin = 1 << 31       # the pin to toggle

    await machine.open()
    try:
        await pio.open()

        # Query the initial pin-state
        print(f"Initial pin state: 0x{await pio.get_pin_state():08x}")

        await machine.cont()

        # Toggle pin every second
        while True:
            # Enable pin, print new state
            pio.enable_pins(pin)
            print(f"Pin state: 0x{await pio.get_pin_state():08x}")

            await asyncio.sleep(1.0)

            # Disable pin, print new state
            pio.disable_pins(pin)
            print(f"Pin state: 0x{await pio.get_pin_state():08x}")

            await asyncio.sleep(1.0)

    finally:
        await machine.quit()
        pio.close()
        machine.close()


if __name__ == '__main__':
    asyncio.run(main())
