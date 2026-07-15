from __future__ import annotations

import struct
import threading
from collections import OrderedDict
from typing import Any

from .protocol import AUDIO_SAMPLE_RATE, AUDIO_SAMPLES_PER_FRAME


class JitterBuffer:
    """Small sequence-aware PCM jitter buffer with silence loss concealment."""

    def __init__(self, target_ms: int = 100, max_frames: int = 50) -> None:
        self.target_frames = max(1, target_ms // 20)
        self.max_frames = max(max_frames, self.target_frames + 2)
        self._packets: OrderedDict[int, bytes] = OrderedDict()
        self._samples: list[int] = []
        self._next_sequence: int | None = None
        self._started = False
        self._lock = threading.Lock()
        self.received = 0
        self.lost = 0
        self.late = 0
        self.resyncs = 0

    def feed(self, sequence: int, payload: bytes) -> None:
        with self._lock:
            if self._next_sequence is not None and sequence < self._next_sequence:
                # While capture is paused (mute, reconnect, Mac sleep) playback
                # keeps advancing _next_sequence past the sender's frozen
                # counter. Without a resync every resumed packet would be
                # dropped as late forever.
                if self._next_sequence - sequence > self.max_frames:
                    self._packets.clear()
                    self._samples.clear()
                    self._next_sequence = None
                    self._started = False
                    self.resyncs += 1
                else:
                    self.late += 1
                    return
            if sequence not in self._packets:
                self._packets[sequence] = payload
                self._packets = OrderedDict(sorted(self._packets.items()))
                self.received += 1
            while len(self._packets) > self.max_frames:
                self._packets.popitem(last=False)

    def _next_frame_locked(self) -> list[int]:
        silence = [0] * AUDIO_SAMPLES_PER_FRAME
        if not self._started:
            if len(self._packets) < self.target_frames:
                return silence
            self._next_sequence = next(iter(self._packets))
            self._started = True

        assert self._next_sequence is not None
        payload = self._packets.pop(self._next_sequence, None)
        self._next_sequence = (self._next_sequence + 1) & 0xFFFFFFFF
        if payload is None:
            self.lost += 1
            return silence
        return list(struct.unpack("<320h", payload))

    def read_samples(self, count: int) -> list[int]:
        with self._lock:
            while len(self._samples) < count:
                self._samples.extend(self._next_frame_locked())
            result = self._samples[:count]
            del self._samples[:count]
            return result


class NullAudioOutput:
    def __init__(self, target_ms: int = 100) -> None:
        self.jitter = JitterBuffer(target_ms)

    def start(self) -> None:
        return None

    def stop(self) -> None:
        return None

    def feed(self, sequence: int, payload: bytes) -> None:
        self.jitter.feed(sequence, payload)


class BlackHoleAudioOutput(NullAudioOutput):
    def __init__(
        self,
        device_name: str = "BlackHole 2ch",
        target_ms: int = 100,
        gain: float = 20.0,
    ) -> None:
        super().__init__(target_ms)
        self.device_name = device_name
        # Make-up gain. The ES8311 is kept at its clean 0dB setting, where close
        # speech only reaches ~1% full scale; raising the codec's own gain
        # amplified its noise floor faster than the voice. Applying the gain here
        # keeps the codec's SNR and stays tunable without reflashing.
        self.gain = gain
        self._stream: Any = None
        self._numpy: Any = None
        self._source: list[float] = []
        self._phase = 0.0
        self.output_rate = 48_000.0

    def start(self) -> None:
        try:
            import numpy
            import sounddevice
        except ImportError as exc:
            raise RuntimeError(
                "audio dependencies are missing; install bridge/requirements.txt"
            ) from exc

        devices = sounddevice.query_devices()
        candidates = [
            (index, device)
            for index, device in enumerate(devices)
            if self.device_name.lower() in str(device["name"]).lower()
            and int(device["max_output_channels"]) >= 2
        ]
        if not candidates:
            raise RuntimeError(
                f"output device '{self.device_name}' was not found; install BlackHole 2ch"
            )
        index, device = candidates[0]
        self.output_rate = float(device["default_samplerate"] or 48_000)
        self._numpy = numpy
        self._stream = sounddevice.OutputStream(
            device=index,
            samplerate=self.output_rate,
            channels=2,
            dtype="float32",
            blocksize=0,
            callback=self._callback,
        )
        self._stream.start()
        print(f"Audio output: {device['name']} at {self.output_rate:.0f} Hz (software resampling enabled)")

    def stop(self) -> None:
        if self._stream is not None:
            self._stream.stop()
            self._stream.close()
            self._stream = None

    def _callback(self, outdata: Any, frames: int, _time: Any, _status: Any) -> None:
        np = self._numpy
        step = AUDIO_SAMPLE_RATE / self.output_rate
        positions = self._phase + np.arange(frames, dtype=np.float64) * step
        required = int(positions[-1]) + 2 if frames else 2
        if len(self._source) < required:
            samples = self.jitter.read_samples(required - len(self._source))
            self._source.extend(sample / 32768.0 for sample in samples)
        mono = np.interp(positions, np.arange(len(self._source)), self._source).astype(np.float32)
        if self.gain != 1.0:
            # tanh soft-clip: loud syllables compress instead of hard-clipping,
            # which would smear the waveform STT depends on.
            mono = np.tanh(mono * self.gain).astype(np.float32)
        outdata[:, 0] = mono
        outdata[:, 1] = mono
        next_position = self._phase + frames * step
        consumed = int(next_position)
        if consumed:
            del self._source[:consumed]
        self._phase = next_position - consumed
