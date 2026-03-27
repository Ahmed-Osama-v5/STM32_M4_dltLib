#!/usr/bin/env python3
"""
dlt_integration_test.py
PC-side integration test harness for the DLT MCU library.

Tests:
  T01 — Receive at least one valid DLT log frame after boot
  T02 — Set log level for MOTR/CTRL → verify response
  T03 — Set default log level → verify response
  T04 — Get log info → verify NOT_SUPPORTED response
  T05 — Verify MCNT increments (no gaps)
  T06 — Verify status frame arrives within STATUS_INTERVAL + margin
  T07 — Set level to OFF → verify no log frames for 2s
  T08 — Restore level → verify frames resume

Usage:
    python dlt_integration_test.py --port /dev/ttyUSB0 --baud 1000000
"""

import serial
import struct
import time
import argparse
import sys
from dataclasses import dataclass, field
from typing import Optional, List

# ── DLT Constants ─────────────────────────────────────────────────────────────

DLT_SERIAL_HDR      = b'DLS\x01'
DLT_HTYP_STANDARD   = 0x27
DLT_STD_HDR_LEN     = 12
DLT_EXT_HDR_LEN     = 10

DLT_SVC_SET_LOG_LEVEL     = 0x01
DLT_SVC_GET_LOG_INFO      = 0x03
DLT_SVC_SET_DEFAULT_LEVEL = 0x11

DLT_CTRL_OK            = 0x00
DLT_CTRL_NOT_SUPPORTED = 0x01

LEVEL_NAMES = {0:'OFF',1:'FATAL',2:'ERROR',3:'WARN',4:'INFO',5:'DEBUG',6:'VERBOSE'}

# ── Frame Dataclass ────────────────────────────────────────────────────────────

@dataclass
class DltFrame:
    htyp:      int
    mcnt:      int
    length:    int
    timestamp: float       # converted to ms
    ecu_id:    str
    msin:      int
    noar:      int
    app_id:    str
    ctx_id:    str
    payload:   bytes
    rx_time:   float = field(default_factory=time.monotonic)

    @property
    def level(self) -> int:
        return (self.msin >> 4) & 0xF

    @property
    def mstp(self) -> int:
        return (self.msin >> 1) & 0x7

    @property
    def msg_id(self) -> Optional[int]:
        if len(self.payload) >= 4:
            return struct.unpack_from('<I', self.payload, 0)[0]
        return None

    @property
    def is_control_response(self) -> bool:
        return self.mstp == 0x03  # CONTROL

    @property
    def ctrl_status(self) -> Optional[int]:
        if self.is_control_response and len(self.payload) >= 5:
            return self.payload[4]
        return None

    @property
    def ctrl_svc_id(self) -> Optional[int]:
        if self.is_control_response and len(self.payload) >= 4:
            return struct.unpack_from('<I', self.payload, 0)[0]
        return None

    def __str__(self):
        ts_ms = self.timestamp * 0.1
        lvl   = LEVEL_NAMES.get(self.level, '?')
        if self.is_control_response:
            svc  = f"0x{self.ctrl_svc_id:08X}" if self.ctrl_svc_id is not None else "?"
            stat = {0:"OK",1:"NOT_SUPPORTED",2:"ERROR"}.get(self.ctrl_status, "?")
            return (f"[CTRL_RESP] {self.ecu_id} {self.app_id}/{self.ctx_id} "
                    f"svc={svc} status={stat} mcnt={self.mcnt}")
        return (f"[{lvl:7s}] {self.ecu_id} {self.app_id}/{self.ctx_id} "
                f"msgid=0x{self.msg_id:08X} ts={ts_ms:.1f}ms mcnt={self.mcnt} "
                f"paylen={len(self.payload)}")


# ── Frame Parser ──────────────────────────────────────────────────────────────

