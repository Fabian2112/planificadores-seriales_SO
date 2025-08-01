#!/bin/bash

# Ejecutar Valgrind para cada módulo en paralelo con las rutas correctas
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./memoria/bin/memoria & 
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./kernel/bin/kernel & 
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./cpu/bin/cpu & 
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./io/bin/io &

# Esperar a que todos los procesos finalicen
wait

