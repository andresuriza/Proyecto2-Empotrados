# SoC Reproductor de audio avanzado

Un reproductor de audio implementado para la FPGA De1-SoC para reproducir música. Proyecto 2 del curso Sistemas Empotrados.

## Tecnologías

- `SystemVerilog`
- `C`
- `NIOS II`
- `ARM HPS`

## Características

- Visualización de información de metadatos en tiempo real en display VGA
- Se pueden aplicar filtros en tiempo real (pasabandas, pasaaltas, pasabajas) usando los switches
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

**Importante:** el LW Bridge debe habilitarse explícitamente desde U-Boot con `run bridge_enable_handoff` antes de arrancar Linux. Sin este paso, el ARM recibe `SIGBUS` al acceder a `0xFF200000`.

## Proceso de desarrollo

Se comenzó con el diseño de los componentes principales necesarios en base a la especificación del proyecto, específicamente definiendo qué se desarrollaba en hardware (NIOS II + FPGA) y software (ARM). Una vez se definió esto, a partir del platform designer se empezaron a conectar los componentes y a realizar una prueba básica de síntesis, además de asignar pines a utilizar para los 7 segmentos y los botones.

Del lado de código para el NIOS se comenzó a desarollar el código que utilizara las direcciones de memoria de los distintos PIO con el fin de comunicarse con el hardware.

Para ARM se desarrolló al inicio una prueba inicial de comunicación de la memoria compartida entre ambos procesadores, una vez se determinó que se comunicaban correctamente, se desarrolló el proceso de enviar los datos cargados de las canciones a la memoria para ser procesadas por el IP de audio.

El audio tenía unos bugs como reproducción cortada, muestreo equivocado, entre otros, asi que se utilizó un doble buffer para absorber la latencia de la microSD

Se implementó un controlador personalizado para visualizar texto en pantalla VGA, utilizando un buffer de texto en memoria M10K, una vez se observó lo deseado en pantalla, se procedió a realizar la lectura de metadatos con el fin de observarlos en la pantalla.

De último se hicieron los filtros, usando una máquina de estados de dos estados, con el objetivo de bloquear cambio de filtro hasta que todos esten en 0, es decir, solo puede haber 1 switch seleccionado para que se aplique. Estos filtros inicialmente no se escuchaban adecuadamente, pero luego de unas correcciones de software se pudieron afinar a un modelo de referencia en Python. A nivel matemático, son promedios moviles FIR.

- SW[0] Pasabajos
- SW[1] Pasaaltos
- SW[2] Pasabanda

Se implementó el boot automático sin tener que estar flasheando el sistema mediante JTAG y ejecutando comandos en el procesador ARM, mediante un script de ejecución automática y un daemon para el ejecutable encargado de cargar la música en su directorio correspondiente en forma de playlist.

Se redujo también el tiempo de boot mediante optimizaciones y además reduciendo el tiempo de espera de U-Boot para saltarse el boot tradicional.

## Aprendizaje

El proyecto permitió aplicar los conocimientos de sistemas empotrados y los distintos talleres realizados para combinar lo que son prácticas de co-diseño con desarrollo de firmware y hardware pero mediante el uso de IPs principalmente, enfocándonos principalmente en la estructura del sistema y su lógica, para luego realizar síntesis automatizada gracias a las herramientas que existen.

Es importante conocer a profundidad las herramientas que otorga como Quartus y los recursos de la página de Altera, ya que incluyen cosas como una imagen mínima de Linux, el System Builder el cual funcionó como base para poder conectar varios componentes y utilizar el HPS, además de la terminal para NIOS II, la cual viene con bastantes comandos útiles.

Se debe tener cuidado con las secciones de datos de memoria, ya que en nuestro shared mem original, estas se estaban escribiendo, por lo que cuando los procesadores escribían y leían datos, estos se llegaban a sobreescribir, resultando en comportamientos inesperados, la solución fue explorar la opción de definir las secciones de datos y forzarlas al `onchip_mem`.

La frecuencia de muestreo de las canciones es algo complejo de manejar a la hora de desarrollar un reproductor de audio, por esto herramientas como `FFMPEG` son muy útiles para poder codificar las canciones de manera consistente y correcta para el enfoque del proyecto.

