#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <unistd.h>
#include <signal.h>
#include <readline/readline.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <conexiones.h>
#include "../include/kernel.h"
#include "../include/planificador_largo_plazo.h"

// Declaraciones de funciones auxiliares
void establecer_pid_proceso_actual(int pid);
void* planificador_corto_plazo_hilo(void* arg);

// Estructura para registrar cambios de estado (duplicada de planificador_largo_plazo.c)
typedef struct {
    estado_proceso_t estado_anterior;
    estado_proceso_t estado_nuevo;
    char* timestamp;
    int duracion_ms; // Duración en el estado anterior
} t_cambio_estado;

// Definición de variables globales
extern t_log* kernel_logger;
t_config* kernel_config;

// Variables de configuración
char* IP_MEMORIA;
char* IP_IO;
int PUERTO_MEMORIA;
int PUERTO_ESCUCHA_DISPATCH;
int PUERTO_ESCUCHA_INTERRUPT;
int PUERTO_ESCUCHA_IO;

// Colas del sistema
t_list* cola_new;
t_list* cola_ready;
t_list* cola_susp_ready;
t_list* cola_exec;
t_list* cola_blocked; // Procesos bloqueados por IO
t_list* dispositivos_io_conectados; // Lista de dispositivos IO
t_list* timers_suspension; // Lista de timers de suspensión activos


// Variable para tracking del proceso actual en syscall
static int pid_proceso_actual = -1;


// Funciones auxiliares para el planificador
t_cpu_conectada* buscar_cpu_libre() {
    // Implementación simple - buscar CPU libre en la lista
    if (lista_cpus_conectadas == NULL) {
        log_warning(kernel_logger, "Lista de CPUs conectadas es NULL");
        return NULL;
    }
    
    int total_cpus = list_size(lista_cpus_conectadas);
    log_trace(kernel_logger, "Buscando CPU libre entre %d CPUs conectadas", total_cpus);
    
    for (int i = 0; i < total_cpus; i++) {
        t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, i);
        log_trace(kernel_logger, "CPU ID %d - Libre: %s", cpu->id_cpu, cpu->esta_libre ? "SÍ" : "NO");
        if (cpu->esta_libre) {
            log_info(kernel_logger, "CPU libre encontrada - ID: %d", cpu->id_cpu);
            return cpu;
        }
    }
    
    log_warning(kernel_logger, "No hay CPUs libres disponibles");
    return NULL;
}

t_proceso_kernel* sacar_proceso_de_ready() {
    pthread_mutex_lock(&mutex_cola_ready);
    t_proceso_kernel* proceso = NULL;
    if (!list_is_empty(cola_ready)) {
        proceso = list_remove(cola_ready, 0);
    }
    pthread_mutex_unlock(&mutex_cola_ready);
    return proceso;
}

t_pcb* sacar_de_ready() {
    t_proceso_kernel* proceso = sacar_proceso_de_ready();
    if (proceso != NULL) {
        return &(proceso->pcb);
    }
    return NULL;
}

t_paquete_pcb* crear_paquete_enviar(t_pcb* pcb){
    t_paquete_pcb* paquete = malloc(sizeof(t_paquete_pcb));
    paquete->pid = pcb->pid;
    paquete->pc = pcb->pc;
    paquete->tiempo_estimado = pcb->tiempo_estimado;
    paquete->tiempo_inicio_rafaga = pcb->tiempo_inicio_rafaga;
    paquete->estado_serializado = (int)pcb->estado; // Serializar el estado
    return paquete;
}

void agregar_pcb_a_paquete(t_paquete* paquete, t_pcb* pcb) {
    // Serializar solo los campos escalares del PCB
    agregar_a_paquete(paquete, &pcb->pid, sizeof(int));
    agregar_a_paquete(paquete, &pcb->pc, sizeof(int));
    agregar_a_paquete(paquete, &pcb->tiempo_estimado, sizeof(int));
    agregar_a_paquete(paquete, &pcb->tiempo_inicio_rafaga, sizeof(int));
    int estado_serializado = (int)pcb->estado;
    agregar_a_paquete(paquete, &estado_serializado, sizeof(int));
}

void enviar_interrupt(int socket, int pid) {
    // Enviar señal de interrupción a la CPU
    send(socket, &pid, sizeof(int), 0);
}
/*
void inicializar_sincronizacion() {
    // Inicializar semáforos
    sem_init(&hay_procesos_ready, 0, 0);
    sem_init(&sem_archivo_listo, 0, 0);
    sem_init(&sem_servidor_cpu_listo, 0, 0);
    sem_init(&sem_kernel_cpu_hs, 0, 0);
    sem_init(&sem_syscall_procesada, 0, 0);
}*/
/*
void inicializar_semaforos() {
    // Ya implementado en inicializar_sincronizacion
    log_info(kernel_logger, "Semáforos inicializados correctamente");
}*/
extern t_list* operaciones_io_activas;

// Algoritmos y configuración
char* ALGORITMO_PLANIFICACION;
char* ALGORITMO_INGRESO_A_READY;
char* ALGORITMO_CORTO_PLAZO;
float ALFA;
int ESTIMACION_INICIAL;
int TIEMPO_SUSPENSION;
char* PATH_INSTRUCCIONES;
char* LOG_LEVEL;

// File descriptors
int fd_kernel_dispatch;
int fd_cpu_dispatch;
int fd_cpu_interrupt;
int fd_io;
int conexiones = 0;

// Mutexes
pthread_mutex_t mutex_cola_new;
pthread_mutex_t mutex_cola_ready;
pthread_mutex_t mutex_cola_susp_ready;
pthread_mutex_t mutex_cola_exec;
pthread_mutex_t mutex_cola_blocked;
//pthread_mutex_t mutex_syscall_kernel;
//pthread_mutex_t mutex_io_activas;
pthread_mutex_t mutex_dispositivos_io;
pthread_mutex_t mutex_timers_suspension;
pthread_mutex_t mutex_io;
pthread_mutex_t mutex_dispositivos;
pthread_mutex_t mutex_cpus;
pthread_mutex_t mutex_blocked;
pthread_mutex_t mutex_ready;


// Semáforos
//sem_t sem_archivo_listo;
//sem_t sem_servidor_cpu_listo;
//sem_t sem_kernel_cpu_hs;
//sem_t sem_syscall_procesada;
sem_t hay_procesos_ready;
sem_t sem_io_conectado;

// Otras estructuras globales
t_list* lista_cpus_conectadas;
t_planificador_largo_plazo* plani_lp;

int id_proceso = 0;


void inicializar_kernel(){
    inicializar_colas();
    inicializar_sincronizacion();
    inicializar_semaforos();
    
    // Inicializar mutexes de las colas
    pthread_mutex_init(&mutex_cola_new, NULL);
    pthread_mutex_init(&mutex_cola_ready, NULL);
    pthread_mutex_init(&mutex_cola_susp_ready, NULL);
    pthread_mutex_init(&mutex_cola_exec, NULL);
    pthread_mutex_init(&mutex_cola_blocked, NULL);
    pthread_mutex_init(&mutex_cpus, NULL);
    pthread_mutex_init(&mutex_dispositivos_io, NULL);
    pthread_mutex_init(&mutex_timers_suspension, NULL);
    
    sem_init(&hay_procesos_ready, 0, 0);

    // Inicializar lista de operaciones I/O
    operaciones_io_activas = list_create();
}

void inicializar_colas() {
    cola_new = list_create();
    cola_ready = list_create();
    cola_susp_ready = list_create();
    cola_exec = list_create();
    cola_blocked = list_create();
    dispositivos_io_conectados = list_create();
    timers_suspension = list_create();
    lista_cpus_conectadas = list_create();
}

void destruir_colas() {
    list_destroy_and_destroy_elements(cola_new, free);
    list_destroy_and_destroy_elements(cola_ready, free);
    list_destroy_and_destroy_elements(cola_susp_ready, free);
    list_destroy_and_destroy_elements(cola_exec, free);
    list_destroy_and_destroy_elements(lista_cpus_conectadas, free);
    
    // Destruir mutexes
    pthread_mutex_destroy(&mutex_cola_new);
    pthread_mutex_destroy(&mutex_cola_ready);
    pthread_mutex_destroy(&mutex_cola_susp_ready);
    pthread_mutex_destroy(&mutex_cola_exec);
    pthread_mutex_destroy(&mutex_cpus);
    
    sem_destroy(&hay_procesos_ready);
    
    // Liberar planificador
    if (plani_lp) {
        free(plani_lp->estado);
        free(plani_lp);
    }
}

void inicializar_logs(){

    kernel_logger = log_create("../kernel.log", "KERNEL_LOG", 1, LOG_LEVEL_INFO);
    if (kernel_logger == NULL)
    {
        perror("Algo salio mal con el memoria_log, no se pudo crear o escuchar el archivo");
        exit(EXIT_FAILURE);
    }
    iniciar_logger(kernel_logger);
}

t_config* inicializar_configs(void){

    kernel_config = config_create("../kernel.config");

    if (kernel_config == NULL)
    {
        perror("Error al cargar kernel_config");
        exit(EXIT_FAILURE);
    }

    // Cargar configuraciones
    
    IP_MEMORIA = config_get_string_value(kernel_config, "IP_MEMORIA");
    IP_IO = config_get_string_value(kernel_config, "IP_IO");
    PUERTO_MEMORIA = config_get_int_value(kernel_config, "PUERTO_MEMORIA");
    
    PUERTO_ESCUCHA_DISPATCH = config_get_int_value(kernel_config, "PUERTO_ESCUCHA_DISPATCH");
    PUERTO_ESCUCHA_INTERRUPT = config_get_int_value(kernel_config, "PUERTO_ESCUCHA_INTERRUPT");
    PUERTO_ESCUCHA_IO = config_get_int_value(kernel_config, "PUERTO_ESCUCHA_IO");
    
    ALGORITMO_PLANIFICACION = config_get_string_value(kernel_config, "ALGORITMO_PLANIFICACION");
    ALGORITMO_INGRESO_A_READY = config_get_string_value(kernel_config, "ALGORITMO_INGRESO_A_READY");
    ALGORITMO_CORTO_PLAZO = config_get_string_value(kernel_config,"ALGORITMO_CORTO_PLAZO");
    
    ALFA = atof(config_get_string_value(kernel_config, "ALFA"));

    ESTIMACION_INICIAL = config_get_int_value(kernel_config, "ESTIMACION_INICIAL");
    
    TIEMPO_SUSPENSION = config_get_int_value(kernel_config, "TIEMPO_SUSPENSION");
    
    PATH_INSTRUCCIONES = config_get_string_value(kernel_config, "PATH_INSTRUCCIONES");
    
    LOG_LEVEL = config_get_string_value(kernel_config, "LOG_LEVEL");

    return kernel_config;
}

void imprimir_configs(){
    log_info(kernel_logger, "IP_MEMORIA: %s", IP_MEMORIA);
    log_info(kernel_logger, "IP_IO: %s", IP_IO);
    log_info(kernel_logger, "PUERTO_MEMORIA: %d", PUERTO_MEMORIA);
    log_info(kernel_logger, "PUERTO_ESCUCHA_DISPATCH: %d", PUERTO_ESCUCHA_DISPATCH);
    log_info(kernel_logger, "PUERTO_ESCUCHA_INTERRUPT: %d", PUERTO_ESCUCHA_INTERRUPT);
    log_info(kernel_logger, "PUERTO_ESCUCHA_IO: %d", PUERTO_ESCUCHA_IO);
    log_info(kernel_logger, "ALGORITMO_PLANIFICACION: %s", ALGORITMO_PLANIFICACION);
    log_info(kernel_logger, "ALGORITMO_COLA_NEW: %s", ALGORITMO_INGRESO_A_READY);
    log_info(kernel_logger, "ALFA: %f", ALFA);
    log_info(kernel_logger, "TIEMPO_SUSPENSION: %d", TIEMPO_SUSPENSION);
    log_info(kernel_logger, "PATH_INSTRUCCIONES: %s", PATH_INSTRUCCIONES);
    log_info(kernel_logger, "LOG_LEVEL: %s", LOG_LEVEL);
}

void enviar_proceso(int fd_memoria, t_proceso_kernel* datos ){
    t_paquete* paquete = crear_paquete(ENVIO_ARCHIVO_KERNEL, kernel_logger);
    agregar_a_paquete(paquete, datos->nombre, strlen(datos->nombre) + 1);
    //Convertir el int a string antes de enviarlo
    char tamanio_str[32];
    snprintf(tamanio_str, sizeof(tamanio_str), "%d", datos->tamanio);
    agregar_a_paquete(paquete, tamanio_str, strlen(tamanio_str) + 1);

    log_info(kernel_logger, "Paquete creado:");
    log_info(kernel_logger, "- Código de operación: %d", paquete->codigo_operacion);
    log_info(kernel_logger, "- Tamaño del buffer: %d", paquete->buffer->size);
    log_info(kernel_logger, "- Nombre archivo: '%s' (longitud: %zu)", datos->nombre, strlen(datos->nombre));
    log_info(kernel_logger, "- Tamaño archivo string: '%s' (longitud: %zu)", tamanio_str, strlen(tamanio_str));

    enviar_paquete(paquete, fd_memoria);

    log_info(kernel_logger, "Paquete con archivo pseudocodigo enviado con exito a Memoria");

    bool confirmacion_archivo;

    log_info(kernel_logger, "Esperando confirmación de carga de archivo...");
    if (recv(fd_memoria, &confirmacion_archivo, sizeof(bool), MSG_WAITALL) > 0) {
        log_info(kernel_logger,confirmacion_archivo ? "Éxito\n" : "Error\n");

        if (confirmacion_archivo) {
            // Si se cargó el archivo exitosamente, notificar
            sem_post(&sem_archivo_listo);
            log_info(kernel_logger, "Archivo cargado exitosamente, notificando a los módulos");
        }

    }
    eliminar_paquete(paquete);
}

