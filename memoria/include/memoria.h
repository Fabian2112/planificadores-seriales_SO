#ifndef MEMORIA_MAIN_H
#define MEMORIA_MAIN_H

#include "conexion.h"

t_config* memoria_config;
t_log* memoria_logger;

// Variables de configuración
int PUERTO_ESCUCHA;
int TAM_MEMORIA;
int TAM_PAGINA;
int ENTRADAS_POR_TABLA;
int CANTIDAD_NIVELES;
int RETARDO_MEMORIA;
char* PATH_SWAPFILE;
int RETARDO_SWAP;
char* LOG_LEVEL;
char* DUMP_PATH;

int fd_memoria;
int fd_cpu;
int fd_kernel;

typedef struct {
    int marco;
    int pid;
    int pagina_virtual;
    bool bit_uso;
    bool bit_modificado;
} t_marco_en_uso;

// Funciones de inicialización
void inicializar_memoria();
void inicializar_logs();
void inicializar_configs();
void imprimir_configs();

void* atender_memoria_kernel(void* arg);
void* atender_memoria_cpu(void* arg);

void mostrar_contenido_archivo(FILE* archivo, t_log* logger);
char* entregar_linea(FILE* archivo, t_log* logger, int buffer_size);

void simular_retardo_swap();
void procesar_envio_archivo_kernel(int fd_kernel);
void procesar_crear_proceso(int fd_kernel);
void procesar_consultar_espacio(int fd_kernel);

t_entrada_pagina* buscar_entrada_pagina(t_tabla_paginas* tabla, int nro_pagina, int niveles_totales, int entradas_por_tabla);
int hacer_reemplazo(t_marco_en_uso* victima, int nuevo_pid, int nueva_pagina_virtual, void* pagina_nueva);
int asignar_marco_libre(int tamanio_proceso);

void procesar_lectura(int socket_cpu);
void procesar_escritura(int socket_cpu);

void dump_memoria();
void procesar_dump_memory(int fd_kernel);

#endif // MEMORIA_MAIN_H
