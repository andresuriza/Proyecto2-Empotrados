# SoC Reproductor de audio avanzado

Un reproductor de audio implementado para la FPGA De1-SoC para reproducir música. Proyecto 2 del curso Sistemas Empotrados.

## Tecnologías

- `SystemVerilog`
- `C`
- `NIOS II`
- `ARM HPS`

## Características

- Visualización de información de metadatos en tiempo real en display VGA
- Se pueden aplicar filtros en tiempo real usando los switches `SW0`, `SW1` y `SW2`
- Cargar música en formato `WAV` en una tarjeta microSD
- Alta definición de audio desde el parlante

## Diagrama de bloques


```
┌────────────────────────────────────────────────────────────────────────┐
│                          DE1-SoC (Cyclone V)                           │
│                                                                        │
│  ┌──────────────────────────────────┐    LW Bridge (0xFF200000)        │
│  │        ARM HPS — Linux           │◄────────────────────────────┐   │
│  │  hw_test_list.c                  │   shared_mem  mailbox sh[0..4]│  │
│  │  · hilo lector  SD → RAM ring    │◄──────────────────────────┐  │  │
│  │  · alimentador  ring → shmem     │   PCM buffer  sh[64..16383]│  │  │
│  │  · actualiza buffer VGA          │                            │  │  │
│  └──────────┬───────────────────────┘                            │  │  │
│             │ /dev/mmcblk0p1 (microSD)                           │  │  │
│  ┌──────────▼───────────────────────┐                            │  │  │
│  │        FPGA — NIOS II Gen2       │────────────────────────────┘  │  │
│  │  hello_world_small.c             │                               │  │
│  │  · drenaje de audio (48 kHz)     │   PIOs / MMIO                 │  │
│  │  · apply_filter() FIR            │◄──────────────────────────────┘  │
│  │  · ISRs: KEY[3:0], SW[2:0], 1ms │                                   │
│  │  · HEX MM:SS                     │                                   │
│  └──────────┬───────────────────────┘                                   │
│             │ I2S + I2C (WM8731)           VGA Text Controller          │
│  ┌──────────▼──────────────────┐    ┌──────────────────────────────┐   │
│  │  audio_pll  50→12.288 MHz   │    │  40×8 chars, 640×480@60 Hz   │   │
│  │  WM8731 Codec               │    │  base ARM: 0xFF210000        │   │
│  └─────────────────────────────┘    └──────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────┘
```
## Pipeline de datos de audio

```
microSD (WAV 16-bit PCM)
    │
    ▼
[Hilo Lector — ARM]  ──────────────────────────────────────────────────────
    │  Lee en bloques de CHUNK_FRAMES=2048 frames                          │
    │  Escribe en RAM ring de 256 K frames (~5.5 s a 48 kHz, DDR3)        │
    ▼                                                                       │
[Alimentador — ARM]  (main thread)                                         │
    │  Drena RAM ring → shared_mem circular buffer                         │ backpressure:
    │  Publica sh[HEAD]; bloqueado si NIOS no drena (pausa)                │ cuando NIOS
    ▼                                                                       │ pausa, el ARM
[shared_mem — buffer circular — 16 320 words × 32 bits]                   │ se frena solo
    │  [31:16] = muestra izquierda  [15:0] = muestra derecha               │
    ▼                                                                       │
[NIOS II — bucle de drenaje]  ─────────────────────────────────────────────
    │  Drena batch ← espacio disponible en FIFO del codec
    │  apply_filter(left, right, SW[1:0])
    ▼
[WM8731 Audio IP — FIFO]  →  DAC I2S  →  Parlante / salida de audio
```

## Protocolo Mailbox (memoria compartida)

La comunicación entre ARM y NIOS se realiza sin semáforos mediante un arreglo de palabras de 32 bits en `shared_mem`. El mecanismo de pausa se implementa por `backpressure`: el NIOS deja de drenar el buffer, este se llena y el ARM deja de escribir de forma natural.

| `sh[]` | Dir. NIOS | Dir. ARM (LW Bridge) | Nombre | Dirección | Descripción |
|--------|-----------|----------------------|--------|-----------|-------------|
| 0 | `0x00000000` | `0xFF200000` | CMD | ARM→NIOS | `0`=idle · `1`=play · `2`=stop |
| 1 | `0x00000004` | `0xFF200004` | HEAD | ARM→NIOS | Puntero de escritura del buffer circular |
| 2 | `0x00000008` | `0xFF200008` | TAIL | NIOS→ARM | Puntero de lectura del buffer circular |
| 3 | `0x0000000C` | `0xFF20000C` | REQ | NIOS→ARM | `0`=nada · `1`=next · `2`=prev · `3`=stop (rebobinar) |
| 4 | `0x00000010` | `0xFF200010` | STATE | NIOS→ARM | `0`=detenido · `1`=reproduciendo · `2`=pausa |
| 64–16383 | `0x00000100`+ | `0xFF200100`+ | PCM | circular | `[31:16]`=L · `[15:0]`=R · 16 320 words ≈ 5.4 s |

---

## Mapa de Memoria

### Espacio de direcciones del NIOS II (bus Avalon)