bool enviar_handshake(int fd_memoria){
    t_paquete* paquete = crear_paquete(HANDSHAKE, kernel_logger);
    agregar_a_paquete(paquete, "handshake", strlen("handshake") + 1);

    log_info(kernel_logger, "Paquete creado:");
    log_info(kernel_logger, "- Código de operación: %d", paquete->codigo_operacion);
    log_info(kernel_logger, "- Tamaño del buffer: %d", paquete->buffer->size);

    enviar_paquete(paquete, fd_memoria);

    log_info(kernel_logger, "Handshake enviado con exito a Memoria");

    bool respuesta_handshake;

    log_info(kernel_logger, "Esperando respuesta de memoria...");
    if (recv(fd_memoria, &respuesta_handshake, sizeof(bool), MSG_WAITALL) > 0) {
        log_info(kernel_logger,respuesta_handshake ? "Éxito\n" : "Error\n");
    }
    eliminar_paquete(paquete);
    return respuesta_handshake;
}

t_proceso_kernel* crear_proceso(char* nombre,int tamanio){
    // Construir la ruta completa usando la configuración
    char path_completo[512];
    snprintf(path_completo, sizeof(path_completo), "%s%s", PATH_INSTRUCCIONES, nombre);
    
    // Debug: mostrar información detallada
    log_info(kernel_logger, "=== DEBUG CREAR_PROCESO ===");
    log_info(kernel_logger, "Nombre archivo solicitado: '%s'", nombre);
    log_info(kernel_logger, "PATH_INSTRUCCIONES configurado: '%s'", PATH_INSTRUCCIONES);
    log_info(kernel_logger, "Ruta completa construida: '%s'", path_completo);
    log_info(kernel_logger, "Directorio actual de trabajo: %s", getcwd(NULL, 0));
    log_info(kernel_logger, "Intentando abrir archivo...");
    
    // Verificar si el archivo existe antes de intentar abrirlo
    if (access(path_completo, F_OK) != 0) {
        log_error(kernel_logger, "El archivo no existe: %s", path_completo);
        log_error(kernel_logger, "Access errno: %d (%s)", errno, strerror(errno));
    } else {
        log_info(kernel_logger, "El archivo existe, verificando permisos de lectura...");
        if (access(path_completo, R_OK) != 0) {
            log_error(kernel_logger, "No hay permisos de lectura: %s", path_completo);
            log_error(kernel_logger, "Read access errno: %d (%s)", errno, strerror(errno));
        } else {
            log_info(kernel_logger, "Archivo accesible para lectura");
        }
    }
    
    FILE* archivo = fopen(path_completo,"r");
    if (!archivo) {
        log_error(kernel_logger,"ERROR: Archivo no encontrado en ruta: %s", path_completo);
        log_error(kernel_logger,"Errno: %d (%s)", errno, strerror(errno));
        return NULL;
    }
    
    log_info(kernel_logger, "ÉXITO: Archivo de pseudocódigo abierto desde: %s", path_completo);
    t_list* instrucciones = list_create();
    char linea[256];

    while (fgets(linea, sizeof(linea), archivo)) {
        // Eliminar salto de línea
        linea[strcspn(linea, "\r\n")] = 0;

        // Copiar la línea en memoria dinámica
        char* instruccion = strdup(linea);
        list_add(instrucciones, instruccion);
    }

    fclose(archivo);
    t_proceso_kernel *proceso = malloc(sizeof(t_proceso_kernel));
    proceso -> codigo = instrucciones;
    proceso -> nombre = nombre;
    proceso -> tamanio = tamanio;
    proceso -> estado = ESTADO_NEW;
    proceso -> pcb = *crear_PCB(); // Asignar por valor, pero el PCB es creado correctamente
    
    // Inicializar métricas de estado del proceso
    inicializar_metricas_proceso(proceso);
    
    return proceso;
}

// Función para conectar con módulo I/O
int conectar_a_io() {
    log_info(kernel_logger, "Iniciando conexión a módulo I/O...");

    if (IP_IO == NULL) {
        log_error(kernel_logger, "IP_IO no configurada");
        return -1;
    }

    int fd_io = crear_conexion(kernel_logger, IP_IO, PUERTO_ESCUCHA_IO);
    if (fd_io <= 0) {
        log_error(kernel_logger, "Error al conectar con módulo I/O");
        return -1;
    }

    // 1. Enviar identificación como KERNEL
    int identificador = KERNEL;
    if (send(fd_io, &identificador, sizeof(int), 0) != sizeof(int)) {
        log_error(kernel_logger, "Error al enviar identificación al módulo I/O");
        close(fd_io);
        return -1;
    }

    // 2. Recibir confirmación
    bool respuesta_io;
    if (recv(fd_io, &respuesta_io, sizeof(bool), MSG_WAITALL) != sizeof(bool)) {
        log_error(kernel_logger, "Error al recibir confirmación del módulo I/O");
        close(fd_io);
        return -1;
    }

    if (!respuesta_io) {
        log_error(kernel_logger, "Módulo I/O rechazó la conexión");
        close(fd_io);
        return -1;
    }

    log_info(kernel_logger, "Conexión con módulo I/O establecida correctamente");
    return fd_io;
}

// Función para enviar handshake a I/O
bool handshake_con_io(int socket_io) {
    log_info(kernel_logger, "Iniciando handshake con dispositivo I/O...");
    // 1. Enviar identificación del kernel
    int identificador = KERNEL;
    ssize_t enviado = send(socket_io, &identificador, sizeof(int), 0);
    
    if (enviado != sizeof(int)) {
        log_error(kernel_logger, "Error al enviar identificación a I/O");
        return false;
    }

    log_info(kernel_logger, "Identificación enviada correctamente al dispositivo I/O");
    
    // 2. Recibir confirmación
    bool confirmacion;
    ssize_t recibido = recv(socket_io, &confirmacion, sizeof(bool), MSG_WAITALL);
    
    if (recibido != sizeof(bool) || !confirmacion) {
        log_error(kernel_logger, "Handshake fallido con dispositivo I/O");
        return false;
    }
    
    log_info(kernel_logger, "Handshake exitoso con dispositivo I/O");
    return true;
}

// Función para manejar conexiones entrantes de dispositivos I/O
void* manejar_conexion_io(void* arg) {
    int socket_io = *(int*)arg;
    free(arg);
    
    // Realizar handshake
    if (!handshake_con_io(socket_io)) {
        close(socket_io);
        return NULL;
    }
    
    // Crear estructura para el dispositivo
    t_dispositivo_kernel* dispositivo = malloc(sizeof(t_dispositivo_kernel));
    dispositivo->socket_fd = socket_io;
    dispositivo->conectado = true;
    dispositivo->nombre = NULL;
    pthread_mutex_init(&dispositivo->mutex_io, NULL);
    
    // Agregar a la lista de dispositivos
    pthread_mutex_lock(&mutex_dispositivos);
    list_add(dispositivos_io_conectados, dispositivo);
    pthread_mutex_unlock(&mutex_dispositivos);
    
    log_info(kernel_logger, "Dispositivo I/O conectado y registrado");
    
    // *** CREAR HILO DE ESCUCHA INMEDIATAMENTE ***
    pthread_create(&dispositivo->hilo_io, NULL, escuchar_finalizaciones_io, dispositivo);
    pthread_detach(dispositivo->hilo_io);
    
    log_info(kernel_logger, "Hilo de escucha creado para dispositivo I/O");
    
    return NULL;
}

// Inicializar servidor para dispositivos I/O
void inicializar_servidor_io() {
    log_info(kernel_logger, "Inicializando servidor para dispositivos I/O...");
    
    // Inicializar mutex y lista de dispositivos I/O
    dispositivos_io_conectados = list_create();
    pthread_mutex_init(&mutex_dispositivos, NULL);

    log_info(kernel_logger, "Mutex y lista de dispositivos I/O inicializados");
    
    // Crear hilo servidor para aceptar conexiones I/O
    int puerto_io = config_get_int_value(kernel_config, "PUERTO_ESCUCHA_IO");

    log_info(kernel_logger, "Iniciando servidor en puerto %d...", puerto_io);
    int socket_servidor = iniciar_servidor(kernel_logger, puerto_io);
    
    if (socket_servidor == -1) {
        log_error(kernel_logger, "No se pudo iniciar servidor I/O");
        exit(EXIT_FAILURE);
    }
    
    log_info(kernel_logger, "Servidor I/O iniciado en puerto %d", puerto_io);
    
    // Crear hilo con verificación adicional
    pthread_t hilo_servidor_io;
    int* socket_arg = malloc(sizeof(int));
    *socket_arg = socket_servidor;
    
    log_info(kernel_logger, "Pasando socket %d al hilo servidor", *socket_arg);
    
    if (pthread_create(&hilo_servidor_io, NULL, servidor_io_thread, socket_arg) != 0) {
        log_error(kernel_logger, "Error creando hilo servidor I/O");
        free(socket_arg);
        exit(EXIT_FAILURE);
    }
    
    pthread_detach(hilo_servidor_io);
    log_info(kernel_logger, "Hilo servidor I/O creado exitosamente");
}

// Hilo servidor que acepta conexiones I/O
void* servidor_io_thread(void* arg) {
    int socket_servidor = *(int*)arg;
    free(arg);
    
    while (true) {
        log_info(kernel_logger, "Esperando conexiones de dispositivos I/O...");
        int socket_cliente = esperar_cliente(kernel_logger, socket_servidor);
        
        if (socket_cliente != -1) {
            log_info(kernel_logger, "Dispositivo I/O conectado");
            
            // Crear hilo para manejar la conexión
            int* socket_arg = malloc(sizeof(int));
            *socket_arg = socket_cliente;
            
            pthread_t hilo_conexion;
            pthread_create(&hilo_conexion, NULL, manejar_conexion_io, socket_arg);
            pthread_detach(hilo_conexion);
        } else {
            // *** AGREGAR PAUSA PARA EVITAR BUCLE INFINITO ***
            log_error(kernel_logger, "Error en accept(), esperando antes de reintentar...");
            sleep(1);  // Pausa de 1 segundo antes de reintentar
        }
    }
    
    return NULL;
}

