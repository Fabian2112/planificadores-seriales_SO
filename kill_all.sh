#!/bin/bash
echo "🛑 Matando terminales y procesos del TP..."

# Matar terminales abiertas con xterm (si las usaste)
pkill xterm

# Matar procesos específicos de los binarios
pkill -f memoria/bin/memoria
pkill -f kernel/bin/kernel
pkill -f cpu/bin/cpu
pkill -f io/bin/io

echo "✅ Todos los procesos finalizados."
