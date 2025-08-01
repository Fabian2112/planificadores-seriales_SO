#ifndef UTILSMMU_H_
#define UTILSMMU_H_

#include "cpu.h"
#include "cache.h"
#include "tlb.h"
#include <stdint.h>

void iniciar_MMU();
int* obtener_direccion_fisica(int dir, int pid, int conexion_memoria);
int* obtener_direccion_fisica_con_cache(int dir, int pid, int conexion_memoria);
int calcular_entrada_nivel(int nro_pagina, int nivel, int cant_entradas_tabla, int niveles);
void* solicitar_direccion_fisica_multinivel(int nro_pagina, int pid, int conexion_memoria);
void* cargar_pagina_desde_memoria(int nro_pagina, int pid, int conexion_memoria);
void limpiar_tlb_proceso(int pid);
void finalizar_MMU();

#endif // UTILSMMU_H_