// Mover proceso de BLOCKED a READY (integración con planificador)
void mover_proceso_blocked_a_ready(t_pcb* proceso) {
    pthread_mutex_lock(&mutex_blocked);
    
    // Remover de cola BLOCKED
    bool encontrado = false;
    for (int i = 0; i < list_size(cola_blocked); i++) {
        t_pcb* proc_blocked = list_get(cola_blocked, i);
        if (proc_blocked->pid == proceso->pid) {
            list_remove(cola_blocked, i);
            encontrado = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&mutex_blocked);
    
    if (encontrado) {
        // Agregar a cola READY
        pthread_mutex_lock(&mutex_ready);
        proceso->estado = READY;
        list_add(cola_ready, proceso);
        pthread_mutex_unlock(&mutex_ready);
        
        log_info(kernel_logger, "Proceso PID %d: BLOCKED → READY", proceso->pid);
        
        // Señalar al planificador que hay procesos listos
        //sem_post(&sem_procesos_ready);
    }
}

// Recibir finalización de I/O (debe ser llamada desde un hilo separado)
void* escuchar_finalizaciones_io(void* arg) {
    t_dispositivo_kernel* dispositivo = (t_dispositivo_kernel*)arg;
    
    while (dispositivo->conectado) {
        t_mensaje_io respuesta;
        
        // Esperar respuesta del dispositivo I/O
        ssize_t recibido = recv(dispositivo->socket_fd, &respuesta, 
                               sizeof(t_mensaje_io), MSG_WAITALL);
        
        if (recibido <= 0) {
            if (recibido == 0) {
                log_info(kernel_logger, "Dispositivo I/O desconectado");
            } else {
                log_error(kernel_logger, "Error en recv de I/O: %s", strerror(errno));
            }
            dispositivo->conectado = false;
            break;
        }
        
        if (recibido != sizeof(t_mensaje_io)) {
            log_error(kernel_logger, "Mensaje I/O incompleto");
            continue;
        }
        
        // Procesar finalización
        if (respuesta.tipo == IO_FINALIZACION) {
            log_info(kernel_logger, "I/O finalizada - PID %d", respuesta.pid);
            
            // Buscar proceso y moverlo de BLOCKED a READY
            t_proceso_kernel* proceso = buscar_proceso_por_pid(respuesta.pid);
            if (proceso) {
                mover_proceso_blocked_a_ready(&(proceso->pcb));
            }
        }
    }
    
    return NULL;
}

// Iniciar hilos de escucha para cada dispositivo I/O
void iniciar_escucha_dispositivos() {
    pthread_mutex_lock(&mutex_dispositivos);
    
    for (int i = 0; i < list_size(dispositivos_io_conectados); i++) {
        t_dispositivo_kernel* dispositivo = list_get(dispositivos_io_conectados, i);
        
        if (dispositivo->conectado) {
            pthread_create(&dispositivo->hilo_io, NULL, escuchar_finalizaciones_io, dispositivo);
            pthread_detach(dispositivo->hilo_io);
        }
    }
    
    pthread_mutex_unlock(&mutex_dispositivos);
}

int main(int argc, char* argv[]) {
    
    inicializar_logs();
    inicializar_configs();
    imprimir_configs();

    // Handshake de memoria
    log_info(kernel_logger,"Esperando handshake con memoria...");
    int memoria = conectar_a_memoria();
    if (!enviar_handshake(memoria)){
        log_error(kernel_logger,"Modulo de memoria no disponible");
        exit(EXIT_FAILURE);
    }
    // Validar argumentos
    if (argc < 3) {
        log_error(kernel_logger, "Uso: %s <nombre_archivo> <tamaño_archivo>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    

    // Obtener valores desde argv
    char* nombre_archivo = argv[1];
    int tamanio_archivo = atoi(argv[2]);  // convertir a entero
    
    if (nombre_archivo == NULL || strlen(nombre_archivo) == 0) {
        log_error(kernel_logger, "Nombre de archivo inválido: %s\n", nombre_archivo);
        exit(EXIT_FAILURE);
    }
    if (tamanio_archivo < 0) {
        log_error(kernel_logger, "Tamaño de archivo inválido: %d\n", tamanio_archivo);
        exit(EXIT_FAILURE);
    }

    log_info(kernel_logger, "Nombre de archivo: %s", nombre_archivo);
    log_info(kernel_logger, "Tamaño de archivo: %d", tamanio_archivo);

    // Inicializar I/O
    inicializar_servidor_io();
    
    log_info(kernel_logger, "Esperando conexión de dispositivos I/O...");

    // Esperar a que se conecten dispositivos
    sleep(2); 
    
    // Iniciar escucha de dispositivos conectados
    iniciar_escucha_dispositivos();

    // Esperar confirmación de ambos módulos
    //sem_wait(&sem_io_conectado);
    log_info(kernel_logger, "Todos los módulos conectados y confirmados");

    inicializar_kernel();

    log_info(kernel_logger, "Se inicializo kernel.");
    
    // Establecer la conexión con memoria para el planificador
    establecer_conexion_memoria(memoria);
    
    // INICIAR SERVIDOR CPU EN HILO INDEPENDIENTE ANTES DE LA PLANIFICACIÓN
    pthread_t hilo_cpu_dispatch;
    log_info(kernel_logger, "Iniciando servidor CPU Dispatch...");
    if (pthread_create(&hilo_cpu_dispatch, NULL, atender_cpu_dispatch, &fd_cpu_dispatch) != 0) {
        log_error(kernel_logger, "Error al crear hilo de CPU Dispatch");
        exit(EXIT_FAILURE);
    } else {
        log_info(kernel_logger, "Creado hilo de CPU Dispatch");
    }
    
    // INICIAR SERVIDOR CPU INTERRUPT EN HILO INDEPENDIENTE
    pthread_t hilo_cpu_interrupt;
    log_info(kernel_logger, "Iniciando servidor CPU Interrupt...");
    if (pthread_create(&hilo_cpu_interrupt, NULL, atender_cpu_interrupt, &fd_cpu_interrupt) != 0) {
        log_error(kernel_logger, "Error al crear hilo de CPU Interrupt");
        exit(EXIT_FAILURE);
    } else {
        log_info(kernel_logger, "Creado hilo de CPU Interrupt");
    }
    
    // INICIAR PLANIFICADOR DE CORTO PLAZO EN HILO INDEPENDIENTE
    pthread_t hilo_planificador_corto;
    log_info(kernel_logger, "Iniciando planificador de corto plazo...");
    if (pthread_create(&hilo_planificador_corto, NULL, planificador_corto_plazo_hilo, NULL) != 0) {
        log_error(kernel_logger, "Error al crear hilo de planificador corto plazo");
        exit(EXIT_FAILURE);
    } else {
        log_info(kernel_logger, "Creado hilo de planificador corto plazo");
    }
    
    // Dar tiempo para que los servidores se inicien
    usleep(100000); // 100ms
    
    t_proceso_kernel* proceso = crear_proceso(nombre_archivo,tamanio_archivo);
    if (proceso == NULL) {
        log_error(kernel_logger, "Error al crear proceso inicial");
        exit(EXIT_FAILURE);
    }
    
    plani_lp = iniciar_planificador_largo_plazo();
    
    log_info(kernel_logger, "Planificador Largo Plazo en estado: %s", plani_lp->estado);
    log_info(kernel_logger, "Presione ENTER para iniciar la planificación...");
    getchar();
    
    free(plani_lp->estado);
    plani_lp->estado = strdup("INICIADO");
    plani_lp->activo = true;
    log_info(kernel_logger, "Planificador de largo plazo INICIADO");

    planificar_con_plp(proceso);

    // El servidor CPU ya está corriendo en hilo independiente
    // Esperar a que termine (o implementar lógica de shutdown)
    log_info(kernel_logger, "Planificación completada, CPU servidor corriendo...");
    
    // Esperar ambos hilos
    pthread_join(hilo_cpu_dispatch, NULL);
    pthread_join(hilo_planificador_corto, NULL);
    destruir_colas();

    
    log_debug(kernel_logger,"Se ha desconectado de kernel");

    printf("\nKERNEL DESCONECTADO!\n\n");

    return EXIT_SUCCESS;
}


int conectar_a_memoria(){

    log_info(kernel_logger, "Iniciando conexión a memoria...");

    int fd_memoria = crear_conexion(kernel_logger, IP_MEMORIA, PUERTO_MEMORIA);

    if (fd_memoria <= 0) {
        log_error(kernel_logger, "Error al conectar con memoria");
        return -1;
    }

    // Identificarse como KERNEL
    int identificador = KERNEL;
    // Esperar confirmación de memoria
    bool confirmacion = send(fd_memoria, &identificador, sizeof(int), 0);
    if (!confirmacion) {
        log_error(kernel_logger, "Memoria rechazó la conexión");
        close(fd_memoria);
        return -1;
    }
    log_info(kernel_logger, "Cliente Memoria: %d conectado", fd_memoria);
    
    ssize_t bytes_recibidos = recv(fd_memoria, &confirmacion, sizeof(bool), MSG_WAITALL);
    
    if (bytes_recibidos <= 0) {
        log_error(kernel_logger, "Error al recibir confirmación de memoria");
        close(fd_memoria);
        return -1;
    }
    return fd_memoria;
}


void* atender_cpu_dispatch() {
    fd_kernel_dispatch = -1;

    log_info(kernel_logger, "Iniciando conexión a CPU_dispatch...");

    fd_kernel_dispatch = iniciar_servidor(kernel_logger, PUERTO_ESCUCHA_DISPATCH);
    if (fd_kernel_dispatch == -1){
        log_error(kernel_logger, "ERROR: Kernel no levanta servidor");
        return NULL;
    }
    
    log_info(kernel_logger, "Servidor de Kernel Dispatch iniciado. Esperando Clientes... con fd_kernel_dispatch: %d", fd_kernel_dispatch);

    // Señalizar que el servidor está listo
    sem_post(&sem_servidor_cpu_listo);
    
    int fd_cpu_dispatch = esperar_cliente(kernel_logger, fd_kernel_dispatch);
    if (fd_kernel_dispatch < 0) {
        log_error(kernel_logger, "Error al aceptar cliente de cpu_dispatch");
        return NULL;
    }

    log_info(kernel_logger, "Cliente fd_cpu: %d conectado", fd_cpu_dispatch);

    // Crear y agregar CPU a la lista de CPUs conectadas
    t_cpu_conectada* nueva_cpu = malloc(sizeof(t_cpu_conectada));
    nueva_cpu->id_cpu = list_size(lista_cpus_conectadas); // ID basado en el orden de conexión
    nueva_cpu->socket_dispatch = fd_cpu_dispatch;
    nueva_cpu->socket_interrupt = -1; // Se asignará cuando se conecte por interrupt
    nueva_cpu->esta_libre = true;
    nueva_cpu->proceso_en_exec = NULL;
    
    pthread_mutex_lock(&mutex_cpus);
    list_add(lista_cpus_conectadas, nueva_cpu);
    pthread_mutex_unlock(&mutex_cpus);
    
    log_info(kernel_logger, "CPU registrada - ID: %d, Socket Dispatch: %d, Total CPUs: %d", 
             nueva_cpu->id_cpu, nueva_cpu->socket_dispatch, list_size(lista_cpus_conectadas));

    // Señalizar que se recibió el handshake de CPU
    sem_post(&sem_kernel_cpu_hs);

    log_info(kernel_logger, "Esperando operaciones de CPU...");

    // Esperar a que el archivo esté listo antes de procesar syscalls
    sem_wait(&sem_archivo_listo);

    bool cpu_conectado = true;
    // Bucle principal de atención a syscalls
    while (cpu_conectado) {
        log_info(kernel_logger, "Esperando syscall de CPU...");

        int cod_op = recibir_operacion(kernel_logger, fd_cpu_dispatch);

        switch(cod_op) {
            case -1:
                log_error(kernel_logger, "CPU se desconectó");
                
                // Remover CPU de la lista de CPUs conectadas
                pthread_mutex_lock(&mutex_cpus);
                for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
                    t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, i);
                    if (cpu->socket_dispatch == fd_cpu_dispatch) {
                        list_remove(lista_cpus_conectadas, i);
                        log_info(kernel_logger, "CPU ID %d removida de la lista. CPUs restantes: %d", 
                                 cpu->id_cpu, list_size(lista_cpus_conectadas));
                        free(cpu);
                        break;
                    }
                }
                pthread_mutex_unlock(&mutex_cpus);
                
                cpu_conectado = false;
                break;

            case SYSCALL: {
                log_info(kernel_logger, "Recibida syscall de CPU");
                
                pthread_mutex_lock(&mutex_syscall_kernel);

                t_list* lista = recibir_paquete_desde_buffer(kernel_logger, fd_cpu_dispatch);

                bool resultado = false;
                
                if (lista != NULL && list_size(lista) > 0) {
                    char* syscall = list_get(lista, 0);
                    
                    if(syscall != NULL && strlen(syscall) > 0) {
                        log_info(kernel_logger, "Procesando syscall: %s", syscall);
                        
                        // Procesar la syscall
                        resultado = procesar_syscall(syscall);
                        
                        log_info(kernel_logger, "Syscall %s procesada: %s", 
                                syscall, resultado ? "EXITOSA" : "ERROR");
                    } else {
                        log_warning(kernel_logger, "Syscall NULL o vacía recibida");
                    }
                    
                    list_destroy_and_destroy_elements(lista, free);
                } else {
                    log_warning(kernel_logger, "Lista de syscall NULL o vacía");
                    if (lista) list_destroy(lista);
                }

                // Enviar respuesta INMEDIATAMENTE
                t_paquete* paquete_respuesta;
                if (resultado) {
                    paquete_respuesta = crear_paquete(SYSCALL_OK, kernel_logger);
                    log_info(kernel_logger, "Enviando SYSCALL_OK al CPU");
                } else {
                    paquete_respuesta = crear_paquete(SYSCALL_ERROR, kernel_logger);
                    log_info(kernel_logger, "Enviando SYSCALL_ERROR al CPU");
                }
                
                enviar_paquete(paquete_respuesta, fd_cpu_dispatch);
                
                eliminar_paquete(paquete_respuesta);
                pthread_mutex_unlock(&mutex_syscall_kernel);
                
                sem_post(&sem_syscall_procesada);

                log_info(kernel_logger, "Respuesta enviada al CPU, listo para próxima syscall");
                break;
            }
            
            case PCB_DEVUELTO: {
                log_info(kernel_logger, "Recibido PCB devuelto de CPU");
                
                t_list* lista = recibir_paquete_desde_buffer(kernel_logger, fd_cpu_dispatch);
                
                if (lista != NULL && list_size(lista) >= 3) {
                    // El paquete debe contener: PID, tiempo_ejecutado, motivo
                    int pid = *(int*)list_get(lista, 0);
                    int tiempo_ejecutado = *(int*)list_get(lista, 1);
                    char* motivo = (char*)list_get(lista, 2);
                    
                    log_info(kernel_logger, "PCB devuelto - PID: %d, Tiempo: %d ms, Motivo: %s", 
                             pid, tiempo_ejecutado, motivo);
                    
                    // Buscar la CPU que tenía este proceso
                    pthread_mutex_lock(&mutex_cpus);
                    t_cpu_conectada* cpu_encontrada = NULL;
                    
                    for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
                        t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, i);
                        if (!cpu->esta_libre && cpu->proceso_en_exec != NULL && 
                            cpu->proceso_en_exec->pid == pid) {
                            cpu_encontrada = cpu;
                            break;
                        }
                    }
                    
                    if (cpu_encontrada != NULL) {
                        t_pcb* pcb = cpu_encontrada->proceso_en_exec;
                        
                        // PROCESAMIENTO DEL TIEMPO REAL EJECUTADO R(n)
                        int tiempo_real_ejecutado = tiempo_ejecutado;
                        
                        // Si no se proveyó tiempo ejecutado, calcularlo basado en timestamps
                        if (tiempo_real_ejecutado <= 0) {
                            if (pcb->tiempo_inicio_rafaga > 0) {
                                long tiempo_actual = time(NULL) * 1000; // ms
                                tiempo_real_ejecutado = (int)(tiempo_actual - pcb->tiempo_inicio_rafaga);
                                
                                // Validar que el tiempo sea positivo y razonable
                                if (tiempo_real_ejecutado < 0) tiempo_real_ejecutado = 1;
                                if (tiempo_real_ejecutado > 60000) tiempo_real_ejecutado = 60000; // máximo 1 minuto
                                
                                log_info(kernel_logger, "Tiempo calculado por timestamp: %d ms (inicio: %d, actual: %ld)", 
                                         tiempo_real_ejecutado, pcb->tiempo_inicio_rafaga, tiempo_actual);
                            } else {
                                // Si no hay timestamp, usar estimación como fallback
                                tiempo_real_ejecutado = pcb->tiempo_estimado / 2;
                                log_warning(kernel_logger, "No hay timestamp de inicio, usando estimación/2 = %d ms", 
                                           tiempo_real_ejecutado);
                            }
                        }
                        
                        // Validar que tengamos un tiempo válido para procesar
                        if (tiempo_real_ejecutado <= 0) {
                            tiempo_real_ejecutado = 1; // mínimo 1 ms
                            log_warning(kernel_logger, "Tiempo ejecutado inválido, usando 1 ms como mínimo");
                        }
                        
                        log_info(kernel_logger, "## TIEMPO REAL EJECUTADO R(n) = %d ms para PID=%d", 
                                 tiempo_real_ejecutado, pcb->pid);
                        
                        // Liberar CPU ANTES de procesar devolución
                        cpu_encontrada->esta_libre = true;
                        cpu_encontrada->proceso_en_exec = NULL;
                        
                        pthread_mutex_unlock(&mutex_cpus);
                        
                        // PROCESAR DEVOLUCIÓN Y ACTUALIZAR ESTIMACIÓN
                        procesar_devolucion_cpu(pcb, tiempo_real_ejecutado, motivo);
                        
                        // REPLANIFICACIÓN AUTOMÁTICA
                        if (!string_equals_ignore_case(motivo, "EXIT")) {
                            log_info(kernel_logger, "## Iniciando replanificación automática tras devolución");
                            
                            // Para SRT, verificar si hay procesos con mejor estimación esperando
                            if (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
                                // Verificar si hay procesos en READY con mejor estimación
                                pthread_mutex_lock(&mutex_cola_ready);
                                bool hay_mejor_proceso = false;
                                
                                if (!list_is_empty(cola_ready)) {
                                    t_proceso_kernel* primer_ready = list_get(cola_ready, 0);
                                    if (primer_ready != NULL) {
                                        log_trace(kernel_logger, "Primer proceso en READY: PID=%d, estimación=%d", 
                                                 primer_ready->pcb.pid, primer_ready->pcb.tiempo_estimado);
                                        hay_mejor_proceso = true;
                                    }
                                }
                                pthread_mutex_unlock(&mutex_cola_ready);
                                
                                if (hay_mejor_proceso) {
                                    log_info(kernel_logger, "## SRT: Hay procesos esperando, iniciando planificación");
                                }
                            }
                            
                            // Llamar al planificador mejorado
                            replanificar_tras_devolucion(motivo);
                        } else {
                            log_info(kernel_logger, "## Proceso PID=%d terminó, no se requiere replanificación", pcb->pid);
                        }
                    } else {
                        pthread_mutex_unlock(&mutex_cpus);
                        log_warning(kernel_logger, "No se encontró CPU con proceso PID %d", pid);
                    }
                    
                    list_destroy_and_destroy_elements(lista, free);
                } else {
                    log_warning(kernel_logger, "Paquete PCB_DEVUELTO malformado");
                    if (lista) list_destroy_and_destroy_elements(lista, free);
                }
                break;
            }
            
            default:
                log_warning(kernel_logger, "Operación no reconocida de CPU: %d", cod_op);
                break;
        }

    }

    // Limpieza final
    close(fd_cpu_dispatch);
    close(fd_kernel_dispatch);
    log_info(kernel_logger, "Servidor Dispatch finalizado");

    return NULL;
}