| Base (hex) | Componente | Tamaño | IRQ | Función |
|------------|------------|--------|-----|---------|
| `0x00000000` | `shared_mem` | 64 KB | — | Mailbox ARM↔NIOS + buffer PCM circular |
| `0x00020000` | `onchip_mem` | 64 KB | — | Código y datos del NIOS II (`.text`, `.bss`, `.heap`, `.stack`) |
| `0x00031000` | `sys_timer` | 32 B | 1 | Temporizador de 1 ms (tick del reloj MM:SS) |
| `0x00031020` | `audio_0` | 16 B | — | IP WM8731: ctrl +0, fifo +4, izq +8, der +12 |
| `0x00031030` | `audio_and_video_config_0` | 16 B | — | Configuración I2C del codec (I2C maestro FPGA) |
| `0x00031040` | `pio_hex_hi` | 16 B | — | HEX5–HEX4 salida, 14 bits |
| `0x00031050` | `pio_hex_lo` | 16 B | — | HEX3–HEX0 salida, 28 bits |
| `0x00031060` | `pio_sw` | 16 B | 4 | SW[2:0] entrada con IRQ por flanco |
| `0x00031070` | `pio_key` | 16 B | 0 | KEY[3:0] entrada con IRQ por flanco (activo-bajo) |
| `0x00031080` | `jtag_uart` | 8 B | 2 | UART debug JTAG (`printf` por cable USB-Blaster) |

**Layout de pio_hex_lo** (`[27:21]`=HEX3, `[20:14]`=HEX2, `[13:7]`=HEX1, `[6:0]`=HEX0)  
**Layout de pio_hex_hi** (`[13:7]`=HEX5, `[6:0]`=HEX4)

### Espacio de direcciones del ARM HPS (Linux — LW Bridge)

| Dir. ARM (hex) | Offset LW | Módulo | Descripción |
|----------------|-----------|--------|-------------|
| `0xFF200000` | `0x00000` | `shared_mem` | Mailbox + buffer PCM (mismo bloque físico que `0x0` del NIOS) |
| `0xFF210000` | `0x10000` | `vga_text_controller` | Buffer de texto VGA 40×8 (write-only; celda = `row×40+col`) |

> **Importante:** el LW Bridge debe habilitarse explícitamente desde U-Boot con `run bridge_enable_handoff` antes de arrancar Linux. Sin este paso, el ARM recibe `SIGBUS` al acceder a `0xFF200000`.

---

## Proceso de desarrollo

Se comenzó con el diseño de los componentes principales necesarios en base a la especificación del proyecto, específicamente definiendo qué se desarrollaba en hardware (NIOS II + FPGA) y software (ARM). Una vez se definió esto, a partir del platform designer se empezaron a conectar los componentes y a realizar una prueba básica de síntesis, además de asignar pines a utilizar para los 7 segmentos y los botones.

Del lado de código para el NIOS se comenzó a desarollar el código que utilizara las direcciones de memoria de los distintos PIO a utilizar, con el fin de comunicarse con el hardware.

Para ARM se desarrolló al inicio una prueba inicial de comunicación de la memoria compartida entre ambos procesadores, una vez se determinó que se comunicaban correctamente, se desarrolló el proceso de enviar los datos cargados de las canciones a la memoria para ser procesadas por el IP de audio.

El audio tenía unos bugs como reproducción cortada, muestreo equivocado, entre otros, asi que se utilizo un doble buffer para absorber la latencia de la microSD

Se implemento un controlador personalizado para visualizar texto en pantalla VGA, utilizando un buffer de texto en memoria M10K, una vez se observo lo deseado, se procedio a realizar la lectura de metadatos con el fin de observarlos en la pantalla.

De ultimo se hicieron los filtros, usando una maquina de estados de dos estados, con el objetivo de bloquear cambio de filtro hasta que todos esten en 0, es decir, solo puede haber 1 switch seleccionado para que se aplique. Estos filtros inicialmente no se escuchaban adecuadamente, pero luego de unas correcciones de software se pudieron afinar a un modelo de referencia en Python. A nivel matematico, son promedios moviles FIR.

- SW[0] Pasabajos
- SW[1] Pasaaltos
- SW[2] Pasabanda

Se implementó el boot automático sin tener que estar flasheando el sistema mediante JTAG y ejecutando comandos en el procesador ARM, mediante un script de ejecución automática y un daemon para el ejecutable encargado de cargar la música en su directorio correspondiente en forma de playlist.

Se redujo también el tiempo de boot mediante optimizaciones y además reduciendo el tiempo de espera de U-Boot para saltarse el boot tradicional.

## Aprendizaje

Conocer bastante bien las herramientas que otorgan los sistemas como Quartus y la pagina de Altera, ya que vienen recursos utiles como una imagen minima de Linux, el System Builder el cual funciono como base para poder conectar varios componentes y utilizar el HPS, ademas de la terminal para NIOS II, la cual viene con bastantes comandos utiles.

Se debe tener cuidado con las secciones de datos de memoria, ya que en nuestro shared mem original, estas se estaban escribiendo, por lo que cuando los procesadores escribian y leian datos se sobreescribian, resultando en comportamientos inesperados, la solucion fue explorar la opcion de definir las secciones de datos y forzarlas al `onchip_mem`.

La frecuencia de muestreo de las canciones es algo complejo de manejar a la hora de desarrollar un reproductor de audio, por esto herramientas como `FFMPEG` son muy utiles para poder codificar las canciones de manera consistente y util para el enfoque del proyecto.

## Ejecutar el proyecto

1. Se debe instalar Quartus 18.1 y compilar el proyecto para poder programar el `.sof` en la FPGA
2. Abrir Eclipse para NIOS II y crear un proyecto con el `.sopcinfo` generado

