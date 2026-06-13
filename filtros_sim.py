#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Simula los 3 filtros FIR del NIOS (software/.../filters.c) sobre un WAV de 48kHz,
# para tener una referencia de como DEBERIA sonar cada uno en la PC.
#
# Genera 4 WAV nuevos:
#   <nombre>_lowpass.wav    (pasa-bajos:  apagado/grave)
#   <nombre>_highpass.wav   (pasa-altos:  fino/brillante)
#   <nombre>_bandpass.wav   (pasa-banda:  telefono/medios)
#   <nombre>_eq.wav         (ecualizador 3 bandas, curva "V": realza graves+agudos, corta medios)
#   <nombre>_reverb.wav     (reverb: banco de combs realimentados, agrega cola/eco)
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

# --- ECUALIZADOR de 3 bandas (FIR). Bandas COMPLEMENTARIAS (low+mid+high = x exacto):
#       graves = MA(EQ_LO) ; agudos = x - MA(EQ_HI) ; medios = MA(EQ_HI) - MA(EQ_LO)
#     Despues cada banda * su ganancia. Curva "V": realza graves y agudos, corta medios.
EQ_LO     = 32    # corte bajo  (~660Hz): por debajo = graves
EQ_HI     = 4     # corte alto  (~5kHz):  por encima = agudos
GAIN_LOW  = 1.0   # graves: x1 (sin boost -> no satura)
GAIN_MID  = 0.375 # medios: scoop  -> NIOS: out = m - 0.625*mid
GAIN_HIGH = 1.0   # agudos: x1 (sin boost -> no peta)

# --- REVERB (renglon 3 del PDF: "filtro de reverberacion"). MONO (como el bandpass).
#     Banco de combs realimentados en paralelo:  y[n] = x[n] + g*y[n-D].
#     Varios D distintos -> cola densa. Mezcla seco/humedo con REV_WET.
REV_DELAYS   = [1557, 1617, 1491, 1422]   # muestras @48kHz (~30-34ms), mutuamente primos
REV_FEEDBACK = 0.72                        # realimentacion (<1 = estable); + alto = cola + larga
REV_WET      = 0.35                        # 0 = seco, 1 = solo reverb


def ma(x, N):
    """Promedio movil causal de N muestras (FIR), por canal. Igual que el NIOS:
    el buffer arranca en 0, asi que el comienzo es un transitorio."""
    k = np.ones(N) / N
    if x.ndim == 1:
        return np.convolve(x, k, 'full')[:len(x)]
    cols = [np.convolve(x[:, c], k, 'full')[:len(x)] for c in range(x.shape[1])]
    return np.stack(cols, axis=1)


def comb(x, D, g):
    """Comb realimentado y[n] = x[n] + g*y[n-D]. Vectorizado por bloques de D:
    dentro de un bloque, y[n-D] cae en el bloque ANTERIOR (ya final) -> exacto y rapido."""
    y = x.astype(np.float64).copy()
    for start in range(D, len(x), D):
        end = min(start + D, len(x))
        y[start:end] += g * y[start - D:end - D]
    return y


def reverb_mono(mono):
    """Suma de varios combs (banco Schroeder), normalizada. Devuelve la parte 'humeda'."""
    acc = np.zeros_like(mono, dtype=np.float64)
    for D in REV_DELAYS:
        acc += comb(mono, D, REV_FEEDBACK)
    return acc / len(REV_DELAYS)


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

    # ecualizador 3 bandas (FIR): low+mid+high = x exacto, cada banda con su ganancia
    eq_low  = ma(x, EQ_LO)               # graves
    eq_high = x - ma(x, EQ_HI)           # agudos
    eq_mid  = ma(x, EQ_HI) - ma(x, EQ_LO)  # medios
    eq3 = GAIN_LOW * eq_low + GAIN_MID * eq_mid + GAIN_HIGH * eq_high

    # reverb (mono, como el bandpass): mezcla seco + cola de combs
    if x.ndim == 2:
        m   = (x[:, 0] + x[:, 1]) / 2.0
        wet = reverb_mono(m)
        rv  = (1.0 - REV_WET) * m + REV_WET * wet
        reverb = np.repeat(rv.reshape(-1, 1), x.shape[1], axis=1)
    else:
        wet = reverb_mono(x)
        reverb = (1.0 - REV_WET) * x + REV_WET * wet

    base = path.rsplit('.', 1)[0]
    escribir_wav(base + "_lowpass.wav",  fs, nch, lowpass)
    escribir_wav(base + "_highpass.wav", fs, nch, highpass)
    escribir_wav(base + "_bandpass.wav", fs, nch, bandpass)
    escribir_wav(base + "_eq.wav",       fs, nch, eq3)
    escribir_wav(base + "_reverb.wav",   fs, nch, reverb)

    print("Generados:")
    print("  %s_lowpass.wav    -> apagado/grave (se van los agudos)" % base)
    print("  %s_highpass.wav   -> fino/brillante (se van los graves)" % base)
    print("  %s_bandpass.wav   -> telefono/medios (se van graves Y agudos)" % base)
    print("  %s_eq.wav         -> EQ 3 bandas curva V (graves+agudos arriba, medios abajo)" % base)
    print("  %s_reverb.wav     -> reverb (cola/eco, suena 'con espacio')" % base)


if __name__ == "__main__":
    main()