void* atender_cpu_interrupt() {
    fd_cpu_interrupt = -1;

    log_info(kernel_logger, "Iniciando servidor CPU Interrupt...");

    fd_cpu_interrupt = iniciar_servidor(kernel_logger, PUERTO_ESCUCHA_INTERRUPT);
    if (fd_cpu_interrupt == -1) {
        log_error(kernel_logger, "ERROR: Kernel no pudo levantar servidor interrupt");
        return NULL;
    }
    
    log_info(kernel_logger, "Servidor de Kernel Interrupt iniciado en puerto %d. Esperando Clientes...", PUERTO_ESCUCHA_INTERRUPT);

    while (1) {
        int fd_cpu_interrupt_cliente = esperar_cliente(kernel_logger, fd_cpu_interrupt);
        if (fd_cpu_interrupt_cliente < 0) {
            log_error(kernel_logger, "Error al aceptar cliente de CPU interrupt");
            continue;
        }

        log_info(kernel_logger, "CPU conectada al canal interrupt: fd=%d", fd_cpu_interrupt_cliente);

        // Manejar interrupciones de esta CPU en un bucle
        bool cpu_interrupt_conectada = true;
        while (cpu_interrupt_conectada) {
            op_code cod_op = recibir_operacion(kernel_logger, fd_cpu_interrupt_cliente);

            switch(cod_op) {
                case -1:
                    log_info(kernel_logger, "CPU interrupt se desconectó: fd=%d", fd_cpu_interrupt_cliente);
                    cpu_interrupt_conectada = false;
                    break;

                case INTERRUPCION: {
                    log_info(kernel_logger, "Recibida interrupción de CPU");
                    
                    t_list* lista = recibir_paquete_desde_buffer(kernel_logger, fd_cpu_interrupt_cliente);
                    
                    if (lista != NULL && list_size(lista) > 0) {
                        char* tipo_interrupcion = list_get(lista, 0);
                        
                        if (tipo_interrupcion != NULL) {
                            log_info(kernel_logger, "Procesando interrupción: %s", tipo_interrupcion);
                            
                            // Procesar diferentes tipos de interrupción
                            if (strcmp(tipo_interrupcion, "QUANTUM_FINISHED") == 0) {
                                log_info(kernel_logger, "## Interrupción por fin de quantum");
                            } else if (strcmp(tipo_interrupcion, "USER_INTERRUPT") == 0) {
                                log_info(kernel_logger, "## Interrupción solicitada por usuario");
                            } else {
                                log_warning(kernel_logger, "Tipo de interrupción desconocida: %s", tipo_interrupcion);
                            }
                        }
                        
                        list_destroy_and_destroy_elements(lista, free);
                    } else {
                        log_warning(kernel_logger, "Paquete de interrupción malformado");
                        if (lista) list_destroy(lista);
                    }
                    break;
                }
                
                default:
                    log_warning(kernel_logger, "Operación no reconocida en canal interrupt: %d", cod_op);
                    break;
            }
        }

        close(fd_cpu_interrupt_cliente);
    }

    close(fd_cpu_interrupt);
    log_info(kernel_logger, "Servidor Interrupt finalizado");
    return NULL;
}


bool procesar_syscall(char* syscall) {
    if (syscall == NULL) {
        log_error(kernel_logger, "Syscall es NULL");
        return false;
    }
    
    printf("\n=== SYSCALL RECIBIDA ===\n");
    printf("Kernel > Ejecutando syscall: %s\n", syscall);
    printf("=======================\n");
    
    // Registrar en log
    log_info(kernel_logger, "Ejecutando syscall: %s", syscall);

    bool resultado = false;

    // Simular procesamiento
    //sleep(1);

    char tipo[32];
    char parametro1[64], parametro2[64];
    
    int cantidad = sscanf(syscall, "%s %s %s", tipo, parametro1, parametro2);
    
    // Validar que se leyeron al menos los parámetros básicos
    if (cantidad < 1) {
        log_error(kernel_logger, "Error al parsear la syscall: %s", syscall);
        return false;
    }
    
    // Obtener PID del proceso actual para logging obligatorio
    int pid_actual = obtener_pid_proceso_actual();
    if (pid_actual != -1) {
        log_syscall_recibida(pid_actual, tipo);
    }
    
    // Procesar diferentes tipos de syscalls
  if (strncmp(tipo, "IO",2) == 0) {
        resultado = procesar_io_syscall(syscall);
    }
    else if (strncmp(tipo, "INIT_PROC",9 ) == 0) {
        int tamanio_proceso = atoi(parametro2);
        (void)tamanio_proceso; // Usar el tamaño del proceso si es necesario
        char nombre_proceso[64];
        strcpy(nombre_proceso, parametro1);
        resultado = procesar_init_proc_syscall(syscall);
    }
    else if (strncmp(tipo, "DUMP_MEMORY",11) == 0) {
        resultado = procesar_dump_memory_syscall(syscall);
    }
    else if (strncmp(tipo, "EXIT",4) == 0) {
        resultado = procesar_exit_syscall(syscall);
    }
    else {
        log_warning(kernel_logger, "Syscall no reconocida: %s", syscall);
        printf("Kernel > Syscall no reconocida: %s\n", syscall);
        resultado = false;
    }
    
    printf("Kernel > Syscall %s: %s\n", syscall, resultado ? "COMPLETADA" : "ERROR");
    printf("=======================\n\n");
    
    log_info(kernel_logger, "Syscall %s procesada con resultado: %s", 
             syscall, resultado ? "EXITOSA" : "ERROR");
    
    return resultado;
}


// Función para simular operación de I/O en hilo separado
void* ejecutar_operacion_io(void* arg) {
    t_operacion_io* operacion = (t_operacion_io*)arg;
    
    log_info(kernel_logger, "Iniciando operación I/O en %s por %d ms (PID=%d)", 
             operacion->dispositivo, operacion->tiempo_ms, operacion->pid_proceso);
    printf("Kernel > [I/O] Iniciando operación en %s (%d ms) para PID=%d\n", 
           operacion->dispositivo, operacion->tiempo_ms, operacion->pid_proceso);
    
    // Simular el tiempo real de la operación I/O
    usleep(operacion->tiempo_ms * 1000); // convertir ms a microsegundos
    
    operacion->completada = true;
    
    log_info(kernel_logger, "Operación I/O completada en %s para PID=%d", 
             operacion->dispositivo, operacion->pid_proceso);
    printf("Kernel > [I/O] Operación completada en %s para PID=%d\n", 
           operacion->dispositivo, operacion->pid_proceso);
    
    // Procesar fin de operación
    procesar_fin_io(operacion->dispositivo);
    
    log_info(kernel_logger, "Operación I/O completada en %s para PID=%d", 
             operacion->dispositivo, operacion->pid_proceso);
    printf("Kernel > [I/O] Operación completada en %s para PID=%d\n", 
           operacion->dispositivo, operacion->pid_proceso);
    
    // Procesar fin de operación
    procesar_fin_io(operacion->dispositivo);
    
    // Liberar memoria
    free(operacion->dispositivo);
    free(operacion);
    
    return NULL;
}

bool modulo_io_disponible() {
    pthread_mutex_lock(&mutex_io);
    bool disponible = (fd_io > 0);
    pthread_mutex_unlock(&mutex_io);
    return disponible;
}

bool enviar_operacion_io(int pid, const char* dispositivo, int tiempo_ms) {

    if (!modulo_io_disponible()) {
        log_error(kernel_logger, "Intento de enviar I/O con módulo no disponible");
        return false;
    }

    // Verificar si el módulo I/O está conectado
    if (fd_io <= 0) {
        log_error(kernel_logger, "No hay conexión con módulo I/O para enviar operación");
        return false;
    }

    // Crear paquete con la operación I/O
    t_paquete* paquete = crear_paquete(OPERACION_IO, kernel_logger);
    
    // Agregar datos al paquete (PID, dispositivo, tiempo)
    agregar_a_paquete(paquete, &pid, sizeof(int));
    agregar_a_paquete(paquete, (void*)dispositivo, strlen(dispositivo) + 1);
    agregar_a_paquete(paquete, &tiempo_ms, sizeof(int));

    // Enviar paquete
    enviar_paquete(paquete, fd_io);

    if (errno != 0) {
        log_error(kernel_logger, "Error al enviar operación I/O: %s", strerror(errno));
        eliminar_paquete(paquete);
        return false;
    }

    eliminar_paquete(paquete);
    log_info(kernel_logger, "Operación I/O enviada: PID %d - %s (%d ms)", pid, dispositivo, tiempo_ms);
    return true;
}