## ¿Qué se puede mejorar?

- Modos como bucle o aleatorio para playlist

- Soporte de más formatos de archivo

- Conexión para streaming desde PC

- Un mejor sistema de sonido (la especificación nos pedia usar un parlante + amplificador soldado, pero se puede usar cualquiera con jack 3.5mm)

## Ejecutar el proyecto

### Prerequisitos

Se debe instalar Quartus 18.1.

### Compilacion

1. Compilar en Quartus luego de abrir el `Proyecto2.qsf`

2. Corregir los mapeos de seccion para el BSP 

```bash
nios2-bsp hal nios2_bsp hps.sopcinfo \
  --cmd "add_section_mapping .rodata onchip_mem" \
  --cmd "add_section_mapping .rwdata onchip_mem" \
  --cmd "add_section_mapping .bss    onchip_mem" \
  --cmd "add_section_mapping .heap   onchip_mem" \
  --cmd "add_section_mapping .stack  onchip_mem"
```

3. Compilar el `.elf` y generar los `.hex` de init de memoria

```bash
cd software/hello_world_small
make clean
make
make mem_init_generate
```

4. Copiar los hex a la raiz

```bash
cp software/hello_world_small/mem_init/hps_onchip_mem.hex .
cp software/hello_world_small/mem_init/hps_shared_mem.hex .
```

5. Inyectar el NIOS en el .sof y generar el rbf (desde terminal NIOS)

```bash
quartus_cdb Proyecto2 --update_mif
quartus_asm Proyecto2
quartus_cpf -c -o bitstream_compression=on Proyecto2.sof output_file.rbf
```

6. Compilar binario ARM

**Nota** Debe realizarse desde Linux o WSL.

```bash
arm-linux-gnueabihf-gcc -O2 -static -pthread -o hw_test_list HPS_code/hw_test_list.c
```

7. Copiar a tarjeta SD `hw_test_list` a `/home/root/hw_test_list`

8. Copiar a tarjeta SD (particion FAT) el `output_file.rbf`

9. Copiar a `/etc/init.d/` el archivo `audioplayer`

**Nota** Para comunicarse con la FPGA para los siguientes comandos, se recomienda usar PuTTY o Minicom para utilizar el cable Mini-B UART o Ethernet.

10. Cambiar permisos para que se ejecute el daemon

```bash
chmod +x /etc/init.d/audioplayer
ln -s /etc/init.d/audioplayer /etc/rc5.d/S15audioplayer
```

11. Eliminar el delay a U-BOOT

```bash
setenv fdtimage socfpga_cyclone5_de1_soc.dtb
setenv bootcmd 'run mmcload; run mmcboot'
setenv bootdelay 0
saveenv
reset
```

12. Como optimizaciones sobre la imagen de Linux basica de Altera De1-SoC, ejecutar los siguientes comandos:

```bash
# rcS.d (antes del runlevel):
rm /etc/rcS.d/S40networking)
rm /etc/rcS.d/S*portmap
rm /etc/rcS.d/S*bootlogd
rm /etc/rcS.d/S*mountnfs*
rm /etc/rcS.d/S*banner*

# rc5.d (runlevel 5):
rm /etc/rc5.d/S*syslog*
rm /etc/rc5.d/S*gsrd*                
rm /etc/rc5.d/S*bootlogd
rm /etc/rc5.d/S*lighttpd             
rm /etc/rc5.d/S*sshd
```
13. Convertir las canciones deseadas al formato correcto

```bash
ffmpeg -i "entrada.wav" -ar 48000 -ac 2 -c:a pcm_s16le \
       -metadata title="Titulo" -metadata artist="Artista" -metadata album="Album" \
       "salida.wav"

  - -ar 48000  -> 48 kHz   |   -c:a pcm_s16le -> WAV 16-bit   |   -ac 2 
``` 
## Ejemplos de funcionalidad

<img width="800" height="900" alt="image" src="https://github.com/user-attachments/assets/cca724b2-05bc-42c1-aaa6-e4525a5a4541" />

https://github.com/user-attachments/assets/55049c0d-988b-4757-9c5e-33aed6ddc479
