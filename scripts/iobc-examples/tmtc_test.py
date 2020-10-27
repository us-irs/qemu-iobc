#!/usr/bin/env python
import asyncio
import struct
import json
import re
import errno
import crcmod


class PUSTelecommand:
    headerSize = 6

    def __init__(self, service: int, subservice: int, SSC=0, data=bytearray([]), sourceId=0, version=0,
                 packetType=1, dataFieldHeaderFlag=1, apid=0x73, length=0):
        self.packetId = [0x0, 0x0]
        self.packetId[0] = ((version << 5) & 0xE0) | ((packetType & 0x01) << 4) | \
                           ((dataFieldHeaderFlag & 0x01) << 3) | ((apid & 0x700) >> 8)
        self.packetId[1] = apid & 0xFF
        self.ssc = SSC
        self.psc = (SSC & 0x3FFF) | (0xC0 << 8)
        self.length = length
        self.pusVersionAndAckByte = 0b00011111
        self.service = service
        self.subservice = subservice
        self.sourceId = sourceId
        self.data = data
        # print("Apid:" + str(self.apid))

    def getLength(self):
        return 4 + len(self.data) + 1

    def pack(self):
        dataToPack = bytearray()
        dataToPack.append(self.packetId[0])
        dataToPack.append(self.packetId[1])
        dataToPack.append((self.psc & 0xFF00) >> 8)
        dataToPack.append(self.psc & 0xFF)
        length = self.getLength()
        dataToPack.append((length & 0xFF00) >> 8)
        dataToPack.append(length & 0xFF)
        dataToPack.append(self.pusVersionAndAckByte)
        dataToPack.append(self.service)
        dataToPack.append(self.subservice)
        dataToPack.append(self.sourceId)
        dataToPack += self.data
        crc_func = crcmod.mkCrcFun(0x11021, rev=False, initCrc=0xFFFF, xorOut=0x0000)
        crc = crc_func(dataToPack)

        dataToPack.append((crc & 0xFF00) >> 8)
        dataToPack.append(crc & 0xFF)
        return dataToPack

    def packInformation(self):
        tcInformation = {
            "service": self.service,
            "subservice": self.subservice,
            "ssc": self.ssc,
            "packetId": self.packetId,
            "data": self.data
        }
        return tcInformation

    def packCommandTuple(self):
        commandTuple = (self.packInformation(), self.pack())
        return commandTuple


# Takes pusPackets, removes current Packet Error Control, calculates new CRC (16 bits at packet end) and
# adds it as correct Packet Error Control Code. Reference: ECSS-E70-41A p. 207-212
def generatePacketCRC(TCPacket):
    crc_func = crcmod.mkCrcFun(0x11021, rev=False, initCrc=0xFFFF, xorOut=0x0000)
    crc = crc_func(bytearray(TCPacket[0:len(TCPacket) - 2]))
    TCPacket[len(TCPacket) - 2] = (crc & 0xFF00) >> 8
    TCPacket[len(TCPacket) - 1] = crc & 0xFF


def generateCRC(data):
    dataWithCRC = bytearray()
    dataWithCRC += data
    crc_func = crcmod.mkCrcFun(0x11021, rev=False, initCrc=0xFFFF, xorOut=0x0000)
    crc = crc_func(data)
    dataWithCRC.append((crc & 0xFF00) >> 8)
    dataWithCRC.append(crc & 0xFF)
    return dataWithCRC





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

    async def _write(self, data):
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

    async def write(self, data):
        while len(data) > 0:
            n = min((len(data), 0xFF))
            head, data = data[:n], data[n:]

            while True:
                try:
                    await self._write(head)
                except UsartStatusException as e:
                    if e.errno == errno.EAGAIN:
                        await asyncio.sleep(0.001)
                        continue
                    else:
                        raise e

                break

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

    def inject_timeout(self):
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
    usart = Usart(QEMU_ADDR_AT91_USART0)
    async with usart as usart:

        packet = PUSTelecommand(17, 1)
        packet = packet.pack()
        print(len(packet))

        await usart.write(packet)
        usart.inject_timeout()

        print(await usart.read(12))
        print(await usart.read(100))
        print("test")


if __name__ == '__main__':
    asyncio.run(main())
