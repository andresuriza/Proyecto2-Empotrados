#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Simula los 3 filtros FIR del NIOS (software/.../filters.c) sobre un WAV de 48kHz,
# para tener una referencia de como DEBERIA sonar cada uno en la PC.
#
# Genera 3 WAV nuevos:
#   <nombre>_lowpass.wav    (pasa-bajos:  apagado/grave)
#   <nombre>_highpass.wav   (pasa-altos:  fino/brillante)
#   <nombre>_bandpass.wav   (pasa-banda:  telefono/medios)
#
# Uso:   python filtros_sim.py entrada.wav
# Requiere: numpy     (pip install numpy)

import sys, wave
import numpy as np

# --- mismos taps que filters.c (cambialos si ahi los cambiaste) ---
LP_N  = 12   # pasa-bajos:  promedio movil de 12, atenuado x12/16 (~1.8kHz)
HP_N  = 8    # pasa-altos:  x - promedio movil de 8 (~2.6kHz)
BPH_N = 4    # pasa-banda:  promedio movil corto (~5kHz, deja pasar mas voz/agudos)
BPL_N = 64   # pasa-banda:  promedio movil largo (~330Hz)  -- el bandpass va en MONO


def ma(x, N):
    """Promedio movil causal de N muestras (FIR), por canal. Igual que el NIOS:
    el buffer arranca en 0, asi que el comienzo es un transitorio."""
    k = np.ones(N) / N
    if x.ndim == 1:
        return np.convolve(x, k, 'full')[:len(x)]
    cols = [np.convolve(x[:, c], k, 'full')[:len(x)] for c in range(x.shape[1])]
    return np.stack(cols, axis=1)


def leer_wav(path):
    w = wave.open(path, 'rb')
    fs, nch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n)
    w.close()
    if sw != 2:
        raise SystemExit("Solo WAV de 16-bit. Convertilo: ffmpeg -i in.wav -c:a pcm_s16le out.wav")
    data = np.frombuffer(raw, dtype=np.int16).reshape(-1, nch)
    return fs, nch, data


def escribir_wav(path, fs, nch, data):
    # saturar a int16 (igual que el clamp del NIOS)
    d = np.clip(np.round(data), -32768, 32767).astype(np.int16)
    w = wave.open(path, 'wb')
    w.setnchannels(nch)
    w.setsampwidth(2)
    w.setframerate(fs)
    w.writeframes(d.tobytes())
    w.close()


def main():
    if len(sys.argv) < 2:
        print("Uso: python filtros_sim.py entrada.wav")
        return
    path = sys.argv[1]
    fs, nch, data = leer_wav(path)
    print("Entrada: %s   fs=%dHz   canales=%d   muestras=%d" % (path, fs, nch, len(data)))
    if fs != 48000:
        print("OJO: no es 48kHz (igual se procesa).")

    x = data.astype(np.float64)
    lowpass  = ma(x, LP_N) * (LP_N / 16.0)   # atenuado igual que el NIOS (sum >> 4)
    highpass = x - ma(x, HP_N)
    # bandpass EN MONO (igual que el NIOS): mezcla L+R, filtra una vez, manda a los 2 canales
    if x.ndim == 2:
        m  = ((x[:, 0] + x[:, 1]) / 2.0).reshape(-1, 1)
        bp = ma(m, BPH_N) - ma(m, BPL_N)
        bandpass = np.repeat(bp, x.shape[1], axis=1)
    else:
        bandpass = ma(x, BPH_N) - ma(x, BPL_N)

    base = path.rsplit('.', 1)[0]
    escribir_wav(base + "_lowpass.wav",  fs, nch, lowpass)
    escribir_wav(base + "_highpass.wav", fs, nch, highpass)
    escribir_wav(base + "_bandpass.wav", fs, nch, bandpass)

    print("Generados:")
    print("  %s_lowpass.wav    -> apagado/grave (se van los agudos)" % base)
    print("  %s_highpass.wav   -> fino/brillante (se van los graves)" % base)
    print("  %s_bandpass.wav   -> telefono/medios (se van graves Y agudos)" % base)


if __name__ == "__main__":
    main()