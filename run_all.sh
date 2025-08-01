#!/bin/bash

## Configuración de tiempos (en segundos)
TIEMPO_MEMORIA=1.5     # Memoria necesita más tiempo para inicializar
TIEMPO_KERNEL=1.0      # Kernel requiere menos que Memoria
TIEMPO_CPU=0.5         # CPU puede iniciarse rápido después de Kernel
TIEMPO_IO=0            # I/O no necesita espera

## Obtener puertos de configuración
PUERTO_MEMORIA=$(grep "PUERTO_ESCUCHA" ./memoria/memoria.config | cut -d'=' -f2)
PUERTO_KERNEL_DISPATCH=$(grep "PUERTO_ESCUCHA_DISPATCH" ./kernel/kernel.config | cut -d'=' -f2)
PUERTO_IO=$(grep "PUERTO_ESCUCHA" ./io/io.config | cut -d'=' -f2)

echo "🚀 Iniciando módulos con tiempos optimizados:"
echo "------------------------------------------"
echo "Memoria (puerto $PUERTO_MEMORIA)    : $TIEMPO_MEMORIA seg"
echo "Kernel (puerto $PUERTO_KERNEL_DISPATCH) : $TIEMPO_KERNEL seg" 
echo "CPU (conecta a $PUERTO_MEMORIA)     : $TIEMPO_CPU seg"
echo "I/O (puerto $PUERTO_IO)           : sin espera"
echo "------------------------------------------"

# Iniciar Memoria
xterm -hold -T "Memoria:$PUERTO_MEMORIA" -geometry 80x24+0+0 -e "./memoria/bin/memoria" &
sleep $TIEMPO_MEMORIA

# Iniciar Kernel  
xterm -hold -T "Kernel:$PUERTO_KERNEL_DISPATCH" -geometry 80x24+650+0 -e "./kernel/bin/kernel" &
sleep $TIEMPO_KERNEL

# Iniciar CPU
xterm -hold -T "CPU→Memoria" -geometry 80x24+0+400 -e "./cpu/bin/cpu" &
sleep $TIEMPO_CPU

# Iniciar I/O
xterm -hold -T "I/O:$PUERTO_IO" -geometry 80x24+650+400 -e "./io/bin/io" &

echo "✅ Todos los módulos iniciados"
echo "Nota: Las terminales están organizadas en posiciones específicas para mejor visualización"
