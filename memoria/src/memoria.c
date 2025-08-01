
#include "memoria.h"
#include <server.h>
#include <cliente.h>

// Definiciones adicionales
#define CONSULTAR_ESPACIO 100
#define KERNEL 0
#define CPU 1
#define HANDSHAKE 200

int fd_kernel = -1;
char path_instrucciones[256];
FILE* archivo_instrucciones = NULL;


void inicializar_memoria() {
    inicializar_logs();
    inicializar_configs();
    imprimir_configs();
    inicializar_sincronizacion();
    inicializar_semaforos();

    // Calcular marcos totales
    total_marcos = TAM_MEMORIA / TAM_PAGINA;
    log_info(memoria_logger, "Marcos totales: %d", total_marcos);

    // Inicializar control de marcos
    marcos_libres = malloc(total_marcos * sizeof(bool));
    for (int i = 0; i < total_marcos; i++) {
        marcos_libres[i] = true; // Todos los marcos inicialmente libres
    }
    log_info(memoria_logger, "Marcos libres inicializados");

    // Inicializar diccionarios
    procesos_activos = dictionary_create();
    procesos_suspendidos = dictionary_create();
    log_info(memoria_logger, "Diccionarios de procesos inicializados");

    marcos_en_uso = list_create();
    puntero_clock = 0;

    // Inicializar memoria física
    memoria_fisica = malloc(TAM_MEMORIA);
    if (memoria_fisica == NULL) {
        log_error(memoria_logger, "Error al asignar memoria física");
        exit(EXIT_FAILURE);
    }
    memset(memoria_fisica, 0, TAM_MEMORIA);  // Inicializar a 0
    log_info(memoria_logger, "Memoria física inicializada (%d bytes)", TAM_MEMORIA);

    // Inicializar archivo de swap
    swap_file = fopen(PATH_SWAPFILE, "w+b");
    if (swap_file == NULL) {
        log_error(memoria_logger, "Error al abrir el archivo de swap: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    log_info(memoria_logger, "Archivo de swap abierto en: %s", PATH_SWAPFILE);
}

void inicializar_logs(){

    memoria_logger = log_create("memoria.log", "MEMORIA_LOG", 1, LOG_LEVEL_INFO);
    if (memoria_logger == NULL)
    {
        perror("Algo salio mal con el memoria_log, no se pudo crear o escuchar el archivo");
        exit(EXIT_FAILURE);
    }
    iniciar_logger(memoria_logger);
}

void inicializar_configs(){

    memoria_config = config_create("../memoria.config");

    if (memoria_config == NULL)
    {
        perror("Error al cargar memoria_config");
        exit(EXIT_FAILURE);
    }

    // Cargar configuraciones
    PUERTO_ESCUCHA = config_get_int_value(memoria_config, "PUERTO_ESCUCHA");
    TAM_MEMORIA = config_get_int_value(memoria_config, "TAM_MEMORIA");
    TAM_PAGINA = config_get_int_value(memoria_config, "TAM_PAGINA");
    ENTRADAS_POR_TABLA = config_get_int_value(memoria_config, "ENTRADAS_POR_TABLA");
    CANTIDAD_NIVELES = config_get_int_value(memoria_config, "CANTIDAD_NIVELES");
    RETARDO_MEMORIA = config_get_int_value(memoria_config, "RETARDO_MEMORIA");
    PATH_SWAPFILE = config_get_string_value(memoria_config, "PATH_SWAPFILE");
    RETARDO_SWAP = config_get_int_value(memoria_config, "RETARDO_SWAP");
    LOG_LEVEL = config_get_string_value(memoria_config, "LOG_LEVEL");
    DUMP_PATH = config_get_string_value(memoria_config, "DUMP_PATH");
}

void imprimir_configs(){
    log_info(memoria_logger, "PUERTO_ESCUCHA: %d", PUERTO_ESCUCHA);
    log_info(memoria_logger, "TAM_MEMORIA: %d", TAM_MEMORIA);
    log_info(memoria_logger, "TAM_PAGINA: %d", TAM_PAGINA);
    log_info(memoria_logger, "ENTRADAS_POR_TABLA: %d", ENTRADAS_POR_TABLA);
    log_info(memoria_logger, "CANTIDAD_NIVELES: %d", CANTIDAD_NIVELES);
    log_info(memoria_logger, "RETARDO_MEMORIA: %d", RETARDO_MEMORIA);
    log_info(memoria_logger, "PATH_SWAPFILE: %s", PATH_SWAPFILE);
    log_info(memoria_logger, "RETARDO_SWAP: %d", RETARDO_SWAP);
    log_info(memoria_logger, "LOG_LEVEL: %s", LOG_LEVEL);
    log_info(memoria_logger, "DUMP_PATH: %s", DUMP_PATH);
}

t_tabla_paginas* crear_tablas_paginas_multinivel(int nivel_actual, int niveles_totales, int entradas_por_tabla) {

    log_info(memoria_logger, "Creando tabla de páginas en nivel %d de %d", nivel_actual, niveles_totales);
    t_tabla_paginas* tabla = malloc(sizeof(t_tabla_paginas));
    if (!tabla) return NULL;

    tabla->nivel = nivel_actual;
    tabla->entradas_usadas = 0;
    tabla->entradas = calloc(entradas_por_tabla, sizeof(t_entrada_pagina*));

    if (!tabla->entradas) {
        free(tabla);
        return NULL;
    }

    for (int i = 0; i < entradas_por_tabla; i++) {
        t_entrada_pagina* entrada = malloc(sizeof(t_entrada_pagina));
        if (!entrada) continue;

        entrada->presente = false;
        entrada->modificada = false;
        entrada->referenciada = false;
        entrada->marco = -1;
        entrada->subtabla = NULL;

        // Si no estamos en el último nivel, creamos la subtabla recursivamente
        if (nivel_actual < niveles_totales - 1) {
            entrada->subtabla = crear_tablas_paginas_multinivel(nivel_actual + 1, niveles_totales, entradas_por_tabla);
        }

        tabla->entradas[i] = entrada;
    }

    log_info(memoria_logger, "Tabla de páginas creada en nivel %d con %d entradas", nivel_actual, entradas_por_tabla);
    return tabla;
}

int main(void) {

    inicializar_memoria();

    log_info(memoria_logger, "Se inicializo memoria.");

    fd_memoria = -1;

    fd_memoria = iniciar_servidor(memoria_logger, PUERTO_ESCUCHA);

    if (fd_memoria == -1){
        log_error(memoria_logger, "ERROR: Memoria no levanta servidor");
        return EXIT_SUCCESS;
    }

    log_info(memoria_logger, "Se inicializo servidor Memoria. Creando hilos...");

    pthread_t hilo_memoria_kernel;
    pthread_t hilo_memoria_cpu;
    
    int fd_cliente;
    for (int i = 0; i < 2; i++)
        {
        log_info(memoria_logger, "Servidor de Memoria iniciado. Esperando Clientes... con fd_memoria: %d", fd_memoria);
        
        fd_cliente = esperar_cliente(memoria_logger, fd_memoria);
        if (fd_cliente < 0) {
            log_error(memoria_logger, "Error al aceptar cliente");
            continue;
        }

        int cod_op = recibir_operacion(memoria_logger, fd_cliente);
        switch (cod_op)
            {
            case KERNEL:
            log_info(memoria_logger, "Se conecto el KERNEL");
            fd_kernel = fd_cliente;
            bool confirmacion = true;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            pthread_create(&hilo_memoria_kernel, NULL, atender_memoria_kernel, &fd_kernel);

            log_info(memoria_logger, "Creado hilo de kernel");

            break;

            case CPU:
            log_info(memoria_logger, "Se conecto la CPU");
            fd_cpu = fd_cliente;
            pthread_create(&hilo_memoria_cpu, NULL, atender_memoria_cpu, &fd_cpu);

            log_info(memoria_logger, "Creado hilo de CPU");
            // Esperar a que CPU responda el handshake
            sem_wait(&sem_cpu_memoria_hs);
            log_info(memoria_logger, "Handshake con CPU completado");
            break;

            default:
            log_error(memoria_logger, "No reconozco ese codigo");
            break;
            }
        
        }

    pthread_join(hilo_memoria_kernel,NULL);
    pthread_join(hilo_memoria_cpu, NULL);

    log_debug(memoria_logger,"Se ha desconectado de memoria");

	printf("\nMEMORIA DESCONECTADO!\n\n");

    return EXIT_SUCCESS;
}

//------- GESTION DE MARCOS -------//

int asignar_marco_libre(int tamanio_proceso) {

    // Si el tamaño es 0, no asignar marco
    if (tamanio_proceso == 0) {
        log_debug(memoria_logger, "Proceso con tamaño 0 - No se asigna marco");
        return -1; // Indicador de que no se necesita marco
    }

    pthread_mutex_lock(&mutex_marcos);
    
    for (int i = 0; i < total_marcos; i++) {
        if (marcos_libres[i]) {
            marcos_libres[i] = false;
            pthread_mutex_unlock(&mutex_marcos);
            log_debug(memoria_logger, "Marco %d asignado", i);
            return i;
        }
    }
    
    pthread_mutex_unlock(&mutex_marcos);
    log_warning(memoria_logger, "No hay marcos libres disponibles");
    return -1;
}

void liberar_marco(int marco) {
    if (marco >= 0 && marco < total_marcos) {
        pthread_mutex_lock(&mutex_marcos);
        marcos_libres[marco] = true;
        pthread_mutex_unlock(&mutex_marcos);
        log_debug(memoria_logger, "Marco %d liberado", marco);
    }
}

int contar_marcos_libres() {
    pthread_mutex_lock(&mutex_marcos);
    
    int count = 0;
    for (int i = 0; i < total_marcos; i++) {
        if (marcos_libres[i]) {
            count++;
        }
    }
    
    pthread_mutex_unlock(&mutex_marcos);
    return count;
}

//------- OPERACIONES DE MEMORIA -------//

void simular_retardo_memoria() {
    if (RETARDO_MEMORIA > 0) {
        usleep(RETARDO_MEMORIA * 1000);
    }
}

int leer_memoria(int direccion_fisica, void* buffer, int tamanio) {
    if (direccion_fisica < 0 || direccion_fisica + tamanio > TAM_MEMORIA) {
        log_error(memoria_logger, "Dirección física fuera de rango: %d", direccion_fisica);
        return -1;
    }
    
    simular_retardo_memoria();
    
    // Simular lectura de memoria física
    memcpy(buffer, memoria_fisica + direccion_fisica, tamanio);
    log_debug(memoria_logger, "Leyendo %d bytes desde dirección física %d", tamanio, direccion_fisica);
    
    return 0;
}

int escribir_memoria(int direccion_fisica, void* datos, int tamanio) {
    if (direccion_fisica < 0 || direccion_fisica + tamanio > TAM_MEMORIA) {
        log_error(memoria_logger, "Dirección física fuera de rango: %d", direccion_fisica);
        return -1;
    }
    
    simular_retardo_memoria();
    
    // Simular escritura en memoria física
    memcpy(memoria_fisica + direccion_fisica, datos, tamanio);
    log_debug(memoria_logger, "Escribiendo %d bytes en dirección física %d", tamanio, direccion_fisica);
    
    return 0;
}

int traducir_direccion(int pid, int direccion_virtual) {
    char pid_str[32];
    sprintf(pid_str, "%d", pid);
    
    t_proceso* proceso = dictionary_get(procesos_activos, pid_str);
    if (!proceso) {
        log_error(memoria_logger, "Proceso PID %d no encontrado", pid);
        return -1;
    }
    
    int pagina_virtual = direccion_virtual / TAM_PAGINA;
    int desplazamiento = direccion_virtual % TAM_PAGINA;
    
    // Buscar la entrada de página
    int indice = pagina_virtual % ENTRADAS_POR_TABLA;
    if (indice >= ENTRADAS_POR_TABLA || !proceso->tabla_paginas->entradas[indice]) {
        log_error(memoria_logger, "Página virtual %d no mapeada para PID %d", pagina_virtual, pid);
        return -1;
    }
    
    t_entrada_pagina* entrada = (t_entrada_pagina*)proceso->tabla_paginas->entradas[indice];
    if (!entrada->presente) {
        log_warning(memoria_logger, "Página virtual %d no presente en memoria para PID %d", pagina_virtual, pid);
        return -1; // Page fault
    }
    
    int direccion_fisica = entrada->marco * TAM_PAGINA + desplazamiento;
    
    // Marcar como referenciada
    entrada->referenciada = true;
    
    log_debug(memoria_logger, "Dirección virtual %d (PID %d) traducida a dirección física %d", 
              direccion_virtual, pid, direccion_fisica);
    
    return direccion_fisica;
}

t_entrada_pagina* buscar_entrada_pagina(t_tabla_paginas* tabla, int nro_pagina, int niveles_totales, int entradas_por_tabla) {
    t_tabla_paginas* tabla_actual = tabla;

    for (int nivel = 0; nivel < niveles_totales - 1; nivel++) {
        int indice = (nro_pagina / (int)pow(entradas_por_tabla, (niveles_totales - nivel - 1))) % entradas_por_tabla;
        t_entrada_pagina* entrada = tabla_actual->entradas[indice];
        tabla_actual = (t_tabla_paginas*) entrada->subtabla;
    }

    int indice_final = nro_pagina % entradas_por_tabla;
    return tabla_actual->entradas[indice_final];
}

//------- OPERACIONES DE SWAP -------//

void simular_retardo_swap() {
    if (RETARDO_SWAP > 0) {
        usleep(RETARDO_SWAP * 1000);
    }
}

int leer_pagina_swap(int pid, int pagina_virtual, void* buffer) {
    if (!swap_file) {
        log_error(memoria_logger, "Archivo de swap no disponible");
        return -1;
    }
    
    simular_retardo_swap();
    
    // Calcular posición en el archivo de swap
    long posicion = (long)pid * 1000 + pagina_virtual * TAM_PAGINA;
    
    if (fseek(swap_file, posicion, SEEK_SET) != 0) {
        log_error(memoria_logger, "Error al posicionar en archivo de swap");
        return -1;
    }
    
    if (fread(buffer, TAM_PAGINA, 1, swap_file) != 1) {
        log_error(memoria_logger, "Error al leer página desde swap");
        return -1;
    }
    
    log_debug(memoria_logger, "Página virtual %d del proceso PID %d leída desde swap", pagina_virtual, pid);
    
    return 0;
}

int escribir_pagina_swap(int pid, int pagina_virtual, void* datos) {
    if (!swap_file) {
        log_error(memoria_logger, "Archivo de swap no disponible");
        return -1;
    }
    
    pthread_mutex_lock(&mutex_swap);
    
    simular_retardo_swap();
    
    // Calcular posición en el archivo de swap
    long posicion = (long)pid * 1000 + pagina_virtual * TAM_PAGINA;
    
    if (fseek(swap_file, posicion, SEEK_SET) != 0) {
        log_error(memoria_logger, "Error al posicionar en archivo de swap");
        pthread_mutex_unlock(&mutex_swap);
        return -1;
    }
    
    if (fwrite(datos, TAM_PAGINA, 1, swap_file) != 1) {
        log_error(memoria_logger, "Error al escribir página en swap");
        pthread_mutex_unlock(&mutex_swap);
        return -1;
    }
    
    fflush(swap_file);
    pthread_mutex_unlock(&mutex_swap);
    
    log_debug(memoria_logger, "Página virtual %d del proceso PID %d escrita en swap", pagina_virtual, pid);
    
    return 0;
}

void liberar_paginas_swap(int pid, int cantidad_paginas) {
    if (!swap_file) return;

    pthread_mutex_lock(&mutex_swap);

    long posicion = (long)pid * 1000;
    char* buffer_vacio = calloc(1, TAM_PAGINA);

    fseek(swap_file, posicion, SEEK_SET);

    for (int i = 0; i < cantidad_paginas; i++) {
        fwrite(buffer_vacio, TAM_PAGINA, 1, swap_file);
    }

    fflush(swap_file);
    pthread_mutex_unlock(&mutex_swap);

    log_info(memoria_logger, "Liberadas %d páginas del PID %d en SWAP", cantidad_paginas, pid);
    free(buffer_vacio);
}

int reemplazar_pagina(int pid, int pagina_virtual, void* pagina_nueva) {
    int total = list_size(marcos_en_uso);
    int vueltas = 0;

    while (vueltas < 2) {
        for (int i = 0; i < total; i++) {
            t_marco_en_uso* entrada = list_get(marcos_en_uso, puntero_clock);

            t_proceso* proceso = dictionary_get(procesos_activos, string_itoa(entrada->pid));
            t_entrada_pagina* pagina = buscar_entrada_pagina(proceso->tabla_paginas, entrada->pagina_virtual, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);

            // Primera vuelta: Referenciada = 0, Modificada = 0
            if (vueltas == 0 && !pagina->referenciada && !pagina->modificada) {
                return hacer_reemplazo(entrada, pid, pagina_virtual, pagina_nueva);
            }

            // Segunda vuelta: Referenciada = 0 (modificada o no)
            if (vueltas == 1 && !pagina->referenciada) {
                return hacer_reemplazo(entrada, pid, pagina_virtual, pagina_nueva);
            }

            // Si referenciada = 1, la pasamos a 0 para la próxima vuelta
            pagina->referenciada = false;

            puntero_clock = (puntero_clock + 1) % total;
        }
        vueltas++;
    }

    log_error(memoria_logger, "No se encontró una página para reemplazar tras 2 vueltas");
    return -1;
}

int hacer_reemplazo(t_marco_en_uso* victima, int nuevo_pid, int nueva_pagina_virtual, void* pagina_nueva) {
    // Si estaba modificada, escribir en swap
    if (victima->bit_modificado) {
        void* pagina_a_escribir = malloc(TAM_PAGINA);
        memcpy(pagina_a_escribir, memoria_fisica + victima->marco * TAM_PAGINA, TAM_PAGINA);
        escribir_pagina_swap(victima->pid, victima->pagina_virtual, pagina_a_escribir);
        free(pagina_a_escribir);
        log_info(memoria_logger, "Página modificada de PID %d, página %d escrita en swap", victima->pid, victima->pagina_virtual);
    }

    // Marcar página vieja como no presente
    t_proceso* victima_proceso = dictionary_get(procesos_activos, string_itoa(victima->pid));
    t_entrada_pagina* entrada_vieja = buscar_entrada_pagina(victima_proceso->tabla_paginas, victima->pagina_virtual, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);
    entrada_vieja->presente = false;
    entrada_vieja->marco = -1;

    // Cargar página nueva en el marco reemplazado
    memcpy(memoria_fisica + victima->marco * TAM_PAGINA, pagina_nueva, TAM_PAGINA);

    // Actualizar entrada de página nueva
    t_proceso* nuevo_proceso = dictionary_get(procesos_activos, string_itoa(nuevo_pid));
    t_entrada_pagina* nueva_entrada = buscar_entrada_pagina(nuevo_proceso->tabla_paginas, nueva_pagina_virtual, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);
    nueva_entrada->presente = true;
    nueva_entrada->marco = victima->marco;
    nueva_entrada->referenciada = true;
    nueva_entrada->modificada = false;

    // Actualizar metadata del marco
    victima->pid = nuevo_pid;
    victima->pagina_virtual = nueva_pagina_virtual;
    victima->bit_modificado = false;

    log_info(memoria_logger, "Reemplazo de página exitoso. Página %d de PID %d ocupó marco %d", nueva_pagina_virtual, nuevo_pid, victima->marco);
    return victima->marco;
}

void liberar_entrada(t_entrada_pagina* entrada) {
    if (entrada == NULL) return;

    if (entrada->presente) {
        liberar_marco(entrada->marco);  // marco vuelve a estar disponible
    }

    if (entrada->subtabla) {
        t_tabla_paginas* subtabla = (t_tabla_paginas*) entrada->subtabla;
        for (int i = 0; i < ENTRADAS_POR_TABLA; i++) {
            if (subtabla->entradas[i]) {
                liberar_entrada(subtabla->entradas[i]);
            }
        }
        free(subtabla->entradas);
        free(subtabla);
    }

    free(entrada);
}

void liberar_tabla_paginas(t_tabla_paginas* tabla) {
    if (tabla == NULL) return;

    for (int i = 0; i < ENTRADAS_POR_TABLA; i++) {
        if (tabla->entradas[i]) {
            liberar_entrada(tabla->entradas[i]);
        }
    }

    free(tabla->entradas);
    free(tabla);
}

void finalizar_proceso(int pid) {
    char pid_key[16];
    sprintf(pid_key, "%d", pid);

    pthread_mutex_lock(&mutex_procesos);
    t_proceso* proceso = dictionary_remove(procesos_activos, pid_key);
    pthread_mutex_unlock(&mutex_procesos);

    if (!proceso) {
        log_warning(memoria_logger, "Se intentó finalizar un proceso inexistente con PID %d", pid);
        return;
    }

    liberar_tabla_paginas(proceso->tabla_paginas);
    free(proceso);

    log_info(memoria_logger, "Proceso PID %d finalizado y recursos liberados correctamente.", pid);
}

// Función auxiliar para escribir el encabezado del dump
void escribir_encabezado_dump(FILE* dump_file, int pid, const char* timestamp, const char* estado) {
    fprintf(dump_file, "\n=== Dump de Proceso PID %d - %s ===\n", pid, timestamp);
    fprintf(dump_file, "Estado: %s\n", estado);
}

// Función auxiliar para escribir el pie del dump
void escribir_pie_dump(FILE* dump_file, int pid) {
    fprintf(dump_file, "=== Fin Dump PID %d ===\n\n", pid);
}

char* generar_nombre_dump(int pid) {
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_info);
    
    char* nombre = malloc(strlen(DUMP_PATH) + 20 + 10); // PID + timestamp + extension
    sprintf(nombre, "%s/%d-%s.dmp", DUMP_PATH, pid, timestamp);
    
    return nombre;
}

void volcar_memoria_proceso(FILE* dump_file, t_proceso* proceso) {
    // Iterar por todas las páginas del proceso
    for (int i = 0; i < ENTRADAS_POR_TABLA; i++) {
        t_entrada_pagina* entrada = proceso->tabla_paginas->entradas[i];
        if (!entrada) continue;
        
        if (entrada->presente) {
            // Página está en memoria física
            void* marco = memoria_fisica + (entrada->marco * TAM_PAGINA);
            fwrite(marco, TAM_PAGINA, 1, dump_file);
        } else {
            // Página está en swap, leerla primero
            void* buffer = malloc(TAM_PAGINA);
            if (leer_pagina_swap(proceso->pid, i, buffer) == 0) {
                fwrite(buffer, TAM_PAGINA, 1, dump_file);
            } else {
                // Escribir ceros si no se puede leer de swap
                memset(buffer, 0, TAM_PAGINA);
                fwrite(buffer, TAM_PAGINA, 1, dump_file);
            }
            free(buffer);
        }
    }
}

int calcular_tamano_proceso(t_proceso* proceso) {
    
    // Si el proceso fue creado con tamaño 0, devolver 0
    if (proceso->tamanio == 0) {
        return 0;
    }
    
    // Calcular tamaño normal para otros procesos
    int paginas_asignadas = 0;
    
    for (int i = 0; i < ENTRADAS_POR_TABLA; i++) {
        if (proceso->tabla_paginas->entradas[i]) {
            paginas_asignadas++;
        }
    }
    
    return paginas_asignadas * TAM_PAGINA;
}

// Función principal para generar el dump de un proceso específico

bool dump_proceso(int pid) {
    char pid_key[16];
    sprintf(pid_key, "%d", pid);

    pthread_mutex_lock(&mutex_procesos);
    
    // Obtener proceso
    t_proceso* proceso = dictionary_get(procesos_activos, pid_key);
    if (!proceso) {
        proceso = dictionary_get(procesos_suspendidos, pid_key);
    }
    
    if (!proceso) {
        pthread_mutex_unlock(&mutex_procesos);
        log_error(memoria_logger, "Proceso PID %d no encontrado para dump", pid);
        return false;
    }

    // Generar nombre de archivo único
    char* dump_filename = generar_nombre_dump(pid);
    
    // Abrir archivo de dump
    FILE* dump_file = fopen(dump_filename, "wb"); // Modo binario para volcar memoria
    if (!dump_file) {
        pthread_mutex_unlock(&mutex_procesos);
        log_error(memoria_logger, "No se pudo crear archivo de dump: %s", strerror(errno));
        free(dump_filename);
        return false;
    }

    // 1. Escribir información del proceso
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    const char* estado = dictionary_get(procesos_activos, pid_key) ? "ACTIVO" : "SUSPENDIDO";
    
    fprintf(dump_file, "=== Dump de Proceso PID %d - %s ===\n", pid, timestamp);
    fprintf(dump_file, "Estado: %s\n", estado);
    fprintf(dump_file, "Tamaño total: %d bytes\n", calcular_tamano_proceso(proceso));
    
    // 2. Volcar contenido de memoria del proceso
    volcar_memoria_proceso(dump_file, proceso);
    
    fprintf(dump_file, "=== Fin Dump PID %d ===\n", pid);

    // Liberar recursos
    fclose(dump_file);
    free(dump_filename);
    pthread_mutex_unlock(&mutex_procesos);
    
    log_info(memoria_logger, "Dump del proceso PID %d generado correctamente", pid);
    return true;
}

void _dump_informacion_general(FILE* dump_file) {
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(dump_file, "=== Dump General de Memoria - %s ===\n\n", timestamp);

    // Información general del sistema
    fprintf(dump_file, "Configuración:\n");
    fprintf(dump_file, "  Tamaño memoria: %d bytes\n", TAM_MEMORIA);
    fprintf(dump_file, "  Tamaño página: %d bytes\n", TAM_PAGINA);
    fprintf(dump_file, "  Marcos totales: %d\n", total_marcos);
    fprintf(dump_file, "  Marcos libres: %d\n", contar_marcos_libres());
    fprintf(dump_file, "  Procesos activos: %d\n", dictionary_size(procesos_activos));
    fprintf(dump_file, "  Procesos suspendidos: %d\n\n", dictionary_size(procesos_suspendidos));
}

void _dump_cada_proceso(char* pid_key, void* proceso_ptr) {
    (void)proceso_ptr;  // Evita warning
    int pid = atoi(pid_key);
    dump_proceso(pid);
}

void _dump_procesos() {
    pthread_mutex_lock(&mutex_procesos);

    dictionary_iterator(procesos_activos, _dump_cada_proceso);
    dictionary_iterator(procesos_suspendidos, _dump_cada_proceso);

    pthread_mutex_unlock(&mutex_procesos);
}

void dump_memoria() {
    FILE* dump_file = fopen(DUMP_PATH, "w");
    if (!dump_file) {
        log_error(memoria_logger, "No se pudo abrir archivo de dump: %s", strerror(errno));
        return;
    }

    _dump_informacion_general(dump_file);
    fclose(dump_file);
    
    _dump_procesos();

    log_info(memoria_logger, "Dump de memoria generado en %s", DUMP_PATH);
}

void suspender_proceso(int pid) {
    char pid_key[16];
    sprintf(pid_key, "%d", pid);

    pthread_mutex_lock(&mutex_procesos);
    t_proceso* proceso = dictionary_remove(procesos_activos, pid_key);
    pthread_mutex_unlock(&mutex_procesos);

    if (!proceso) {
        log_warning(memoria_logger, "No se encontró el proceso %d para suspender", pid);
        return;
    }

    t_tabla_paginas* tabla = proceso->tabla_paginas;

    for (int i = 0; i < ENTRADAS_POR_TABLA; i++) {
        t_entrada_pagina* entrada = tabla->entradas[i];
        if (entrada && entrada->presente) {
            void* pagina = malloc(TAM_PAGINA);
            memcpy(pagina, memoria_fisica + entrada->marco * TAM_PAGINA, TAM_PAGINA);

            escribir_pagina_swap(pid, i, pagina);
            free(pagina);

            liberar_marco(entrada->marco);
            entrada->presente = false;
            entrada->marco = -1;
        }
    }

    pthread_mutex_lock(&mutex_procesos);
    dictionary_put(procesos_suspendidos, pid_key, proceso);
    pthread_mutex_unlock(&mutex_procesos);

    log_info(memoria_logger, "Proceso PID %d suspendido correctamente", pid);
}

void reactivar_proceso(int pid) {
    char pid_key[16];
    sprintf(pid_key, "%d", pid);

    pthread_mutex_lock(&mutex_procesos);
    t_proceso* proceso = dictionary_remove(procesos_suspendidos, pid_key);
    pthread_mutex_unlock(&mutex_procesos);

    if (!proceso) {
        log_warning(memoria_logger, "No se encontró el proceso %d en suspendidos para reactivar", pid);
        return;
    }

    pthread_mutex_lock(&mutex_procesos);
    dictionary_put(procesos_activos, pid_key, proceso);
    pthread_mutex_unlock(&mutex_procesos);

    log_info(memoria_logger, "Proceso PID %d reactivado y vuelto a memoria activa", pid);
}


//------- HILOS DE ATENCION -------//

//Recibir y atender pedidos de KERNEL

void procesar_envio_archivo_kernel(int fd_kernel) {
    log_info(memoria_logger, "Recibido cod_op ENVIO_ARCHIVO_KERNEL de kernel");

    t_list* lista = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
    
    if (lista == NULL) {
        log_error(memoria_logger, "Error: recibir_paquete devolvió NULL");
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        return;
    }

    log_info(memoria_logger, "Paquete recibido de kernel con %d elementos", list_size(lista));

    if (list_size(lista) >= 2) {
        log_info(memoria_logger, "Me llegaron los siguientes valores:");

        char* nombre_archivo = list_get(lista, 0);
        char* tamanio_str = list_get(lista, 1);
        int tamanio_proceso = atoi(tamanio_str);

        // Generar PID automáticamente (secuencial desde 0)
        int ultimo_pid = -1; // Variable para mantener el último PID asignado
        int pid = ++ultimo_pid;

        log_info(memoria_logger, "Creando proceso - PID: %d, Archivo: %s, Tamaño: %d", pid, nombre_archivo, tamanio_proceso);

        if (nombre_archivo == NULL) {
            log_error(memoria_logger, "Error: elementos del paquete son NULL");
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }
    
        log_info(memoria_logger, "Archivo: %s, Tamaño proceso: %d", nombre_archivo, tamanio_proceso);

        char path_completo[256];
        snprintf(path_completo, sizeof(path_completo), "../utils/pruebas/revenge-of-the-cth-pruebas/%s", nombre_archivo);

        // Caso especial para tamaño 0
        if (tamanio_proceso == 0) {
            log_info(memoria_logger, "Proceso con tamaño 0 - No se verificará espacio en memoria");
            
            // Resto del código para manejar el archivo...
        }

        log_info(memoria_logger, "Path completo: %s", path_completo);

        log_info(memoria_logger, "Guardando path en variable global");

        if (access(path_completo, F_OK) == -1) {
            log_error(memoria_logger, "El archivo no existe en la ruta: %s", path_completo);
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }
        
        if (access(path_completo, R_OK) == -1) {
            log_error(memoria_logger, "No hay permisos para leer el archivo: %s", path_completo);
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }

        // Crear estructura del proceso
        t_proceso* nuevo_proceso = malloc(sizeof(t_proceso));
        if (!nuevo_proceso) {
            log_error(memoria_logger, "Error al asignar memoria para proceso");
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }

        nuevo_proceso->pid = pid;
        nuevo_proceso->tamanio = tamanio_proceso;
        nuevo_proceso->tabla_paginas = crear_tablas_paginas_multinivel(0, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);

        log_info(memoria_logger, "Creando tabla de páginas para PID %d, tamanio %d, niveles %d, entradas por tabla %d", pid, tamanio_proceso, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);

        if (!nuevo_proceso->tabla_paginas) {
            free(nuevo_proceso);
            log_error(memoria_logger, "Error al crear tabla de páginas");
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }

        // Asignar marcos si es necesario (tamaño > 0)
        if (tamanio_proceso > 0) {
            log_info(memoria_logger, "Asignando marcos físicos para PID %d, por ser tamanio_proceso >0 : %d", pid, tamanio_proceso);

            int marcos_necesarios = ceil((double)tamanio_proceso / TAM_PAGINA);
            int marcos_libres = contar_marcos_libres();

            log_info(memoria_logger, "Marcos necesarios: %d, Marcos libres: %d", marcos_necesarios, marcos_libres);
            
            if (marcos_libres < marcos_necesarios) {
                log_warning(memoria_logger, "No hay suficiente espacio (Necesita %d marcos, hay %d)", marcos_necesarios, marcos_libres);
                liberar_tabla_paginas(nuevo_proceso->tabla_paginas);
                free(nuevo_proceso);
                bool confirmacion = false;
                send(fd_kernel, &confirmacion, sizeof(bool), 0);
                list_destroy_and_destroy_elements(lista, free);
                return;
            }

            // Asignar marcos físicos
            for (int i = 0; i < marcos_necesarios; i++) {
                log_info(memoria_logger, "Asignando marco %d para PID %d", i, pid);
                // Asignar marco libre
                int marco = asignar_marco_libre(tamanio_proceso);
                if (marco == -1) {
                    log_error(memoria_logger, "Error al asignar marco %d para PID %d", i, pid);
                    // Liberar marcos ya asignados
                    for (int j = 0; j < i; j++) {
                        t_entrada_pagina* entrada = buscar_entrada_pagina(
                            nuevo_proceso->tabla_paginas, j, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);
                            log_info(memoria_logger, "Liberando marco %d para PID %d", entrada->marco, pid);
                        if (entrada && entrada->presente) {
                            liberar_marco(entrada->marco);
                        }
                    }
                    liberar_tabla_paginas(nuevo_proceso->tabla_paginas);
                    free(nuevo_proceso);
                    bool confirmacion = false;
                    send(fd_kernel, &confirmacion, sizeof(bool), 0);
                    list_destroy_and_destroy_elements(lista, free);

                    log_error(memoria_logger, "No se pudo asignar marco para PID %d", pid);

                    return;
                }

                // Configurar entrada de página
                t_entrada_pagina* entrada = buscar_entrada_pagina(nuevo_proceso->tabla_paginas, i, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);
                entrada->marco = marco;
                entrada->presente = true;

                log_info(memoria_logger, "Marco %d asignado a PID %d, página virtual %d", marco, pid, i);

                // Registrar marco en uso
                t_marco_en_uso* marco_uso = malloc(sizeof(t_marco_en_uso));
                marco_uso->marco = marco;
                marco_uso->pid = pid;
                marco_uso->pagina_virtual = i;
                marco_uso->bit_modificado = false;
                list_add(marcos_en_uso, marco_uso);

                log_info(memoria_logger, "Marco %d registrado en marcos_en_uso para PID %d, página virtual %d", marco, pid, i);
            }
        }

        // Guardar proceso en diccionario
        char pid_key[16];
        sprintf(pid_key, "%d", pid);

        log_info(memoria_logger, "Guardando proceso PID %d en diccionario de procesos activos", pid);
        
        pthread_mutex_lock(&mutex_procesos);
        dictionary_put(procesos_activos, pid_key, nuevo_proceso);
        pthread_mutex_unlock(&mutex_procesos);

        // Configurar archivo de instrucciones
        strncpy(path_instrucciones, path_completo, sizeof(path_instrucciones));
        log_info(memoria_logger, "Abriendo archivo de instrucciones");
        archivo_instrucciones = fopen(path_completo, "r");

        // Después de abrir el archivo
        if (archivo_instrucciones == NULL) {
            log_error(memoria_logger, "Error al abrir archivo: %s", strerror(errno));
            // Limpiar proceso creado si es necesario
            if (pid != -1) {
                finalizar_proceso(pid);
            }
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }

        // Verificar que el archivo no esté vacío
        fseek(archivo_instrucciones, 0, SEEK_END);
        long tam = ftell(archivo_instrucciones);
        rewind(archivo_instrucciones);
        
        if (tam <= 0) {
            log_error(memoria_logger, "Archivo vacío");
            fclose(archivo_instrucciones);
            archivo_instrucciones = NULL;
            finalizar_proceso(pid);
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }
        
        log_info(memoria_logger, "Archivo abierto exitosamente");
        mostrar_contenido_archivo(archivo_instrucciones, memoria_logger);
    
        bool confirmacion = true;
        log_info(memoria_logger, "Enviando confirmación de carga de archivo a kernel");
        send(fd_kernel, &confirmacion, sizeof(bool), 0);

        sem_post(&sem_archivo_listo);

    } else {
        log_warning(memoria_logger, "Paquete incompleto recibido de kernel");
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        list_destroy_and_destroy_elements(lista, free);
        return;
    }

    log_info(memoria_logger, "Procesamiento de ENVIO_ARCHIVO_KERNEL finalizado, en espera de nuevas operaciones");

    // Liberar lista
    list_destroy_and_destroy_elements(lista, free);
}

void procesar_consultar_espacio(int fd_kernel) {
    log_info(memoria_logger, "Procesando CONSULTAR_ESPACIO desde kernel");
    
    t_list* lista = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
    
    if (lista == NULL || list_size(lista) < 3) {
        log_error(memoria_logger, "Error al recibir datos para consultar espacio");
        bool respuesta = false;
        send(fd_kernel, &respuesta, sizeof(bool), 0);
        if (lista) list_destroy_and_destroy_elements(lista, free);
        return;
    }
    
    // Obtener PID, tamaño y nombre del proceso
    char* pid_str = list_get(lista, 0);
    char* tamanio_str = list_get(lista, 1);
    char* longitud_nombre_str = list_get(lista, 2);
    
    int pid = atoi(pid_str);
    int tamanio = atoi(tamanio_str);
    int longitud_nombre = atoi(longitud_nombre_str);
    (void)longitud_nombre; // Evita warning si no se usa

    char* nombre_proceso = NULL;
    if (list_size(lista) > 3) {
        nombre_proceso = list_get(lista, 3);
    }
    
    log_info(memoria_logger, "Consultando espacio para proceso PID=%d, tamaño=%d bytes, nombre=%s", 
             pid, tamanio, nombre_proceso ? nombre_proceso : "N/A");
    
    // Verificar espacio disponible
    bool hay_espacio = false;
    
    if (tamanio == 0) {
        // Proceso de tamaño 0 siempre puede ser creado
        hay_espacio = true;
        log_info(memoria_logger, "Proceso de tamaño 0 - espacio disponible");
    } else {
        // Calcular marcos necesarios
        int marcos_necesarios = ceil((double)tamanio / TAM_PAGINA);
        int marcos_disponibles = contar_marcos_libres();
        
        hay_espacio = (marcos_disponibles >= marcos_necesarios);
        
        log_info(memoria_logger, "Marcos necesarios: %d, marcos disponibles: %d, espacio suficiente: %s", 
                 marcos_necesarios, marcos_disponibles, hay_espacio ? "SÍ" : "NO");
    }
    
    // Enviar respuesta al kernel
    send(fd_kernel, &hay_espacio, sizeof(bool), 0);
    
    log_info(memoria_logger, "Respuesta enviada a kernel: %s", hay_espacio ? "ESPACIO_DISPONIBLE" : "SIN_ESPACIO");
    
    list_destroy_and_destroy_elements(lista, free);
}

void procesar_crear_proceso(int fd_kernel) {

    t_list* lista = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
    
    if (lista == NULL || list_size(lista) < 1) {
        log_error(memoria_logger, "Error al recibir PID para crear proceso");
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        if (lista) list_destroy_and_destroy_elements(lista, free);
        return;
    }
    
    // Obtener PID y tamaño
    char* pid_str = list_get(lista, 0);
    char* tamanio_str = list_get(lista, 1);
    int pid = atoi(pid_str);
    int tamanio = atoi(tamanio_str);

    // Caso especial para tamaño 0
    if (tamanio == 0) {
        log_info(memoria_logger, "Proceso PID %d creado con tamaño 0 - No se asignarán páginas", pid);
        
        // Crear estructura de proceso sin páginas
        t_proceso* nuevo_proceso = malloc(sizeof(t_proceso));
        nuevo_proceso->pid = pid;
        nuevo_proceso->tamanio = 0;
        nuevo_proceso->tabla_paginas = crear_tablas_paginas_multinivel(0, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);
        
        // Agregar al diccionario global
        char pid_key[16];
        sprintf(pid_key, "%d", pid);
        
        pthread_mutex_lock(&mutex_procesos);
        dictionary_put(procesos_activos, pid_key, nuevo_proceso);
        pthread_mutex_unlock(&mutex_procesos);
        
        // Confirmar al Kernel
        bool confirmacion = true;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        list_destroy_and_destroy_elements(lista, free);
        return;
    }
    
    // Verificar espacio disponible
    int marcos_necesarios = ceil((double)tamanio / TAM_PAGINA);
    bool asignacion_exitosa = true;
    if (contar_marcos_libres() < marcos_necesarios) {
        log_warning(memoria_logger, "No hay suficiente espacio para proceso PID %d (necesita %d marcos)", 
                   pid, marcos_necesarios);
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        list_destroy_and_destroy_elements(lista, free);
        return;
    }

    // Crear estructura de proceso
    t_proceso* nuevo_proceso = malloc(sizeof(t_proceso));
    if (!nuevo_proceso) {
        log_error(memoria_logger, "Error al alocar memoria para nuevo proceso.");
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        list_destroy_and_destroy_elements(lista, free);
        return;
    }

    nuevo_proceso->pid = pid;
    nuevo_proceso->tamanio = tamanio;  // Almacenar el tamaño
    nuevo_proceso->tabla_paginas = crear_tablas_paginas_multinivel(0, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);

    if (!nuevo_proceso->tabla_paginas) {
        free(nuevo_proceso);
        log_error(memoria_logger, "Error al crear tabla de páginas para el proceso %d", pid);
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        list_destroy_and_destroy_elements(lista, free);
        return;
    }

    log_info(memoria_logger, "Creando proceso PID %d con tamaño %d bytes", pid, tamanio);

    // Asignar marcos de memoria
    for (int i = 0; i < marcos_necesarios; i++) {
            int marco = asignar_marco_libre(tamanio);
            if (marco == -1) {
                log_error(memoria_logger, "Error al asignar marco %d para PID %d", i, pid);
                asignacion_exitosa = false;
                // Liberar marcos ya asignados
                for (int j = 0; j < i; j++) {
                    liberar_marco(nuevo_proceso->tabla_paginas->entradas[j]->marco);
                }
                break;
            }
            
            // Asignar marco a la entrada de página correspondiente
            t_entrada_pagina* entrada = buscar_entrada_pagina(nuevo_proceso->tabla_paginas, i, CANTIDAD_NIVELES, ENTRADAS_POR_TABLA);
            entrada->marco = marco;
            entrada->presente = true;
            
            // Registrar marco en uso
            t_marco_en_uso* marco_uso = malloc(sizeof(t_marco_en_uso));
            marco_uso->marco = marco;
            marco_uso->pid = pid;
            marco_uso->pagina_virtual = i;
            marco_uso->bit_modificado = false;
            list_add(marcos_en_uso, marco_uso);
        }
        
        if (!asignacion_exitosa) {
            liberar_tabla_paginas(nuevo_proceso->tabla_paginas);
            free(nuevo_proceso);
            bool confirmacion = false;
            send(fd_kernel, &confirmacion, sizeof(bool), 0);
            list_destroy_and_destroy_elements(lista, free);
            return;
        }

    // Agregar al diccionario global
    char pid_key[16];
    sprintf(pid_key, "%d", pid);

    pthread_mutex_lock(&mutex_procesos);
    dictionary_put(procesos_activos, pid_key, nuevo_proceso);
    pthread_mutex_unlock(&mutex_procesos);

    log_info(memoria_logger, "Proceso PID %d creado exitosamente", pid);

    // Confirmar al Kernel
    bool confirmacion = true;
    send(fd_kernel, &confirmacion, sizeof(bool), 0);

    list_destroy_and_destroy_elements(lista, free);
}

void procesar_dump_memory(int fd_kernel) {
    t_list* lista = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
    
    if (!lista || list_size(lista) < 1) {
        log_error(memoria_logger, "Error al recibir PID para dump memory");
        bool confirmacion = false;
        send(fd_kernel, &confirmacion, sizeof(bool), 0);
        if (lista) list_destroy_and_destroy_elements(lista, free);
        return;
    }

    char* pid_str = list_get(lista, 0);
    int pid = atoi(pid_str);
    list_destroy_and_destroy_elements(lista, free);

    // Realizar el dump del proceso específico
    bool resultado = dump_proceso(pid);

    // Enviar confirmación al Kernel
    send(fd_kernel, &resultado, sizeof(bool), 0);

    if (resultado) {
        log_info(memoria_logger, "Dump del proceso PID %d completado exitosamente", pid);
    } else {
        log_error(memoria_logger, "Error al realizar dump del proceso PID %d", pid);
    }
}

// Función principal MEMORIA-KERNEL
void* atender_memoria_kernel(void* arg) {
    int* fd_kernel_ptr = (int*)arg;
    int fd_kernel = *fd_kernel_ptr;

    log_info(memoria_logger, "Cliente kernel %d conectado. Esperando operaciones...", fd_kernel);

    while (true) {

        pthread_mutex_lock(&mutex_archivo);

        int cod_op = recibir_operacion(memoria_logger, fd_kernel);

        log_info(memoria_logger, "Recibiendo operacion de kernel con cod_op: %d", cod_op);

        if (cod_op == -1) {
            log_error(memoria_logger, "El cliente se desconectó. Terminando servidor");
            pthread_mutex_unlock(&mutex_archivo);
            break;
        }

        switch (cod_op) {
            case HANDSHAKE:
                log_info(memoria_logger, "[KERNEL] HANDSHAKE recibido (FD: %d)", fd_kernel);
                // Procesar handshake del kernel
                t_list* lista_hs = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
                if (lista_hs) {
                    char* mensaje = list_get(lista_hs, 0);
                    log_info(memoria_logger, "Mensaje de handshake recibido: %s", mensaje ? mensaje : "NULL");
                    list_destroy_and_destroy_elements(lista_hs, free);
                }
                // Enviar confirmación de handshake
                bool confirmacion_hs = true;
                send(fd_kernel, &confirmacion_hs, sizeof(bool), 0);
                log_info(memoria_logger, "Confirmación de handshake enviada a kernel");
                break;

            case ENVIO_ARCHIVO_KERNEL:
                log_info(memoria_logger, "[KERNEL] ENVIO_ARCHIVO_KERNEL recibido (FD: %d)", fd_kernel);
                procesar_envio_archivo_kernel(fd_kernel);
                break;

            case CREAR_PROCESO:
                log_info(memoria_logger, "[KERNEL] CREAR_PROCESO recibido (FD: %d)", fd_kernel);
                procesar_crear_proceso(fd_kernel);
                break;

            case DUMP_MEMORY:
                log_info(memoria_logger, "[KERNEL] DUMP_MEMORY recibido (FD: %d)", fd_kernel);
                procesar_dump_memory(fd_kernel);
                break;

            case CONSULTAR_ESPACIO:
                log_info(memoria_logger, "[KERNEL] CONSULTAR_ESPACIO recibido (FD: %d)", fd_kernel);
                procesar_consultar_espacio(fd_kernel);
                break;
            
            case FINALIZAR_PROCESO:
            log_info(memoria_logger, "[KERNEL] FINALIZAR_PROCESO recibido (FD: %d)", fd_kernel);

                t_list* lista = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
                if (!lista || list_size(lista) < 1) {
                    log_error(memoria_logger, "Error al recibir PID para finalizar proceso");
                    if (lista) list_destroy_and_destroy_elements(lista, free);
                    break;
                }

                char* pid_str = list_get(lista, 0);
                int pid = atoi(pid_str);

                finalizar_proceso(pid);

                list_destroy_and_destroy_elements(lista, free);
                break;

            case SUSPENDER_PROCESO:
                log_info(memoria_logger, "[KERNEL] SUSPENDER_PROCESO recibido (FD: %d)", fd_kernel);
                t_list* lista_susp = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
                if (!lista_susp || list_size(lista_susp) < 1) {
                    log_error(memoria_logger, "Error al recibir PID para suspender proceso");
                    if (lista_susp) list_destroy_and_destroy_elements(lista_susp, free);
                    break;
                }
                char* pid_str_susp = list_get(lista_susp, 0);
                int pid_susp = atoi(pid_str_susp);
                
                // Suspender el proceso
                suspender_proceso(pid_susp);
                
                // Enviar confirmación al kernel
                bool confirmacion_susp = true;
                send(fd_kernel, &confirmacion_susp, sizeof(bool), 0);
                
                list_destroy_and_destroy_elements(lista_susp, free);
                break;
                
            case REACTIVAR_PROCESO:
                log_info(memoria_logger, "[KERNEL] REACTIVAR_PROCESO recibido (FD: %d)", fd_kernel);
                t_list* lista_react = recibir_paquete_desde_buffer(memoria_logger, fd_kernel);
                if (!lista_react || list_size(lista_react) < 1) {
                    log_error(memoria_logger, "Error al recibir PID para reactivar proceso");
                    if (lista_react) list_destroy_and_destroy_elements(lista_react, free);
                    break;
                }
                char* pid_str_react = list_get(lista_react, 0);
                int pid_react = atoi(pid_str_react);
                
                // Reactivar el proceso
                reactivar_proceso(pid_react);
                
                // Enviar confirmación al kernel
                bool confirmacion_react = true;
                send(fd_kernel, &confirmacion_react, sizeof(bool), 0);
                
                list_destroy_and_destroy_elements(lista_react, free);
                break;


            default:
                log_warning(memoria_logger,"Operacion desconocida desde Kernel");
            break;
        }
        
        pthread_mutex_unlock(&mutex_archivo);
        sleep(1); // Simular retardo

    }

    log_info(memoria_logger, "Cerrando conexión con kernel");

    close(fd_kernel);
    return NULL;
}


//Recibir y atender pedidos de CPU
bool procesar_solicitud_instruccion(int fd_cpu) {
    char* linea_actual = NULL;
    const int BUFFER_SIZE = 512;
    bool continuar = true;

    log_info(memoria_logger, "Recibida SOLICITUD_INSTRUCCION de CPU");

    // Leer línea del archivo de forma thread-safe
    pthread_mutex_lock(&mutex_archivo_solicitud_instruccion);

    if (archivo_instrucciones == NULL) {
        log_error(memoria_logger, "Archivo de instrucciones no está abierto");
        pthread_mutex_unlock(&mutex_archivo_solicitud_instruccion);
        
        // Enviar error a CPU
        t_paquete* paquete_error = crear_paquete(ERROR, memoria_logger);
        agregar_a_paquete(paquete_error, "Archivo no disponible", 20);
        enviar_paquete(paquete_error, fd_cpu);
        eliminar_paquete(paquete_error);
     
        return false;
    }
        
    linea_actual = entregar_linea(archivo_instrucciones, memoria_logger, BUFFER_SIZE);
    pthread_mutex_unlock(&mutex_archivo_solicitud_instruccion);

    pthread_mutex_lock(&mutex_comunicacion_cpu);

    if (linea_actual == NULL) {
        log_info(memoria_logger, "Fin del archivo de instrucciones, enviando EXIT a CPU");

        t_paquete* paquete = crear_paquete(EXIT, memoria_logger);
        enviar_paquete(paquete, fd_cpu);
        eliminar_paquete(paquete);

        pthread_mutex_lock(&mutex_archivo_instrucciones);
        if (archivo_instrucciones != NULL) {
            fclose(archivo_instrucciones);
            archivo_instrucciones = NULL;
        }
        pthread_mutex_unlock(&mutex_archivo_instrucciones);

        continuar = false;

    } else {
        log_info(memoria_logger, "Enviando LINEA de Instruccion: %s", linea_actual);

        t_paquete* paquete_instr = crear_paquete(PAQUETE, memoria_logger);
        agregar_a_paquete(paquete_instr, linea_actual, strlen(linea_actual) + 1);
        enviar_paquete(paquete_instr, fd_cpu);
        eliminar_paquete(paquete_instr);

        log_info(memoria_logger, "Instrucción enviada a CPU: %s, strlen: %zu", linea_actual, strlen(linea_actual));

        // Liberar memoria de la línea actual
        free(linea_actual);
    }
    pthread_mutex_unlock(&mutex_comunicacion_cpu);  
    
    log_info(memoria_logger, "Solicitud procesada completamente");

    return continuar;
}

void procesar_lectura(int socket_cpu) {
    t_list* lista = recibir_paquete_desde_buffer(memoria_logger, socket_cpu);
    if (!lista || list_size(lista) < 2) {
        log_error(memoria_logger, "Error al recibir solicitud de lectura");
        if (lista) list_destroy_and_destroy_elements(lista, free);
        return;
    }

    int pid = atoi(list_get(lista, 0));
    int direccion_virtual = atoi(list_get(lista, 1));
    int direccion_fisica = traducir_direccion(pid, direccion_virtual);

    if (direccion_fisica == -1) {
        log_error(memoria_logger, "No se pudo traducir dirección para lectura");
        list_destroy_and_destroy_elements(lista, free);
        return;
    }

    int valor_leido = 0;
    leer_memoria(direccion_fisica, &valor_leido, sizeof(int));

    t_paquete* respuesta = crear_paquete(PAQUETE, memoria_logger);
    agregar_a_paquete(respuesta, &valor_leido, sizeof(int));
    enviar_paquete(respuesta, socket_cpu);
    eliminar_paquete(respuesta);

    log_info(memoria_logger, "Lectura: PID %d leyó valor %d en dirección virtual %d (física %d)", pid, valor_leido, direccion_virtual, direccion_fisica);
    list_destroy_and_destroy_elements(lista, free);
}

void procesar_escritura(int socket_cpu) {
    t_list* lista = recibir_paquete_desde_buffer(memoria_logger, socket_cpu);
    if (!lista || list_size(lista) < 3) {
        log_error(memoria_logger, "Error al recibir solicitud de escritura");
        if (lista) list_destroy_and_destroy_elements(lista, free);
        return;
    }

    int pid = atoi(list_get(lista, 0));
    int direccion_virtual = atoi(list_get(lista, 1));
    int valor = atoi(list_get(lista, 2));

    int direccion_fisica = traducir_direccion(pid, direccion_virtual);
    if (direccion_fisica == -1) {
        log_error(memoria_logger, "No se pudo traducir dirección para escritura");
        list_destroy_and_destroy_elements(lista, free);
        return;
    }

    escribir_memoria(direccion_fisica, &valor, sizeof(int));

    t_paquete* respuesta = crear_paquete(PAQUETE, memoria_logger);
    enviar_paquete(respuesta, socket_cpu);
    eliminar_paquete(respuesta);

    log_info(memoria_logger, "Escritura: PID %d escribió valor %d en dirección virtual %d (física %d)", pid, valor, direccion_virtual, direccion_fisica);
    list_destroy_and_destroy_elements(lista, free);
}

// Función principal MEMORIA-CPU
void* atender_memoria_cpu(void* arg) {
    int* fd_cpu_ptr = (int*)arg;
    int fd_cpu = *fd_cpu_ptr;
    bool continuar = true;
    int contador = 1;

    log_info(memoria_logger, "Cliente CPU %d conectado. Esperando operaciones...", fd_cpu);
    
    // Enviar confirmación de conexión
    bool confirmacion = true;
    if (send(fd_cpu, &confirmacion, sizeof(bool), MSG_NOSIGNAL) != sizeof(bool)) {
        log_error(memoria_logger, "Error enviando handshake a CPU");
        close(fd_cpu);
        return NULL;
    }
    log_info(memoria_logger, "Enviado handshake a CPU");

    sem_post(&sem_cpu_memoria_hs);

    log_info(memoria_logger, "Esperando archivo de Instrucciones de Kernel...");

    sem_wait(&sem_archivo_listo);

    // Dar tiempo para sincronización
    usleep(100000); // 100ms

    log_info(memoria_logger, "Archivo de Instrucciones recibido y listo para procesar");

    t_paquete* paquete_listo = crear_paquete(ARCHIVO_LISTO, memoria_logger);
    enviar_paquete(paquete_listo, fd_cpu);
    eliminar_paquete(paquete_listo);
    
    log_info(memoria_logger, "Enviado a CPU el Estado: ARCHIVO_LISTO, Esperando SOLICITUD_INSTRUCCION de CPU...");
    
    // Agregar una pausa para dar tiempo a que CPU procese el ARCHIVO_LISTO
    usleep(500000); // 500ms

    while(continuar){

        log_debug(memoria_logger, "Estado actual Contador: %d", contador);

        // Esperar nueva solicitud de CPU
        int cod_op = recibir_operacion(memoria_logger, fd_cpu);
        log_info(memoria_logger, "Recibiendo operacion de CPU con cod_op: %d", cod_op);

        if (cod_op == -1) {
            log_error(memoria_logger, "CPU se desconectó");
            continuar = false;
            break;
        }

        if(cod_op == 0) {   
            log_warning(memoria_logger, "Recibido cod_op inválido (0), limpiando buffer y reintentando...");
            if(limpiar_buffer_completo(fd_cpu)) {
                log_info(memoria_logger, "Buffer limpiado, reintentando recepción...");
            }
            usleep(100000); // 100ms antes de reintentar
            continue;
        }

        log_info(memoria_logger, "Procesando operación %d de CPU (iteración %d)", cod_op, contador);

        switch(cod_op) {
            case SOLICITUD_INSTRUCCION:
                log_info(memoria_logger, "Procesando SOLICITUD_INSTRUCCION de CPU");
                continuar = procesar_solicitud_instruccion(fd_cpu);
                if(!continuar) {
                    log_info(memoria_logger, "Fin de instrucciones alcanzado");
                }
                break;

            case LECTURA_MEMORIA:
                log_info(memoria_logger, "Procesando    LECTURA_MEMORIA de CPU");
                procesar_lectura(fd_cpu);
                break;

            case ESCRITURA_MEMORIA:
                log_info(memoria_logger, "Procesando ESCRITURA_MEMORIA de CPU");
                procesar_escritura(fd_cpu);
                break;
                
            default:
                log_warning(memoria_logger, "Operación no reconocida de CPU: %d", cod_op);
                // Limpiar buffer más agresivamente
                if (limpiar_buffer_completo(fd_cpu)) {
                    log_info(memoria_logger, "Buffer limpiado después de operación desconocida");
                }
                break;
        }

        usleep(100000); // 100ms
        contador++;
    }

    log_info(memoria_logger, "Cerrando conexión con CPU");
    close(fd_cpu);
    return NULL;
}

void mostrar_contenido_archivo(FILE* archivo, t_log* memoria_logger) {
    char linea[512]; // Buffer para leer líneas

    log_info(memoria_logger, "Contenido del archivo:");

    rewind(archivo); // Por si ya se leyó algo

    while (fgets(linea, sizeof(linea), archivo) != NULL) {
        // Elimina salto de línea si está presente
        linea[strcspn(linea, "\n")] = 0;
        log_info(memoria_logger, "Linea: %s", linea);
    }

    rewind(archivo); // Volver al inicio para uso posterior
}

char* entregar_linea(FILE* archivo, t_log* memoria_logger, int buffer_size) {
        
    if (archivo == NULL || memoria_logger == NULL || buffer_size <= 0) {
        log_error(memoria_logger, "Parámetros inválidos en entregar_linea");
        return NULL;
    }
    
    char* linea = malloc(buffer_size);
    if (linea == NULL) {
        log_error(memoria_logger, "Error: no se pudo asignar memoria para la línea");
        return NULL;
    }

    // Verificar estado del archivo
    if (feof(archivo)) {
        free(linea);
        return NULL;
    }

    log_info(memoria_logger, "Leyendo línea del archivo...");

    // Leer la siguiente línea
    if (fgets(linea, buffer_size, archivo) != NULL) {
        // Eliminar salto de línea si está presente
        linea[strcspn(linea, "\n")] = '\0';

        char* inicio = linea;
        while (*inicio == ' ' || *inicio == '\t') inicio++;
        
        if (*inicio != '\0') {
            // Mover el contenido al inicio del buffer si es necesario
            if (inicio != linea) {
                memmove(linea, inicio, strlen(inicio) + 1);
            }
            
            int len = strlen(linea);
            while (len > 0 && (linea[len-1] == ' ' || linea[len-1] == '\t')) {
                linea[--len] = '\0';
            }
            
            log_info(memoria_logger, "Línea leída: '%s' (longitud: %d)", linea, len);
            return linea;
        }
    }
     
    free(linea);
    
    // End of file o error
    if (feof(archivo)) {
        log_info(memoria_logger, "Se alcanzó el fin del archivo");
    } else {
        log_error(memoria_logger, "Error al leer el archivo: %s", strerror(errno));
    }
    return NULL;
}

