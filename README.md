# SoC Reproductor de audio avanzado

Un reproductor de audio implementado para la FPGA De1-SoC para reproducir música. Proyecto 2 del curso Sistemas Empotrados.

## Tecnologías

- `SystemVerilog`
- `C`
- `NIOS II`
- `ARM HPS`

## Características

- Visualización de información en tiempo real en display VGA
- Se pueden aplicar filtros en tiempo real usando los switches `SW0`, `SW1` y `SW2`
- Cargar música en formato `WAV` en una tarjeta microSD
- Alta definición de audio desde el parlante

## Ejecutar el proyecto

1. Se debe instalar Quartus 18.1 y compilar el proyecto para poder programar el `.sof` en la FPGA
2. Abrir Eclipse para NIOS II y crear un proyecto con el `.sopcinfo` generado