class DltParser:
    """Stateful byte-stream parser — feed bytes, get DltFrame objects."""

    def __init__(self):
        self._buf = bytearray()
        self._frames: List[DltFrame] = []

    def feed(self, data: bytes):
        self._buf.extend(data)
        self._parse()

    def pop_frames(self) -> List[DltFrame]:
        out = self._frames[:]
        self._frames.clear()
        return out

    def _parse(self):
        while True:
            # Find sync
            idx = self._buf.find(DLT_SERIAL_HDR)
            if idx < 0:
                # Keep last 3 bytes in case sync spans two reads
                self._buf = self._buf[-3:]
                return
            if idx > 0:
                self._buf = self._buf[idx:]

            # Need at least serial_hdr(4) + std_hdr(12) + ext_hdr(10) = 26 bytes
            if len(self._buf) < 26:
                return

            # Parse standard header (starts at offset 4)
            htyp, mcnt, dlt_len, tmsp = struct.unpack_from('>BBHI', self._buf, 4)
            ecu_id = self._buf[12:16].decode('ascii', errors='replace').rstrip('\x00')

            # Total frame = serial_hdr(4) + dlt_len
            total = 4 + dlt_len
            if len(self._buf) < total:
                return  # wait for more data

            # Parse extended header (offset 16)
            msin = self._buf[16]
            noar = self._buf[17]
            app_id = self._buf[18:22].decode('ascii', errors='replace').rstrip('\x00')
            ctx_id = self._buf[22:26].decode('ascii', errors='replace').rstrip('\x00')

            # Payload starts at offset 26
            payload = bytes(self._buf[26:total])

            frame = DltFrame(
                htyp=htyp, mcnt=mcnt, length=dlt_len,
                timestamp=tmsp, ecu_id=ecu_id,
                msin=msin, noar=noar,
                app_id=app_id, ctx_id=ctx_id,
                payload=payload
            )
            self._frames.append(frame)
            self._buf = self._buf[total:]


# ── Control Frame Builder ─────────────────────────────────────────────────────

def build_ctrl_frame(svc_id: int, payload: bytes,
                     app_id="TOOL", ctx_id="TEST") -> bytes:
    """Build a DLT control request frame (PC → MCU)."""
    svc_bytes = struct.pack('<I', svc_id)
    full_payload = svc_bytes + payload

    ext_hdr_len = DLT_EXT_HDR_LEN
    std_hdr_len = DLT_STD_HDR_LEN
    dlt_len = std_hdr_len + ext_hdr_len + len(full_payload)

    frame = bytearray()
    # Serial header
    frame += DLT_SERIAL_HDR
    # Standard header
    frame += struct.pack('>BBHI',
                         DLT_HTYP_STANDARD,  # HTYP
                         0x00,               # MCNT
                         dlt_len,            # LEN
                         0x00000000)         # TMSP
    frame += b'TOOL'                         # ECU ID (PC tool)
    # Extended header: MSIN for CONTROL_REQUEST = 0x1E (MSTP=0x07, MTIN=0x01)
    frame += bytes([0x1E, 0x00])             # MSIN, NOAR
    frame += app_id.encode('ascii')[:4].ljust(4, b'\x00')
    frame += ctx_id.encode('ascii')[:4].ljust(4, b'\x00')
    # Payload
    frame += full_payload
    return bytes(frame)


def cmd_set_log_level(app_id: str, ctx_id: str, level: int) -> bytes:
    app = app_id.encode('ascii')[:4].ljust(4, b'\x00')
    ctx = ctx_id.encode('ascii')[:4].ljust(4, b'\x00')
    payload = app + ctx + bytes([level])
    return build_ctrl_frame(DLT_SVC_SET_LOG_LEVEL, payload)

def cmd_set_default_level(level: int) -> bytes:
    return build_ctrl_frame(DLT_SVC_SET_DEFAULT_LEVEL, bytes([level]))

def cmd_get_log_info() -> bytes:
    return build_ctrl_frame(DLT_SVC_GET_LOG_INFO, b'')