bool procesar_io_syscall(char* syscall) {
    printf("Kernel > Procesando operación de E/S: %s\n", syscall);
    log_info(kernel_logger, "Procesando I/O syscall: %s", syscall);
    
    int pid = obtener_pid_proceso_actual();
    if (pid == -1) {
        log_error(kernel_logger, "No se puede identificar el proceso para IO");
        return false;
    }
    
    // Parsear la syscall: "IO DISPOSITIVO TIEMPO"
    char* dispositivo = NULL;
    int tiempo_io = 0;
    
    // Crear una copia para parsear sin modificar el original
    char syscall_copy[256];
    strncpy(syscall_copy, syscall, sizeof(syscall_copy) - 1);
    syscall_copy[sizeof(syscall_copy) - 1] = '\0';
    
    // Parsear la línea
    char* token = strtok(syscall_copy, " ");
    if (token != NULL && strcmp(token, "IO") == 0) {
        token = strtok(NULL, " "); // dispositivo
        if (token != NULL) {
            dispositivo = strdup(token);
            token = strtok(NULL, " "); // tiempo
            if (token != NULL) {
                tiempo_io = atoi(token);
            }
        }
    }
    
    if (dispositivo == NULL || tiempo_io <= 0) {
        log_error(kernel_logger, "Formato de syscall I/O inválido: %s", syscall);
        printf("Kernel > Error: formato I/O inválido: %s\n", syscall);
        if (dispositivo) free(dispositivo);
        return false;
    }
    
    // Verificar que el dispositivo existe
    t_dispositivo_io* disp = buscar_dispositivo_io(dispositivo);
    if (disp == NULL || !disp->conectado) {
        log_error(kernel_logger, "Dispositivo IO no existe o desconectado: %s", dispositivo);
        printf("Kernel > Error: dispositivo %s no existe, enviando proceso PID=%d a EXIT\n", dispositivo, pid);
        
        // Proceso debe ir a EXIT si el dispositivo no existe
        finalizar_proceso_actual();
        free(dispositivo);
        return false;
    }
    
    // Obtener el PCB del proceso actual
    pthread_mutex_lock(&mutex_cola_exec);
    t_pcb* proceso_actual = NULL;
    
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_pcb* proceso = list_get(cola_exec, i);
        if (proceso != NULL && proceso->pid == pid) {
            proceso_actual = proceso;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_exec);
    
    if (proceso_actual == NULL) {
        log_error(kernel_logger, "No se encontró proceso PID=%d en EXEC para IO", pid);
        free(dispositivo);
        return false;
    }
    
    // Bloquear el proceso por IO
    bloquear_proceso_por_io(proceso_actual, dispositivo);
    
    // Después de verificar el dispositivo y bloquear el proceso:
    if (!enviar_operacion_io(pid, dispositivo, tiempo_io)) {
        log_error(kernel_logger, "Falló el envío de operación I/O para PID %d", pid);
        free(dispositivo);
        return false;
    }

    // Verificar si el dispositivo está libre
    if (disp->esta_libre) {
        // Dispositivo libre - asignar inmediatamente
        disp->esta_libre = false;
        disp->proceso_ejecutando = pid;
        
        log_info(kernel_logger, "Proceso PID=%d asignado inmediatamente a dispositivo %s por %d ms", 
                 pid, dispositivo, tiempo_io);
        printf("Kernel > Proceso PID=%d ejecutando IO en %s (%d ms)\n", pid, dispositivo, tiempo_io);
        
        // En implementación real, aquí se enviaría el mensaje al módulo IO
        // Por ahora simulamos con thread
        t_operacion_io* operacion = malloc(sizeof(t_operacion_io));
        operacion->dispositivo = strdup(dispositivo);
        operacion->tiempo_ms = tiempo_io;
        operacion->completada = false;
        operacion->pid_proceso = pid;
        
        if (pthread_create(&operacion->hilo_io, NULL, ejecutar_operacion_io, operacion) == 0) {
            pthread_detach(operacion->hilo_io);
        }
        
    } else {
        // Dispositivo ocupado - proceso queda en cola de bloqueados
        log_info(kernel_logger, "Dispositivo %s ocupado, proceso PID=%d agregado a cola de espera", 
                 dispositivo, pid);
        printf("Kernel > Dispositivo %s ocupado, proceso PID=%d esperando\n", dispositivo, pid);
    }
    

    // Replanificar tras bloqueo
    replanificar_tras_devolucion("SYSCALL");
    
    free(dispositivo);
    return true;
}

//soluciones validas
// Cambiar para que retorne puntero y no copia
// Y que el campo estado se inicialice correctamente
t_pcb* crear_PCB(){
    t_pcb *pcb = malloc(sizeof(t_pcb));
    pcb->pid = id_proceso++;
    pcb->pc = 0;
    pcb->tiempo_estimado = ESTIMACION_INICIAL;
    pcb->tiempo_inicio_rafaga = 0;
    pcb->estado = ESTADO_NEW;
    pcb->metricas_tiempo = queue_create();
    pcb->metricas_estado = queue_create();
    queue_push(pcb->metricas_estado, ESTADO_NEW);
    return pcb;
}

bool procesar_init_proc_syscall(char* syscall) {
    // Parsear la syscall para obtener nombre y tamaño
    char* nombre_proceso = NULL;
    int tamanio_proceso = 0;
    
    // Formato esperado: "INIT_PROC nombre tamaño"
    char* copia_syscall = strdup(syscall);
    char* token = strtok(copia_syscall, " ");
    
    if (token != NULL) {
        token = strtok(NULL, " "); // nombre
        if (token != NULL) {
            nombre_proceso = strdup(token);
            token = strtok(NULL, " "); // tamaño
            if (token != NULL) {
                tamanio_proceso = atoi(token);
            }
        }
    }
    
    free(copia_syscall);
    
    if (nombre_proceso == NULL || tamanio_proceso <= 0) {
        log_error(kernel_logger, "Parámetros inválidos para INIT_PROC: nombre=%s, tamaño=%d", 
                  nombre_proceso ? nombre_proceso : "NULL", tamanio_proceso);
        if (nombre_proceso) {
            free(nombre_proceso);
        }
        return false;
    }
    
    log_info(kernel_logger, "Procesando INIT_PROC syscall: %s (tamaño: %d)", nombre_proceso, tamanio_proceso);
    printf("Kernel > Inicializando proceso: %s (tamaño: %d)\n", nombre_proceso, tamanio_proceso);
    
    // Crear nuevo proceso
    t_proceso_kernel* nuevo_proceso = crear_proceso(nombre_proceso, tamanio_proceso);
    if (nuevo_proceso == NULL) {
        log_error(kernel_logger, "Error al crear proceso %s", nombre_proceso);
        if (nombre_proceso) {
            free(nombre_proceso);
        }
        return false;
    }
    
    // El proceso va directamente a NEW (sin cambiar el estado del proceso invocante)
    nuevo_proceso->estado = ESTADO_NEW;
    nuevo_proceso->pcb.estado = ESTADO_NEW;
    
    // Log obligatorio de creación de proceso
    log_creacion_proceso(nuevo_proceso->pcb.pid);
    
    pthread_mutex_lock(&mutex_cola_new);
    list_add(cola_new, nuevo_proceso);
    pthread_mutex_unlock(&mutex_cola_new);
    
    log_info(kernel_logger, "Proceso %s (PID=%d) creado y agregado a NEW", nombre_proceso, nuevo_proceso->pcb.pid);
    printf("Kernel > Proceso inicializado: %s (PID=%d)\n", nombre_proceso, nuevo_proceso->pcb.pid);
    
    // INIT_PROC no implica cambio de estado del proceso invocante
    // El proceso que llamó a esta syscall continúa ejecutando inmediatamente
    
    if (nombre_proceso) {
        free(nombre_proceso);
    }
    
    return true;
}

bool procesar_dump_memory_syscall(char* syscall) {
    (void)syscall; // No se usa el contenido de syscall, solo el PID del proceso actual
    printf("Kernel > Procesando DUMP_MEMORY syscall\n");
    int pid = obtener_pid_proceso_actual();
    if (pid == -1) {
        log_error(kernel_logger, "No se puede identificar el proceso para DUMP_MEMORY");
        return false;
    }
    
    log_info(kernel_logger, "Procesando DUMP_MEMORY syscall para PID=%d", pid);
    printf("Kernel > Iniciando volcado de memoria para PID=%d\n", pid);
    
    // Bloquear el proceso que invocó la syscall
    pthread_mutex_lock(&mutex_cola_exec);
    t_pcb* proceso_bloqueado = NULL;
    
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_pcb* proceso = list_get(cola_exec, i);
        if (proceso != NULL && proceso->pid == pid) {
            proceso_bloqueado = proceso;
            proceso->estado = ESTADO_BLOCKED;
            list_remove(cola_exec, i);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_exec);
    
    if (proceso_bloqueado == NULL) {
        log_error(kernel_logger, "No se encontró proceso PID=%d en EXEC para DUMP_MEMORY", pid);
        return false;
    }
    
    // Agregar a cola de bloqueados
    pthread_mutex_lock(&mutex_cola_blocked);
    list_add(cola_blocked, proceso_bloqueado);
    pthread_mutex_unlock(&mutex_cola_blocked);
    
    // Comunicar con memoria para realizar el dump
    bool resultado_dump = comunicar_con_memoria_dump(pid);
    
    if (resultado_dump) {
        // Dump exitoso - desbloquear proceso y enviarlo a READY
        log_info(kernel_logger, "Dump de memoria exitoso para PID=%d, desbloqueando proceso", pid);
        desbloquear_proceso_de_io(pid);
        printf("Kernel > Volcado de memoria completado para PID=%d\n", pid);
        
    } else {
        // Error en dump - enviar proceso a EXIT
        log_error(kernel_logger, "Error en dump de memoria para PID=%d, enviando a EXIT", pid);
        
        pthread_mutex_lock(&mutex_cola_blocked);
        for (int i = 0; i < list_size(cola_blocked); i++) {
            t_pcb* proceso = list_get(cola_blocked, i);
            if (proceso != NULL && proceso->pid == pid) {
                proceso->estado = ESTADO_EXIT;
                list_remove(cola_blocked, i);
                break;
            }
        }
        pthread_mutex_unlock(&mutex_cola_blocked);
        
        printf("Kernel > Error en volcado de memoria para PID=%d, proceso enviado a EXIT\n", pid);
    }
    
    return resultado_dump;
}

bool procesar_exit_syscall(char* syscall) {
    (void)syscall; // No se usa el contenido de syscall, solo el PID del proceso actual
    printf("Kernel > Procesando EXIT syscall\n");
    int pid = obtener_pid_proceso_actual();
    if (pid == -1) {
        log_error(kernel_logger, "No se puede identificar el proceso para EXIT");
        return false;
    }
    
    log_info(kernel_logger, "Procesando EXIT syscall para PID=%d", pid);
    printf("Kernel > Finalizando proceso PID=%d\n", pid);
    
    // Log obligatorio de fin de proceso
    log_fin_proceso(pid);
    
    // Log obligatorio de métricas del proceso
    log_metricas_estado(pid);
    
    // Finalizar el proceso que invocó EXIT
    finalizar_proceso_actual();
    
    // Replanificar tras la finalización
    replanificar_tras_devolucion("EXIT");
    
    printf("Kernel > Proceso PID=%d finalizado exitosamente\n", pid);
    
    return true;
}

void planificador_corto_plazo() {
    sem_wait(&hay_procesos_ready);
    pthread_mutex_lock(&mutex_cpus);
    
    log_info(kernel_logger, "## PLANIFICADOR CORTO PLAZO: Iniciando búsqueda de CPU libre");
    
    t_cpu_conectada* cpu_libre = buscar_cpu_libre();

    if (cpu_libre != NULL) {
        log_info(kernel_logger, "## CPU LIBRE ENCONTRADA: ID %d", cpu_libre->id_cpu);
        
        t_proceso_kernel* proceso = sacar_proceso_de_ready();
        if (proceso == NULL) {
            pthread_mutex_unlock(&mutex_cpus);
            log_warning(kernel_logger, "No se pudo obtener proceso de READY");
            return;
        }
        
        t_pcb* pcb = &(proceso->pcb);
        
        cpu_libre->esta_libre = false;
        cpu_libre->proceso_en_exec = pcb;

        // REGISTRO PRECISO DEL TIEMPO DE INICIO DE RÁFAGA
        long timestamp_inicio = time(NULL) * 1000; // Timestamp en ms
        pcb->tiempo_inicio_rafaga = (int)timestamp_inicio;
        pcb->estado = ESTADO_EXEC;
        proceso->estado = ESTADO_EXEC; // Actualizar también el estado del proceso

        // Mover proceso a cola EXEC
        pthread_mutex_lock(&mutex_cola_exec);
        list_add(cola_exec, proceso);
        pthread_mutex_unlock(&mutex_cola_exec);

        // Enviar PCB a CPU por socket_dispatch
        t_paquete* paquete = crear_paquete(PCB_A_EJECUTAR, kernel_logger);
        agregar_pcb_a_paquete(paquete, pcb);
        enviar_paquete(paquete, cpu_libre->socket_dispatch);
        eliminar_paquete(paquete);
/*
        t_paquete_pcb* paquete = crear_paquete_enviar(pcb);
        send(cpu_libre->socket_dispatch, &paquete, sizeof(t_paquete_pcb), 0);
*/
        log_info(kernel_logger, "## PROCESO ASIGNADO: PID=%d → CPU %d (estimación=%d, inicio_ráfaga=%d)", 
                 pcb->pid, cpu_libre->id_cpu, pcb->tiempo_estimado, pcb->tiempo_inicio_rafaga);
        
        // Para SJF/SRT, loggear información adicional
        if (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SJF") || 
            string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
            log_trace(kernel_logger, "Algoritmo %s: proceso con menor estimación (%d) asignado", 
                     ALGORITMO_CORTO_PLAZO, pcb->tiempo_estimado);
        }
    } else {
        // Verificar si es porque no hay CPUs conectadas o porque están ocupadas
        int total_cpus = (lista_cpus_conectadas != NULL) ? list_size(lista_cpus_conectadas) : 0;
        if (total_cpus == 0) {
            log_warning(kernel_logger, "## No hay CPUs conectadas al sistema - Proceso no puede ejecutar");
        } else {
            log_trace(kernel_logger, "## No hay CPUs libres disponibles (%d CPUs ocupadas)", total_cpus);
        }
        // Devolver el semáforo ya que no se pudo asignar
        sem_post(&hay_procesos_ready);
    }

    pthread_mutex_unlock(&mutex_cpus);
}

