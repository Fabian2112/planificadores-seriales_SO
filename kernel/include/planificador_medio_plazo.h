#ifndef PLANIFICADOR_MEDIO_PLAZO_H
#define PLANIFICADOR_MEDIO_PLAZO_H

#include <pthread.h>
#include "pcb.h"
#include "kernel.h"

/**
 * Hilo del planificador de mediano plazo. Se encarga de:
 *  - Evaluar procesos bloqueados para suspensión
 *  - Activar procesos suspendidos cuando hay memoria disponible
 *  - Gestionar operaciones de swap con memoria
 */
void* planificador_medio_plazo(void* args);
void manejar_suspension(t_proceso_kernel* proceso);
void activar_proceso_suspendido(t_proceso_kernel* proceso);

#endif // PLANIFICADOR_MEDIO_PLAZO_H
