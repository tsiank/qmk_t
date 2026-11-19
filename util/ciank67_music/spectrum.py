"""Windowed spectrum analysis; no audio or device access at import time."""
import numpy as np


class Spectrum:
    def __init__(self, rate, size=2048, gain_db=0.0):
        if rate < 8000 or size < 256:
            raise ValueError('Unsupported sample rate or FFT size')
        self.size = size
        self.gain_db = gain_db
        self.window = np.hanning(size)
        self.normalization = 2.0 / self.window.sum()
        frequencies = np.fft.rfftfreq(size, 1.0 / rate)
        edges = np.geomspace(40, min(16000, rate * 0.45), 17)
        self.bins = []
        for low, high in zip(edges[:-1], edges[1:]):
            indexes = np.flatnonzero((frequencies >= low) & (frequencies < high))
            if not len(indexes):
                indexes = np.array([np.argmin(abs(frequencies - np.sqrt(low * high)))])
            self.bins.append(indexes)
        self.levels = np.zeros(16)

    def analyze(self, samples):
        audio = np.asarray(samples, dtype=np.float64)
        if audio.ndim == 1:
            audio = audio[:, None]
        if audio.ndim != 2 or audio.shape[1] == 0 or not np.isfinite(audio).all():
            raise ValueError('Audio must be finite samples with shape (frames, channels)')
        audio = audio[-self.size:]
        if len(audio) < self.size:
            audio = np.pad(audio, ((self.size - len(audio), 0), (0, 0)))
        audio = audio - audio.mean(axis=0)
        fft = np.fft.rfft(audio * self.window[:, None], axis=0) * self.normalization
        # Average channel power, so opposite-phase stereo cannot cancel out.
        power = (abs(fft) ** 2).mean(axis=1)
        energy = np.array([np.sqrt(power[indexes].sum()) for indexes in self.bins])
        db = 20 * np.log10(np.maximum(energy, 1e-9)) + self.gain_db
        levels = np.clip((db + 60) / 54, 0, 1)
        smoothing = np.where(levels > self.levels, 0.65, 0.18)
        self.levels += smoothing * (levels - self.levels)
        return np.rint(self.levels * 255).astype(np.uint8).tolist()