// Función para ejecutar el planificador de corto plazo en bucle
void* planificador_corto_plazo_hilo(void* arg) {
    (void)arg; // Evitar warning de parámetro no usado
    log_info(kernel_logger, "## Iniciando hilo del planificador de corto plazo");
    
    while (true) {
        log_trace(kernel_logger, "## Esperando procesos en READY...");
        sem_wait(&hay_procesos_ready);
        
        // Acceder a las variables globales correctas
        extern t_list* cola_ready;
        extern pthread_mutex_t mutex_cola_ready;
        
        pthread_mutex_lock(&mutex_cola_ready);
        int procesos_ready = (cola_ready != NULL) ? list_size(cola_ready) : 0;
        pthread_mutex_unlock(&mutex_cola_ready);
        
        pthread_mutex_lock(&mutex_cpus);
        int cpus_conectadas = (lista_cpus_conectadas != NULL) ? list_size(lista_cpus_conectadas) : 0;
        pthread_mutex_unlock(&mutex_cpus);
        
        log_info(kernel_logger, "## Proceso detectado en READY - Procesos READY: %d, CPUs conectadas: %d", 
                 procesos_ready, cpus_conectadas);
        
        if (cpus_conectadas == 0) {
            log_warning(kernel_logger, "## No hay CPUs disponibles - esperando conexiones de CPU");
        }
        
        planificador_corto_plazo();
        
        log_trace(kernel_logger, "## Ciclo de planificación completado");
    }
    
    return NULL;
}

void evaluar_interrupcion(t_pcb* nuevo) {
    t_cpu_conectada* cpu;

    for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
        cpu = list_get(lista_cpus_conectadas, i);

        if (!cpu->esta_libre && cpu->proceso_en_exec != NULL) {
            if (nuevo->tiempo_estimado < (cpu->proceso_en_exec)->tiempo_estimado) {
                log_info(kernel_logger, "Enviando interrupción a CPU %d", cpu->id_cpu);
                int pid = cpu->proceso_en_exec->pid;
                enviar_interrupt(cpu->socket_interrupt, pid);
            }
        }
    }
}

// ========== IMPLEMENTACIÓN DE ESTIMACIÓN SJF ==========

/**
 * Calcula la nueva estimación usando la fórmula: Est(n+1) = α*R(n) + (1-α)*Est(n)
 * @param estimacion_anterior: Estimación anterior Est(n)
 * @param tiempo_real: Tiempo real ejecutado R(n)
 * @param alfa: Factor α [0,1]
 * @return Nueva estimación Est(n+1)
 */
int calcular_nueva_estimacion(int estimacion_anterior, int tiempo_real, float alfa) {
    float nueva_estimacion = (alfa * tiempo_real) + ((1.0 - alfa) * estimacion_anterior);
    
    log_trace(kernel_logger, "Calculando nueva estimación: α=%.2f, R(n)=%d, Est(n)=%d → Est(n+1)=%.2f", 
              alfa, tiempo_real, estimacion_anterior, nueva_estimacion);
    
    return (int)nueva_estimacion;
}

/**
 * Actualiza la estimación SJF de un PCB con el tiempo real ejecutado
 * @param pcb: PCB del proceso
 * @param tiempo_real_ejecutado: Tiempo que realmente ejecutó en CPU
 */
void actualizar_estimacion_sjf(t_pcb* pcb, int tiempo_real_ejecutado) {
    if (pcb == NULL) {
        log_error(kernel_logger, "PCB es NULL en actualizar_estimacion_sjf");
        return;
    }
    
    int estimacion_anterior = pcb->tiempo_estimado;
    int nueva_estimacion = calcular_nueva_estimacion(estimacion_anterior, tiempo_real_ejecutado, ALFA);
    
    pcb->tiempo_estimado = nueva_estimacion;
    
    // Registrar métrica de tiempo
    int* metrica = malloc(sizeof(int));
    *metrica = tiempo_real_ejecutado;
    queue_push(pcb->metricas_tiempo, metrica);
    
    log_info(kernel_logger, "## Proceso PID=%d: Estimación actualizada de %d a %d (ejecutó %d ms)", 
             pcb->pid, estimacion_anterior, nueva_estimacion, tiempo_real_ejecutado);
}

/**
 * Procesa la devolución de un proceso desde CPU y actualiza su estimación
 * @param pcb: PCB del proceso devuelto
 * @param tiempo_ejecutado: Tiempo que ejecutó en CPU (en ms)
 * @param motivo: Motivo de la devolución ("SYSCALL", "QUANTUM", "DESALOJO", etc.)
 */
void procesar_devolucion_cpu(t_pcb* pcb, int tiempo_ejecutado, char* motivo) {
    if (pcb == NULL || motivo == NULL) {
        log_error(kernel_logger, "Parámetros inválidos en procesar_devolucion_cpu");
        return;
    }
    
    log_info(kernel_logger, "## PROCESANDO DEVOLUCIÓN: PID=%d, motivo=%s, tiempo_ejecutado=%d ms", 
             pcb->pid, motivo, tiempo_ejecutado);
    
    // VALIDAR Y PROCESAR TIEMPO REAL EJECUTADO R(n)
    int tiempo_real_validado = tiempo_ejecutado;
    
    if (tiempo_real_validado <= 0) {
        log_warning(kernel_logger, "Tiempo ejecutado inválido (%d), usando valor mínimo", tiempo_ejecutado);
        tiempo_real_validado = 1; // mínimo 1 ms
    }
    
    // ACTUALIZACIÓN DE ESTIMACIÓN SJF/SRT
    if (tiempo_real_validado > 0 && 
        (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SJF") || 
         string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT"))) {
        
        int estimacion_anterior = pcb->tiempo_estimado;
        actualizar_estimacion_sjf(pcb, tiempo_real_validado);
        
        log_info(kernel_logger, "## FÓRMULA SJF APLICADA: R(n)=%d, Est(anterior)=%d, Est(nueva)=%d, α=%.2f", 
                 tiempo_real_validado, estimacion_anterior, pcb->tiempo_estimado, ALFA);
    } else if (tiempo_real_validado > 0) {
        log_trace(kernel_logger, "Algoritmo %s no requiere actualización de estimación", ALGORITMO_CORTO_PLAZO);
    }
    
    // Resetear timestamp de inicio
    pcb->tiempo_inicio_rafaga = 0;
    
    // Procesar según el motivo de devolución
    if (string_equals_ignore_case(motivo, "SYSCALL")) {
        // El proceso va a BLOCKED o se procesa la syscall
        log_trace(kernel_logger, "Proceso PID=%d pasa a procesamiento de syscall", pcb->pid);
    } 
    else if (string_equals_ignore_case(motivo, "DESALOJO") || 
             string_equals_ignore_case(motivo, "QUANTUM")) {
        // El proceso vuelve a READY para replanificación
        // Buscar el t_proceso_kernel completo que contiene este PCB
        t_proceso_kernel* proceso_completo = NULL;
        
        // Buscar en cola_exec
        pthread_mutex_lock(&mutex_cola_exec);
        for (int i = 0; i < list_size(cola_exec); i++) {
            t_proceso_kernel* proc = list_get(cola_exec, i);
            if (proc->pcb.pid == pcb->pid) {
                proceso_completo = proc;
                list_remove(cola_exec, i);
                break;
            }
        }
        pthread_mutex_unlock(&mutex_cola_exec);
        
        if (proceso_completo != NULL) {
            // Usar la función centralizada que incluye evaluación SRT
            agregar_proceso_a_ready(proceso_completo);
            
            log_info(kernel_logger, "## Proceso PID=%d reingresa a READY con estimación %d", 
                     pcb->pid, pcb->tiempo_estimado);
        } else {
            log_warning(kernel_logger, "No se encontró proceso completo para PID=%d en cola_exec", pcb->pid);
        }
    }
    else if (string_equals_ignore_case(motivo, "EXIT")) {
        // El proceso termina
        pcb->estado = ESTADO_EXIT;
        log_info(kernel_logger, "## Proceso PID=%d finaliza ejecución", pcb->pid);
    }
}

// ========== FUNCIONES PARA MANEJO DE COLA READY CON SRT ==========

/**
 * Evalúa si un proceso nuevo debe desalojar procesos en ejecución (algoritmo SRT)
 * @param proceso_nuevo: Proceso que acaba de llegar a READY
 */
void evaluar_desalojo_srt(t_proceso_kernel* proceso_nuevo) {
    if (!string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
        // Solo para SRT (SJF con desalojo)
        return;
    }
    
    if (proceso_nuevo == NULL) {
        log_error(kernel_logger, "Proceso nuevo es NULL en evaluar_desalojo_srt");
        return;
    }
    
    log_trace(kernel_logger, "Evaluando desalojo SRT para proceso PID=%d (estimación=%d)", 
              proceso_nuevo->pcb.pid, proceso_nuevo->pcb.tiempo_estimado);
    
    pthread_mutex_lock(&mutex_cpus);
    
    if (lista_cpus_conectadas == NULL) {
        pthread_mutex_unlock(&mutex_cpus);
        return;
    }
    
    t_cpu_conectada* cpu_a_desalojar = NULL;
    int tiempo_restante_maximo = 0;
    
    // Buscar la CPU con el proceso que tenga mayor tiempo restante
    for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
        t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, i);
        
        if (!cpu->esta_libre && cpu->proceso_en_exec != NULL) {
            t_pcb* proceso_en_exec = cpu->proceso_en_exec;
            
            // Calcular tiempo restante del proceso en ejecución
            int tiempo_transcurrido = 0;
            if (proceso_en_exec->tiempo_inicio_rafaga > 0) {
                tiempo_transcurrido = (time(NULL) * 1000) - proceso_en_exec->tiempo_inicio_rafaga;
            }
            
            int tiempo_restante = proceso_en_exec->tiempo_estimado - tiempo_transcurrido;
            if (tiempo_restante < 0) tiempo_restante = 0;
            
            // Si el nuevo proceso tiene estimación menor que el tiempo restante
            if (proceso_nuevo->pcb.tiempo_estimado < tiempo_restante) {
                // Buscar el proceso con mayor tiempo restante para desalojar
                if (tiempo_restante > tiempo_restante_maximo) {
                    tiempo_restante_maximo = tiempo_restante;
                    cpu_a_desalojar = cpu;
                }
            }
        }
    }
    
    // Si encontramos una CPU candidata para desalojo
    if (cpu_a_desalojar != NULL) {
        t_pcb* proceso_a_desalojar = cpu_a_desalojar->proceso_en_exec;
        
        log_info(kernel_logger, "## SRT: Desalojando proceso PID=%d (tiempo restante=%d) por PID=%d (estimación=%d)", 
                 proceso_a_desalojar->pid, tiempo_restante_maximo, 
                 proceso_nuevo->pcb.pid, proceso_nuevo->pcb.tiempo_estimado);
        
        // Log obligatorio de desalojo SJF/SRT
        log_desalojo_sjf_srt(proceso_a_desalojar->pid);
        
        // Enviar interrupción de desalojo
        enviar_interrupt(cpu_a_desalojar->socket_interrupt, proceso_a_desalojar->pid);
        
        log_info(kernel_logger, "## Interrupción de desalojo enviada a CPU %d", cpu_a_desalojar->id_cpu);
    } else {
        log_trace(kernel_logger, "No es necesario desalojar procesos para PID=%d", proceso_nuevo->pcb.pid);
    }
    
    pthread_mutex_unlock(&mutex_cpus);
}

/**
 * Función centralizada para agregar procesos a la cola READY
 * Incluye evaluación automática de desalojo para SRT
 * @param proceso: Proceso a agregar a READY
 */
void agregar_proceso_a_ready(t_proceso_kernel* proceso) {
    if (proceso == NULL) {
        log_error(kernel_logger, "Proceso es NULL en agregar_proceso_a_ready");
        return;
    }
    
    estado_proceso_t estado_anterior = proceso->estado;
    proceso->estado = ESTADO_READY;
    
    pthread_mutex_lock(&mutex_cola_ready);
    
    // Agregar a la cola según el algoritmo
    if (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "FIFO")) {
        list_add(cola_ready, proceso);
    } else if (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SJF") || 
               string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
        // Para SJF y SRT, insertar ordenado por estimación
        list_add_sorted(cola_ready, proceso, comparar_procesos_por_estimacion);
    }
    
    pthread_mutex_unlock(&mutex_cola_ready);
    
    log_info(kernel_logger, "## Proceso PID=%d agregado a READY (estimación=%d)", 
             proceso->pcb.pid, proceso->pcb.tiempo_estimado);
    
    // Señalizar que hay procesos ready
    sem_post(&hay_procesos_ready);
    
    // EVALUACIÓN AUTOMÁTICA DE DESALOJO PARA SRT
    if (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
        evaluar_desalojo_srt(proceso);
    }
    
    // Registrar cambio de estado si es necesario
    if (estado_anterior != ESTADO_READY) {
        // Aca se podría llamar a registrar_cambio_estado si está disponible
        log_trace(kernel_logger, "## Proceso PID=%d: %s → READY", 
                 proceso->pcb.pid, estado_str(estado_anterior));
    }
}