# ── Test Runner ───────────────────────────────────────────────────────────────

class TestRunner:
    def __init__(self, port: str, baud: int):
        self.ser    = serial.Serial(port, baud, timeout=0.05)
        self.parser = DltParser()
        self.results: List[tuple] = []
        self._last_mcnt: Optional[int] = None
        self._mcnt_gaps = 0
        print(f"\n{'='*60}")
        print(f"DLT MCU Integration Test")
        print(f"Port: {port} @ {baud} baud")
        print(f"{'='*60}\n")

    def _drain(self, duration_s: float) -> List[DltFrame]:
        """Read frames for up to duration_s seconds."""
        deadline = time.monotonic() + duration_s
        frames = []
        while time.monotonic() < deadline:
            data = self.ser.read(256)
            if data:
                self.parser.feed(data)
            new = self.parser.pop_frames()
            for f in new:
                print(f"  RX: {f}")
                # Track MCNT gaps
                if self._last_mcnt is not None:
                    expected = (self._last_mcnt + 1) & 0xFF
                    if f.mcnt != expected and not f.is_control_response:
                        self._mcnt_gaps += 1
                        print(f"  ⚠️  MCNT gap: expected {expected}, got {f.mcnt}")
                if not f.is_control_response:
                    self._last_mcnt = f.mcnt
            frames.extend(new)
        return frames

    def _send(self, frame: bytes, label: str):
        print(f"  TX: {label} ({len(frame)}B)")
        self.ser.write(frame)

    def _pass(self, test_id: str, desc: str):
        print(f"  ✅ {test_id} PASS — {desc}\n")
        self.results.append((test_id, True, desc))

    def _fail(self, test_id: str, desc: str):
        print(f"  ❌ {test_id} FAIL — {desc}\n")
        self.results.append((test_id, False, desc))

    # ── Individual Tests ──────────────────────────────────────────────────────

    def t01_receive_boot_frames(self):
        print("T01 — Waiting for log frames after boot (3s)...")
        frames = self._drain(3.0)
        log_frames = [f for f in frames if not f.is_control_response]
        if log_frames:
            self._pass("T01", f"Received {len(log_frames)} log frame(s)")
        else:
            self._fail("T01", "No log frames received in 3s")

    def t02_set_log_level(self):
        print("T02 — Set MOTR/CTRL log level to DEBUG (5)...")
        self._send(cmd_set_log_level("MOTR", "CTRL", 5), "SET_LOG_LEVEL MOTR/CTRL=DEBUG")
        frames = self._drain(1.0)
        resp = [f for f in frames if f.is_control_response
                and f.ctrl_svc_id == DLT_SVC_SET_LOG_LEVEL]
        if resp and resp[0].ctrl_status == DLT_CTRL_OK:
            self._pass("T02", "SET_LOG_LEVEL response OK")
        else:
            self._fail("T02", f"No valid response (got {len(resp)} ctrl frames)")

    def t03_set_default_level(self):
        print("T03 — Set default log level to WARN (3)...")
        self._send(cmd_set_default_level(3), "SET_DEFAULT_LEVEL=WARN")
        frames = self._drain(1.0)
        resp = [f for f in frames if f.is_control_response
                and f.ctrl_svc_id == DLT_SVC_SET_DEFAULT_LEVEL]
        if resp and resp[0].ctrl_status == DLT_CTRL_OK:
            self._pass("T03", "SET_DEFAULT_LEVEL response OK")
        else:
            self._fail("T03", f"No valid response (got {len(resp)} ctrl frames)")

    def t04_get_log_info(self):
        print("T04 — Get log info (expect NOT_SUPPORTED)...")
        self._send(cmd_get_log_info(), "GET_LOG_INFO")
        frames = self._drain(1.0)
        resp = [f for f in frames if f.is_control_response
                and f.ctrl_svc_id == DLT_SVC_GET_LOG_INFO]
        if resp and resp[0].ctrl_status == DLT_CTRL_NOT_SUPPORTED:
            self._pass("T04", "GET_LOG_INFO correctly returned NOT_SUPPORTED")
        else:
            self._fail("T04", f"Unexpected response (got {len(resp)} ctrl frames)")

    def t05_mcnt_gaps(self):
        print("T05 — Restore level to INFO, monitor MCNT for 5s...")
        self._send(cmd_set_default_level(4), "SET_DEFAULT_LEVEL=INFO")
        self._drain(5.0)
        if self._mcnt_gaps == 0:
            self._pass("T05", "No MCNT gaps detected")
        else:
            self._fail("T05", f"{self._mcnt_gaps} MCNT gap(s) detected (frames dropped)")

    def t06_status_frame(self):
        print("T06 — Wait for status frame (DLT/STAT, up to 7s)...")
        frames = self._drain(7.0)
        stat = [f for f in frames
                if f.app_id == "DLT" and f.ctx_id == "STAT"]
        if stat:
            f = stat[0]
            if len(f.payload) >= 9:
                drop_count  = struct.unpack_from('<I', f.payload, 0)[0]
                buf_pct     = f.payload[4]
                uptime_sec  = struct.unpack_from('<I', f.payload, 5)[0]
                self._pass("T06",
                    f"Status frame received — drops={drop_count} "
                    f"buf={buf_pct}% uptime={uptime_sec}s")
            else:
                self._fail("T06", f"Status frame payload too short ({len(f.payload)}B)")
        else:
            self._fail("T06", "No status frame received within 7s")

    def t07_level_off(self):
        print("T07 — Set level to OFF, verify silence for 2s...")
        self._send(cmd_set_default_level(0), "SET_DEFAULT_LEVEL=OFF")
        time.sleep(0.2)  # let command take effect
        frames = self._drain(2.0)
        log_frames = [f for f in frames if not f.is_control_response
                      and not (f.app_id == "DLT" and f.ctx_id == "STAT")]
        if not log_frames:
            self._pass("T07", "No log frames received while level=OFF")
        else:
            self._fail("T07", f"{len(log_frames)} log frame(s) received while level=OFF")

    def t08_restore_level(self):
        print("T08 — Restore level to INFO, verify frames resume...")
        self._send(cmd_set_default_level(4), "SET_DEFAULT_LEVEL=INFO")
        frames = self._drain(3.0)
        log_frames = [f for f in frames if not f.is_control_response]
        if log_frames:
            self._pass("T08", f"Logging resumed — {len(log_frames)} frame(s) received")
        else:
            self._fail("T08", "No log frames after restoring level to INFO")

    # ── Run All ───────────────────────────────────────────────────────────────

    def run_all(self):
        self.t01_receive_boot_frames()
        self.t02_set_log_level()
        self.t03_set_default_level()
        self.t04_get_log_info()
        self.t05_mcnt_gaps()
        self.t06_status_frame()
        self.t07_level_off()
        self.t08_restore_level()
        self._summary()
        self.ser.close()

    def _summary(self):
        passed = sum(1 for _, ok, _ in self.results if ok)
        total  = len(self.results)
        print(f"\n{'='*60}")
        print(f"RESULTS: {passed}/{total} passed")
        print(f"{'='*60}")
        for tid, ok, desc in self.results:
            icon = "✅" if ok else "❌"
            print(f"  {icon} {tid}: {desc}")
        print(f"{'='*60}\n")
        sys.exit(0 if passed == total else 1)


# ── Entry Point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='DLT MCU Integration Test')
    parser.add_argument('--port', required=True,
                        help='Serial port (e.g. /dev/ttyUSB0 or COM3)')
    parser.add_argument('--baud', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    args = parser.parse_args()
    TestRunner(args.port, args.baud).run_all()

if __name__ == '__main__':
    main()