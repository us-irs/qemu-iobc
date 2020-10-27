#!/usr/bin/env python3
#
# Example for peripheral simulation for the USART test task on the isis-obc
# board.
#
# Copyright (c) 2019-2020 KSat e.V. Stuttgart
#
# This work is licensed under the terms of the GNU GPL, version 2 or, at your
# option, any later version. See the COPYING file in the top-level directory.

"""
Example framework for testing the USARTTestTask of the OBSW.

This file is intended to showcase how a USART device would be emulated in
python and connected to the QEMU emulator. The datastructures and functions in
this file should indicate what a simulation framework for the QEMU emulator
might look like.

Requirements:
  Python >= 3.7 (asyncio support)

Instructions:
  Run QEMU (modified for OBSW) via

  qemu-system-arm -M isis-obc -monitor stdio \
      -bios path/to/sourceobsw-at91sam9g20_ek-sdram.bin \
      -qmp unix:/tmp/qemu,server -S

  Then run this script.
"""


import asyncio
import struct
import json
import re
import errno

# Paths to Unix Domain Sockets used by the emulator
QEMU_ADDR_QMP = '/tmp/qemu'
QEMU_ADDR_AT91_USART0 = '/tmp/qemu_at91_usart0'
QEMU_ADDR_AT91_USART2 = '/tmp/qemu_at91_usart2'

# Request/response category and command IDs
IOX_CAT_DATA = 0x01
IOX_CAT_FAULT = 0x02

IOX_CID_DATA_IN = 0x01
IOX_CID_DATA_OUT = 0x02

IOX_CID_FAULT_OVRE = 0x01
IOX_CID_FAULT_FRAME = 0x02
IOX_CID_FAULT_PARE = 0x03
IOX_CID_FAULT_TIMEOUT = 0x04


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


class UsartStatusException(Exception):
    """An exception returned by the USART send command"""
    def __init__(self, errn, *args, **kwargs):
        Exception.__init__(self, f'USART error: {errno.errorcode[errn]}')
        self.errno = errn   # a UNIX error code indicating the reason


class Usart:
    """Connection to emulate a USART device for a given QEMU/At91 instance"""

    def __init__(self, addr):
        self.addr = addr
        self.respd = dict()
        self.respc = asyncio.Condition()
        self.dataq = asyncio.Queue()
        self.datab = bytes()
        self.transport = None
        self.proto = None
        self.seq = 0

    def _protocol(self):
        """The underlying transport protocol"""

        if self.proto is None:
            self.proto = UsartProtocol(self)

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

    async def write(self, data):
        """Write data (bytes) to the USART device"""

        seq = self._send_new_frame(IOX_CAT_DATA, IOX_CID_DATA_IN, data)

        async with self.respc:
            while seq not in self.respd.keys():
                await self.respc.wait()

            resp = self.respd[seq]
            del self.respd[seq]

        status = struct.unpack('I', resp.data)[0]
        if status != 0:
            raise UsartStatusException(status)

    async def read(self, n):
        """Wait for 'n' bytes to be received from the USART"""

        while len(self.datab) < n:
            frame = await self.dataq.get()
            self.datab += frame.data

        data, self.datab = self.datab[:n], self.datab[n:]
        return data

    def inject_overrun_error(self):
        """Inject an overrun error (set CSR_OVRE)"""
        self._send_new_frame(IOX_CAT_FAULT, IOX_CID_FAULT_OVRE)

    def inject_frame_error(self):
        """Inject a frame error (set CSR_FRAME)"""
        self._send_new_frame(IOX_CAT_FAULT, IOX_CID_FAULT_FRAME)

    def inject_parity_error(self):
        """Inject a parity error (set CSR_PARE)"""
        self._send_new_frame(IOX_CAT_FAULT, IOX_CID_FAULT_PARE)

    def inject_timeout_error(self):
        """Inject a timeout (set CSR_TIMEOUT)"""
        self._send_new_frame(IOX_CAT_FAULT, IOX_CID_FAULT_TIMEOUT)


class UsartProtocol(asyncio.Protocol):
    """The USART transport protocoll implementation"""

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
            elif frame.cat == IOX_CAT_DATA and frame.id == IOX_CID_DATA_IN:
                # response for data from device to CPU/board
                loop = asyncio.get_event_loop()
                loop.create_task(self._data_response_received(frame))

    async def _data_response_received(self, frame):
        async with self.conn.respc:
            self.conn.respd[frame.seq] = frame
            self.conn.respc.notify_all()


async def main():
    # Instantiate the connection classes we need. The connections are not
    # opened automatically. Use '.open()' or 'async with machine as m'.
    machine = QmpConnection()
    usart = Usart(QEMU_ADDR_AT91_USART0)

    # Open a connection to the QEMU emulator via QMP.
    await machine.open()
    try:
        # Open the connection to the USART.
        await usart.open()

        # Signal the emulator that it can continue/start emulation. Initially,
        # the emulator should be paused by providing the '-S' option on the
        # command line when starting it. This allows the framework to set
        # itself up before actually starting the emulation, in this case by
        # connection to the USART port.
        await machine.cont()

        # Re-try until the USART has been set up by the OBSW. The USART will
        # return ENXIO until the receiver has been enabled. This may take a
        # bit. We just send it data until it indicates success.
        # TODO: Should we introduce an API to query the rx/tx-enabled status?
        while True:
            try:
                await usart.write(b'abcd')
                print(await usart.read(6))
                break
            except UsartStatusException as e:
                if e.errno == errno.ENXIO:
                    await asyncio.sleep(0.25)
                else:
                    raise e

        # Provide data for the USARTTestTask
        for _ in range(4):
            await usart.write(b'abcd')
            print(await usart.read(6))

    finally:
        # Shutdown and close the emulator. Useful for automatic cleanup. We
        # don't want to leave any unsupervised QEMU instances running.
        await machine.quit()

        # Close connections to USART and machine, if they are still open. The
        # quit command above should automatically close those, but let's be
        # explicit here.
        usart.close()
        machine.close()


if __name__ == '__main__':
    asyncio.run(main())