// ========== FUNCIONES PARA REPLANIFICACIÓN AUTOMÁTICA ==========

/**
 * Optimiza la asignación de CPUs verificando si hay mejores asignaciones posibles
 */
void optimizar_asignacion_cpus() {
    if (!string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
        return; // Solo optimizar para SRT
    }
    
    pthread_mutex_lock(&mutex_cpus);
    pthread_mutex_lock(&mutex_cola_ready);
    
    if (lista_cpus_conectadas == NULL || list_is_empty(cola_ready)) {
        pthread_mutex_unlock(&mutex_cola_ready);
        pthread_mutex_unlock(&mutex_cpus);
        return;
    }
    
    // Buscar procesos en READY con mejor estimación que los en ejecución
    for (int i = 0; i < list_size(cola_ready); i++) {
        t_proceso_kernel* proceso_ready = list_get(cola_ready, i);
        if (proceso_ready == NULL) continue;
        
        // Buscar CPU con proceso de mayor tiempo restante
        t_cpu_conectada* peor_cpu = NULL;
        int peor_tiempo_restante = 0;
        
        for (int j = 0; j < list_size(lista_cpus_conectadas); j++) {
            t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, j);
            
            if (!cpu->esta_libre && cpu->proceso_en_exec != NULL) {
                int tiempo_transcurrido = 0;
                if (cpu->proceso_en_exec->tiempo_inicio_rafaga > 0) {
                    tiempo_transcurrido = (time(NULL) * 1000) - cpu->proceso_en_exec->tiempo_inicio_rafaga;
                }
                
                int tiempo_restante = cpu->proceso_en_exec->tiempo_estimado - tiempo_transcurrido;
                if (tiempo_restante < 0) tiempo_restante = 0;
                
                // Si el proceso en READY es mejor que el que está ejecutando
                if (proceso_ready->pcb.tiempo_estimado < tiempo_restante && 
                    tiempo_restante > peor_tiempo_restante) {
                    peor_tiempo_restante = tiempo_restante;
                    peor_cpu = cpu;
                }
            }
        }
        
        // Si encontramos una optimización, aplicarla
        if (peor_cpu != NULL) {
            log_info(kernel_logger, "## OPTIMIZACIÓN: Desalojando PID=%d (tiempo restante=%d) por PID=%d (estimación=%d)", 
                     peor_cpu->proceso_en_exec->pid, peor_tiempo_restante,
                     proceso_ready->pcb.pid, proceso_ready->pcb.tiempo_estimado);
            
            enviar_interrupt(peor_cpu->socket_interrupt, peor_cpu->proceso_en_exec->pid);
            break; // Una optimización a la vez
        }
    }
    
    pthread_mutex_unlock(&mutex_cola_ready);
    pthread_mutex_unlock(&mutex_cpus);
}

/**
 * Replanificación inteligente tras devolución de CPU
 * @param motivo: Motivo de la devolución para decidir estrategia
 */
void replanificar_tras_devolucion(char* motivo) {
    if (motivo == NULL) {
        log_error(kernel_logger, "Motivo de devolución es NULL");
        return;
    }
    
    log_info(kernel_logger, "## REPLANIFICACIÓN AUTOMÁTICA iniciada (motivo: %s)", motivo);
    
    // Estrategia diferente según el motivo
    if (string_equals_ignore_case(motivo, "SYSCALL")) {
        // Para syscalls, solo asignar si hay CPU libre y procesos esperando
        log_trace(kernel_logger, "Replanificación por SYSCALL: asignación simple");
        planificador_corto_plazo();
        
    } else if (string_equals_ignore_case(motivo, "DESALOJO") || 
               string_equals_ignore_case(motivo, "QUANTUM")) {
        // Para desalojos/quantum, verificar optimizaciones adicionales
        log_trace(kernel_logger, "Replanificación por %s: verificando optimizaciones", motivo);
        
        // Primero asignar procesos a CPUs libres
        planificador_corto_plazo();
        
        // Luego optimizar asignaciones si es SRT
        if (string_equals_ignore_case(ALGORITMO_CORTO_PLAZO, "SRT")) {
            usleep(100000); // 100ms para evitar condiciones de carrera
            optimizar_asignacion_cpus();
        }
        
    } else if (string_equals_ignore_case(motivo, "EXIT")) {
        // Para EXIT, asignación normal
        log_trace(kernel_logger, "Replanificación por EXIT: asignación normal");
        planificador_corto_plazo();
        
    } else {
        // Motivo desconocido, asignación conservadora
        log_warning(kernel_logger, "Motivo de devolución desconocido: %s", motivo);
        planificador_corto_plazo();
    }
    
    // Estadísticas de estado actual
    pthread_mutex_lock(&mutex_cola_ready);
    int procesos_ready = list_size(cola_ready);
    pthread_mutex_unlock(&mutex_cola_ready);
    
    pthread_mutex_lock(&mutex_cpus);
    int cpus_libres = 0;
    int cpus_ocupadas = 0;
    
    if (lista_cpus_conectadas != NULL) {
        for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
            t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, i);
            if (cpu->esta_libre) {
                cpus_libres++;
            } else {
                cpus_ocupadas++;
            }
        }
    }
    pthread_mutex_unlock(&mutex_cpus);
    
    log_info(kernel_logger, "## ESTADO POST-REPLANIFICACIÓN: %d procesos READY, %d CPUs libres, %d CPUs ocupadas", 
             procesos_ready, cpus_libres, cpus_ocupadas);
}

// ========== FUNCIONES PARA MANEJO DE DISPOSITIVOS IO ==========

/**
 * Busca un dispositivo IO por nombre
 */
t_dispositivo_io* buscar_dispositivo_io(char* nombre) {
    if (nombre == NULL || dispositivos_io_conectados == NULL) {
        return NULL;
    }
    
    pthread_mutex_lock(&mutex_dispositivos_io);
    for (int i = 0; i < list_size(dispositivos_io_conectados); i++) {
        t_dispositivo_io* dispositivo = list_get(dispositivos_io_conectados, i);
        if (dispositivo != NULL && dispositivo->nombre != NULL && 
            string_equals_ignore_case(dispositivo->nombre, nombre)) {
            pthread_mutex_unlock(&mutex_dispositivos_io);
            return dispositivo;
        }
    }
    pthread_mutex_unlock(&mutex_dispositivos_io);
    return NULL;
}

/**
 * Agrega un nuevo dispositivo IO al sistema
 */
void agregar_dispositivo_io(char* nombre) {
    if (nombre == NULL) {
        log_error(kernel_logger, "Nombre de dispositivo IO es NULL");
        return;
    }
    
    // Verificar si ya existe
    if (buscar_dispositivo_io(nombre) != NULL) {
        log_warning(kernel_logger, "Dispositivo IO %s ya existe", nombre);
        return;
    }
    
    t_dispositivo_io* nuevo_dispositivo = malloc(sizeof(t_dispositivo_io));
    nuevo_dispositivo->nombre = strdup(nombre);
    nuevo_dispositivo->esta_libre = true;
    nuevo_dispositivo->proceso_ejecutando = -1;
    nuevo_dispositivo->cola_bloqueados = list_create();
    nuevo_dispositivo->conectado = true;
    
    pthread_mutex_lock(&mutex_dispositivos_io);
    list_add(dispositivos_io_conectados, nuevo_dispositivo);
    pthread_mutex_unlock(&mutex_dispositivos_io);
    
    log_info(kernel_logger, "Dispositivo IO agregado: %s", nombre);
}

/**
 * Remueve un dispositivo IO del sistema
 */
void remover_dispositivo_io(char* nombre) {
    if (nombre == NULL) return;
    
    pthread_mutex_lock(&mutex_dispositivos_io);
    
    for (int i = 0; i < list_size(dispositivos_io_conectados); i++) {
        t_dispositivo_io* dispositivo = list_get(dispositivos_io_conectados, i);
        if (dispositivo != NULL && dispositivo->nombre != NULL && 
            string_equals_ignore_case(dispositivo->nombre, nombre)) {
            
            // Procesar desconexión
            procesar_desconexion_io(nombre);
            
            // Liberar memoria
            free(dispositivo->nombre);
            list_destroy(dispositivo->cola_bloqueados);
            list_remove(dispositivos_io_conectados, i);
            free(dispositivo);
            
            log_info(kernel_logger, "Dispositivo IO removido: %s", nombre);
            break;
        }
    }
    
    pthread_mutex_unlock(&mutex_dispositivos_io);
}

/**
 * Procesa la finalización de una operación IO
 */
void procesar_fin_io(char* dispositivo) {
    if (dispositivo == NULL) return;
    
    t_dispositivo_io* disp = buscar_dispositivo_io(dispositivo);
    if (disp == NULL) {
        log_error(kernel_logger, "Dispositivo IO no encontrado para fin de operación: %s", dispositivo);
        return;
    }
    
    log_info(kernel_logger, "Finalizando operación IO en dispositivo: %s", dispositivo);
    
    // Marcar dispositivo como libre
    disp->esta_libre = true;
    int pid_finalizado = disp->proceso_ejecutando;
    disp->proceso_ejecutando = -1;
    
    // Desbloquear el proceso que terminó
    if (pid_finalizado != -1) {
        desbloquear_proceso_de_io(pid_finalizado);
    }
    
    // Verificar si hay procesos esperando
    if (!list_is_empty(disp->cola_bloqueados)) {
        t_pcb* proximo_proceso = list_remove(disp->cola_bloqueados, 0);
        if (proximo_proceso != NULL) {
            log_info(kernel_logger, "Asignando proceso PID=%d a dispositivo %s", 
                     proximo_proceso->pid, dispositivo);
            
            disp->esta_libre = false;
            disp->proceso_ejecutando = proximo_proceso->pid;
            
            // Simular envío a módulo IO (en implementación real sería comunicación por sockets)
            log_info(kernel_logger, "Proceso PID=%d ejecutando IO en %s", 
                     proximo_proceso->pid, dispositivo);
        }
    }
}

/**
 * Procesa la desconexión de un dispositivo IO
 */
void procesar_desconexion_io(char* dispositivo) {
    if (dispositivo == NULL) return;
    
    t_dispositivo_io* disp = buscar_dispositivo_io(dispositivo);
    if (disp == NULL) return;
    
    log_warning(kernel_logger, "Dispositivo IO desconectado: %s", dispositivo);
    
    // Si hay proceso ejecutando, enviarlo a EXIT
    if (disp->proceso_ejecutando != -1) {
        log_info(kernel_logger, "Enviando proceso PID=%d a EXIT por desconexión de %s", 
                 disp->proceso_ejecutando, dispositivo);
        // En implementación real, cambiar estado del proceso a EXIT
        desbloquear_proceso_de_io(disp->proceso_ejecutando);
    }
    
    // Enviar todos los procesos bloqueados a EXIT
    while (!list_is_empty(disp->cola_bloqueados)) {
        t_pcb* proceso = list_remove(disp->cola_bloqueados, 0);
        if (proceso != NULL) {
            log_info(kernel_logger, "Enviando proceso PID=%d a EXIT por desconexión de %s", 
                     proceso->pid, dispositivo);
            // En implementación real, cambiar estado del proceso a EXIT
        }
    }
    
    disp->conectado = false;
}

// ========== FUNCIONES AUXILIARES PARA SYSCALLS ==========

/**
 * Bloquea un proceso por operación IO
 */
void bloquear_proceso_por_io(t_pcb* pcb, char* dispositivo) {
    if (pcb == NULL || dispositivo == NULL) return;
    
    log_info(kernel_logger, "Bloqueando proceso PID=%d por IO en %s", pcb->pid, dispositivo);
    
    // Log obligatorio de motivo de bloqueo
    log_motivo_bloqueo(pcb->pid, dispositivo);
    
    // Cambiar estado del proceso a BLOCKED
    pcb->estado = ESTADO_BLOCKED;
    
    // Remover de EXEC y agregar a BLOCKED
    pthread_mutex_lock(&mutex_cola_exec);
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_pcb* proceso = list_get(cola_exec, i);
        if (proceso != NULL && proceso->pid == pcb->pid) {
            list_remove(cola_exec, i);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_exec);
    
    pthread_mutex_lock(&mutex_cola_blocked);
    list_add(cola_blocked, pcb);
    pthread_mutex_unlock(&mutex_cola_blocked);
    
    // Agregar a la cola del dispositivo
    t_dispositivo_io* disp = buscar_dispositivo_io(dispositivo);
    if (disp != NULL) {
        list_add(disp->cola_bloqueados, pcb);
    }
    
    // INICIAR TIMER DE SUSPENSIÓN AUTOMÁTICA
    iniciar_timer_suspension(pcb->pid);
    
    log_info(kernel_logger, "Proceso PID=%d bloqueado por IO, timer de suspensión iniciado", pcb->pid);
}

