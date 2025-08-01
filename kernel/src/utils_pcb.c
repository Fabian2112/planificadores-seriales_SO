#include "../include/pcb.h"
#include "../include/kernel.h"
#include <commons/log.h>
#include <stdbool.h>

bool comparar_por_estimacion(void* a, void* b) {
    // Esta función se usa cuando trabajamos directamente con PCBs
    t_pcb* p1 = (t_pcb*) a;
    t_pcb* p2 = (t_pcb*) b;
    return p1->tiempo_estimado < p2->tiempo_estimado;
}

bool comparar_procesos_por_estimacion(void* a, void* b) {
    // Esta función se usa cuando trabajamos con t_proceso_kernel
    t_proceso_kernel* p1 = (t_proceso_kernel*) a;
    t_proceso_kernel* p2 = (t_proceso_kernel*) b;
    return p1->pcb.tiempo_estimado < p2->pcb.tiempo_estimado;
}

bool comparar_por_memoria(void* a, void* b) {
    // Para comparar por memoria, necesitaríamos el campo en t_proceso, no en t_pcb
    // Por ahora retornamos false como placeholder
    (void)a;
    (void)b;

    return false;
}

void loggear_pcb(t_log* logger, t_pcb* pcb) {
    log_info(logger, "PCB (PID=%d, Estado=%s, Estimación=%d, PC=%d)",
             pcb->pid,
             estado_str(pcb->estado),
             pcb->tiempo_estimado,
             pcb->pc);
}
