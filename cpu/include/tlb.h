#ifndef TLB_H_
#define TLB_H_

#include "cpu.h"

void iniciar_tlb(int entradas, char* algoritmo, t_log* logger);
void* consultar_tlb(int pid, int pagina);
void actualizar_tlb(int pid, int pagina, void* marco);
void limpiar_tlb();
void eliminar_entradas_proceso(int pid);
void destruir_tlb();
void mostrar_estado_tlb();

#endif