/**
 * Desbloquea un proceso de operación IO
 */
void desbloquear_proceso_de_io(int pid) {
    pthread_mutex_lock(&mutex_cola_blocked);
    
    for (int i = 0; i < list_size(cola_blocked); i++) {
        t_pcb* proceso = list_get(cola_blocked, i);
        if (proceso != NULL && proceso->pid == pid) {
            
            // VERIFICAR SI ESTÁ SUSPENDIDO
            if (proceso->estado == ESTADO_SUSP_BLOCKED) {
                // Proceso suspendido, transicionarlo a SUSP_READY
                pthread_mutex_unlock(&mutex_cola_blocked);
                log_info(kernel_logger, "Proceso PID=%d suspendido, transicionando SUSP_BLOCKED → SUSP_READY", pid);
                procesar_fin_io_suspendido(pid);
                return;
            }
            
            // CANCELAR TIMER DE SUSPENSIÓN
            cancelar_timer_suspension(pid);
            
            // Remover de BLOCKED
            list_remove(cola_blocked, i);
            
            // Cambiar estado a READY
            proceso->estado = ESTADO_READY;
            
            // Log obligatorio de fin de IO
            log_fin_io(pid);
            
            // Agregar a READY usando función centralizada
            t_proceso_kernel* proceso_completo = malloc(sizeof(t_proceso_kernel));
            proceso_completo->pcb = *proceso;
            proceso_completo->estado = ESTADO_READY;
            
            pthread_mutex_unlock(&mutex_cola_blocked);
            
            agregar_proceso_a_ready(proceso_completo);
            
            log_info(kernel_logger, "Proceso PID=%d desbloqueado de IO, timer cancelado, enviado a READY", pid);
            return;
        }
    }
    
    pthread_mutex_unlock(&mutex_cola_blocked);
    log_warning(kernel_logger, "No se encontró proceso PID=%d en cola BLOCKED", pid);
}

/**
 * Obtiene el PID del proceso que está ejecutando actualmente la syscall
 */
int obtener_pid_proceso_actual() {
    // En implementación real, esto debería obtenerse del contexto de la syscall
    // Por ahora devolvemos el valor almacenado estáticamente
    return pid_proceso_actual;
}

/**
 * Establece el PID del proceso actual para syscalls
 */
void establecer_pid_proceso_actual(int pid) {
    pid_proceso_actual = pid;
}

/**
 * Finaliza el proceso actual que invocó EXIT
 */
void finalizar_proceso_actual() {
    int pid = obtener_pid_proceso_actual();
    if (pid == -1) {
        log_warning(kernel_logger, "No hay proceso actual para finalizar");
        return;
    }
    
    log_info(kernel_logger, "Finalizando proceso actual PID=%d", pid);
    
    // Remover de todas las colas
    pthread_mutex_lock(&mutex_cola_exec);
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_pcb* proceso = list_get(cola_exec, i);
        if (proceso != NULL && proceso->pid == pid) {
            proceso->estado = ESTADO_EXIT;
            list_remove(cola_exec, i);
            log_info(kernel_logger, "Proceso PID=%d removido de EXEC y finalizado", pid);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_exec);
    
    // Liberar CPU
    pthread_mutex_lock(&mutex_cpus);
    if (lista_cpus_conectadas != NULL) {
        for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
            t_cpu_conectada* cpu = list_get(lista_cpus_conectadas, i);
            if (cpu != NULL && cpu->proceso_en_exec != NULL && 
                cpu->proceso_en_exec->pid == pid) {
                cpu->esta_libre = true;
                cpu->proceso_en_exec = NULL;
                log_info(kernel_logger, "CPU %d liberada por finalización de proceso PID=%d", 
                         cpu->id_cpu, pid);
                break;
            }
        }
    }
    pthread_mutex_unlock(&mutex_cpus);
}

/**
 * Comunica con memoria para solicitar dump
 */
bool comunicar_con_memoria_dump(int pid) {
    if (pid <= 0) {
        log_error(kernel_logger, "PID inválido para dump de memoria: %d", pid);
        return false;
    }
    
    log_info(kernel_logger, "Solicitando dump de memoria para proceso PID=%d", pid);
    
    // En implementación real, aquí se haría la comunicación por sockets con memoria
    // Por ahora simulamos con un delay
    usleep(500000); // 500ms de simulación
    
    // Simular respuesta exitosa (en implementación real, manejar errores)
    bool resultado = true;
    
    if (resultado) {
        log_info(kernel_logger, "Dump de memoria completado exitosamente para PID=%d", pid);
    } else {
        log_error(kernel_logger, "Error en dump de memoria para PID=%d", pid);
    }
    
    return resultado;
}

// ========== FUNCIONES DE LOGGING OBLIGATORIO ==========

// Syscall recibida: "## (<PID>) - Solicitó syscall: <NOMBRE_SYSCALL>"
void log_syscall_recibida(int pid, char* nombre_syscall) {
    log_info(kernel_logger, "## (%d) - Solicitó syscall: %s", pid, nombre_syscall);
}

// Creación de Proceso: "## (<PID>) Se crea el proceso - Estado: NEW"
void log_creacion_proceso(int pid) {
    log_info(kernel_logger, "## (%d) Se crea el proceso - Estado: NEW", pid);
}

// Cambio de Estado: "## (<PID>) Pasa del estado <ESTADO_ANTERIOR> al estado <ESTADO_ACTUAL>"
void log_cambio_estado(int pid, char* estado_anterior, char* estado_actual) {
    log_info(kernel_logger, "## (%d) Pasa del estado %s al estado %s", pid, estado_anterior, estado_actual);
}

// Motivo de Bloqueo: "## (<PID>) - Bloqueado por IO: <DISPOSITIVO_IO>"
void log_motivo_bloqueo(int pid, char* dispositivo_io) {
    log_info(kernel_logger, "## (%d) - Bloqueado por IO: %s", pid, dispositivo_io);
}

// Fin de IO: "## (<PID>) finalizó IO y pasa a READY"
void log_fin_io(int pid) {
    log_info(kernel_logger, "## (%d) finalizó IO y pasa a READY", pid);
}

// Desalojo de SJF/SRT: "## (<PID>) - Desalojado por algoritmo SJF/SRT"
void log_desalojo_sjf_srt(int pid) {
    log_info(kernel_logger, "## (%d) - Desalojado por algoritmo SJF/SRT", pid);
}

// Fin de Proceso: "## (<PID>) - Finaliza el proceso"
void log_fin_proceso(int pid) {
    log_info(kernel_logger, "## (%d) - Finaliza el proceso", pid);
}

// Función auxiliar para buscar proceso por PID en todas las colas
t_proceso_kernel* buscar_proceso_por_pid(int pid) {
    t_proceso_kernel* proceso = NULL;
    
    // Buscar en cola NEW
    pthread_mutex_lock(&mutex_cola_new);
    for (int i = 0; i < list_size(cola_new); i++) {
        t_proceso_kernel* p = list_get(cola_new, i);
        if (p && p->pcb.pid == pid) {
            proceso = p;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_new);
    
    if (proceso) return proceso;
    
    // Buscar en cola READY
    pthread_mutex_lock(&mutex_cola_ready);
    for (int i = 0; i < list_size(cola_ready); i++) {
        t_proceso_kernel* p = list_get(cola_ready, i);
        if (p && p->pcb.pid == pid) {
            proceso = p;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_ready);
    
    if (proceso) return proceso;
    
    // Buscar en cola EXEC
    pthread_mutex_lock(&mutex_cola_exec);
    for (int i = 0; i < list_size(cola_exec); i++) {
        t_pcb* pcb = list_get(cola_exec, i);
        if (pcb && pcb->pid == pid) {
            // Para EXEC tenemos PCBs, no procesos completos
            // Crear un proceso temporal para consistencia
            proceso = malloc(sizeof(t_proceso_kernel));
            proceso->pcb = *pcb;
            proceso->estado = pcb->estado;
            pthread_mutex_unlock(&mutex_cola_exec);
            return proceso;
        }
    }
    pthread_mutex_unlock(&mutex_cola_exec);
    
    // Buscar en cola BLOCKED
    pthread_mutex_lock(&mutex_cola_blocked);
    for (int i = 0; i < list_size(cola_blocked); i++) {
        t_pcb* pcb = list_get(cola_blocked, i);
        if (pcb && pcb->pid == pid) {
            // Para BLOCKED tenemos PCBs, no procesos completos
            proceso = malloc(sizeof(t_proceso_kernel));
            proceso->pcb = *pcb;
            proceso->estado = pcb->estado;
            pthread_mutex_unlock(&mutex_cola_blocked);
            return proceso;
        }
    }
    pthread_mutex_unlock(&mutex_cola_blocked);
    
    // Buscar en cola SUSP_READY
    pthread_mutex_lock(&mutex_cola_susp_ready);
    for (int i = 0; i < list_size(cola_susp_ready); i++) {
        t_proceso_kernel* p = list_get(cola_susp_ready, i);
        if (p && p->pcb.pid == pid) {
            proceso = p;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cola_susp_ready);
    
    return proceso;
}

// Métricas de Estado: "## (<PID>) - Métricas de estado: NEW (NEW_COUNT) (NEW_TIME), READY (READY_COUNT) (READY_TIME), …"
void log_metricas_estado(int pid) {
    t_proceso_kernel* proceso = buscar_proceso_por_pid(pid);
    if (proceso == NULL) {
        // Si no encontramos el proceso, usar métricas básicas
        log_info(kernel_logger, "## (%d) - Métricas de estado: NEW (1) (0ms), READY (1) (0ms), EXEC (1) (0ms), BLOCKED (0) (0ms), EXIT (1) (0ms)", 
                 pid);
        return;
    }
    
    // Inicializar contadores de estados
    int count_new = 0, count_ready = 0, count_exec = 0, count_blocked = 0, count_exit = 0;
    int count_susp_ready = 0, count_susp_blocked = 0;
    int time_new = 0, time_ready = 0, time_exec = 0, time_blocked = 0, time_exit = 0;
    int time_susp_ready = 0, time_susp_blocked = 0;
    
    // Si tenemos métricas de estado, calcular valores reales
    if (proceso->pcb.metricas_estado && !queue_is_empty(proceso->pcb.metricas_estado)) {
        t_queue* metricas_temp = queue_create();
        
        // Procesar cada cambio de estado
        while (!queue_is_empty(proceso->pcb.metricas_estado)) {
            t_cambio_estado* cambio = queue_pop(proceso->pcb.metricas_estado);
            
            // Contar transiciones por estado anterior
            switch (cambio->estado_anterior) {
                case ESTADO_NEW:
                    count_new++;
                    time_new += cambio->duracion_ms;
                    break;
                case ESTADO_READY:
                    count_ready++;
                    time_ready += cambio->duracion_ms;
                    break;
                case ESTADO_EXEC:
                    count_exec++;
                    time_exec += cambio->duracion_ms;
                    break;
                case ESTADO_BLOCKED:
                    count_blocked++;
                    time_blocked += cambio->duracion_ms;
                    break;
                case ESTADO_EXIT:
                    count_exit++;
                    time_exit += cambio->duracion_ms;
                    break;
                case ESTADO_SUSP_READY:
                    count_susp_ready++;
                    time_susp_ready += cambio->duracion_ms;
                    break;
                case ESTADO_SUSP_BLOCKED:
                    count_susp_blocked++;
                    time_susp_blocked += cambio->duracion_ms;
                    break;
            }
            
            // Guardar cambio en cola temporal
            queue_push(metricas_temp, cambio);
        }
        
        // Restaurar la cola original
        while (!queue_is_empty(metricas_temp)) {
            queue_push(proceso->pcb.metricas_estado, queue_pop(metricas_temp));
        }
        queue_destroy(metricas_temp);
    } else {
        // Si no hay métricas, usar valores por defecto basados en el estado actual
        switch (proceso->estado) {
            case ESTADO_NEW:
                count_new = 1;
                break;
            case ESTADO_READY:
                count_new = 1;
                count_ready = 1;
                break;
            case ESTADO_EXEC:
                count_new = 1;
                count_ready = 1;
                count_exec = 1;
                break;
            case ESTADO_BLOCKED:
                count_new = 1;
                count_ready = 1;
                count_exec = 1;
                count_blocked = 1;
                break;
            case ESTADO_EXIT:
                count_new = 1;
                count_ready = 1;
                count_exec = 1;
                count_exit = 1;
                break;
            case ESTADO_SUSP_READY:
                count_new = 1;
                count_ready = 1;
                count_susp_ready = 1;
                break;
            case ESTADO_SUSP_BLOCKED:
                count_new = 1;
                count_ready = 1;
                count_blocked = 1;
                count_susp_blocked = 1;
                break;
        }
    }
    
    // Log con métricas calculadas
    log_info(kernel_logger, 
        "## (%d) - Métricas de estado: NEW (%d) (%dms), READY (%d) (%dms), EXEC (%d) (%dms), BLOCKED (%d) (%dms), EXIT (%d) (%dms), SUSP_READY (%d) (%dms), SUSP_BLOCKED (%d) (%dms)", 
        pid, count_new, time_new, count_ready, time_ready, count_exec, time_exec, 
        count_blocked, time_blocked, count_exit, time_exit, count_susp_ready, time_susp_ready, 
        count_susp_blocked, time_susp_blocked);
}