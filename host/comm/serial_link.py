"""Serial link with threaded reader, frame queue, and independent processor.

The serial reader thread only reads bytes from the OS serial port and parses
frames. Parsed frames are pushed into a thread-safe queue. A separate processor
thread consumes frames from the queue and invokes the user-supplied on_frame
callback. This prevents heavy or UI-bound callbacks from blocking the reader
and overflowing the OS serial receive buffer.
"""

import queue
import serial
import threading
import time
from typing import Callable, Optional
from .protocol import unpack_frame, Frame

# Frame queue capacity. At 50 frames/s this gives ~10 seconds of buffering.
# If the consumer cannot keep up, old frames are dropped instead of stalling
# the serial reader (which would cause OS buffer overflow).
FRAME_QUEUE_SIZE = 512


class SerialLink:
    def __init__(
        self,
        port: str = "",
        baudrate: int = 115200,
        on_frame: Optional[Callable[[Frame], None]] = None,
    ):
        self.port = port
        self.baudrate = baudrate
        self.on_frame = on_frame
        self.ser: Optional[serial.Serial] = None
        self._reader_thread: Optional[threading.Thread] = None
        self._proc_thread: Optional[threading.Thread] = None
        self._running = False
        self._rx_buf = b""
        self._rx_lock = threading.Lock()
        self._frame_queue: queue.Queue[Optional[Frame]] = queue.Queue(maxsize=FRAME_QUEUE_SIZE)
        self._write_lock = threading.Lock()

    def open(self, port: Optional[str] = None) -> bool:
        if port:
            self.port = port
        try:
            self.ser = serial.Serial(
                self.port,
                self.baudrate,
                timeout=0.05,
                write_timeout=1.0,
            )
            # Enlarge OS receive buffer on Windows to absorb bursts when the
            # processor thread is temporarily blocked (e.g. by UI rendering).
            try:
                self.ser.set_buffer_size(rx_size=65536)
            except Exception:
                # Not supported on all platforms / pyserial backends; ignore.
                pass

            self._running = True
            self._reader_thread = threading.Thread(target=self._reader, daemon=True)
            self._proc_thread = threading.Thread(target=self._processor, daemon=True)
            self._reader_thread.start()
            self._proc_thread.start()
            return True
        except Exception as e:
            print(f"[SerialLink] open failed: {e}")
            return False

    def close(self):
        self._running = False
        # Wake the processor thread so it can exit promptly.
        try:
            self._frame_queue.put_nowait(None)
        except queue.Full:
            pass
        if self._reader_thread:
            self._reader_thread.join(timeout=1.0)
        if self._proc_thread:
            self._proc_thread.join(timeout=1.0)
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None
        # Drain any remaining frames to avoid stale data on reconnect.
        while not self._frame_queue.empty():
            try:
                self._frame_queue.get_nowait()
            except queue.Empty:
                break

    def is_open(self) -> bool:
        return self.ser is not None and self.ser.is_open

    def send(self, data: bytes) -> bool:
        if not self.is_open():
            return False
        try:
            with self._write_lock:
                self.ser.write(data)
            return True
        except Exception as e:
            print(f"[SerialLink] send error: {e}")
            return False

    def _reader(self):
        while self._running and self.ser and self.ser.is_open:
            try:
                chunk = self.ser.read(512)
                if chunk:
                    with self._rx_lock:
                        self._rx_buf += chunk
                    self._parse()
            except Exception as e:
                print(f"[SerialLink] read error: {e}")
                time.sleep(0.1)

    def _parse(self):
        with self._rx_lock:
            buf = self._rx_buf
            self._rx_buf = b""
        while True:
            frame, remaining = unpack_frame(buf)
            if frame is None:
                # Keep incomplete tail (up to reasonable limit).
                if len(remaining) > 4096:
                    remaining = b""
                with self._rx_lock:
                    self._rx_buf = remaining + self._rx_buf
                break
            buf = remaining
            # Push frame to queue; drop oldest frame if queue is full so the
            # serial reader never blocks waiting for the processor.
            try:
                self._frame_queue.put_nowait(frame)
            except queue.Full:
                try:
                    self._frame_queue.get_nowait()
                except queue.Empty:
                    pass
                try:
                    self._frame_queue.put_nowait(frame)
                except queue.Full:
                    pass

    def _processor(self):
        while self._running:
            try:
                frame = self._frame_queue.get(timeout=0.05)
            except queue.Empty:
                continue
            if frame is None:
                # Sentinel value used during close() to wake the thread.
                break
            if self.on_frame:
                try:
                    self.on_frame(frame)
                except Exception as e:
                    print(f"[SerialLink] on_frame error: {e}")
