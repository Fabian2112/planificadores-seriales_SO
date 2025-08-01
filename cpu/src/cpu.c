
#include "cpu.h"
#include "mmu.h"
#include <cliente.h>
#include <server.h>
#include <conexiones.h>// Para PCB_A_EJECUTAR

char instruccion_syscall[256];
t_instruccion_pendiente instruccion_pendiente;
extern bool syscall_en_proceso;  // Declaración externa, definida en utils/src/conexion.c

t_log* cpu_logger = NULL;
t_config* cpu_config = NULL;

// Strings
char* IP_MEMORIA = NULL;
char* IP_KERNEL = NULL;
char* REEMPLAZO_TLB = NULL;
char* REEMPLAZO_CACHE = NULL;
char* LOG_LEVEL = NULL;

// Enteros
int PUERTO_MEMORIA = 0;
int PUERTO_KERNEL_DISPATCH = 0;
int PUERTO_KERNEL_INTERRUPT = 0;
int ENTRADAS_TLB = 0;
int ENTRADAS_CACHE = 0;
int RETARDO_CACHE = 0;

int fd_kernel_dispatch = -1;
int fd_kernel_interrupt = -1;
int fd_memoria = -1;

t_pcb pcb_en_ejecucion = {0};


void inicializar_cpu(){
    inicializar_logs();
    inicializar_configs();
    imprimir_configs();
    inicializar_sincronizacion();
    inicializar_semaforos();

}

void inicializar_logs(){

    cpu_logger = log_create("../cpu.log", "CPU_LOG", 1, LOG_LEVEL_INFO);
    if(cpu_logger == NULL) {    
        perror("Algo salio mal con el cpu_log, no se pudo crear o escuchar el archivo");
        exit(EXIT_FAILURE);
    }

    iniciar_logger(cpu_logger);

}

t_config* inicializar_configs(void){

    cpu_config = config_create("../cpu.config");
   
   if(cpu_config == NULL) {    
       perror("Error al cargar cpu_config");
       exit(EXIT_FAILURE);
   }

    IP_MEMORIA = config_get_string_value(cpu_config, "IP_MEMORIA");
    PUERTO_MEMORIA = config_get_int_value(cpu_config, "PUERTO_MEMORIA");
    IP_KERNEL = config_get_string_value(cpu_config, "IP_KERNEL");
    PUERTO_KERNEL_DISPATCH = config_get_int_value(cpu_config, "PUERTO_KERNEL_DISPATCH");
    PUERTO_KERNEL_INTERRUPT = config_get_int_value(cpu_config, "PUERTO_KERNEL_INTERRUPT");
    ENTRADAS_TLB = config_get_int_value(cpu_config, "ENTRADAS_TLB");
    REEMPLAZO_TLB = config_get_string_value(cpu_config, "REEMPLAZO_TLB");
    ENTRADAS_CACHE = config_get_int_value(cpu_config, "ENTRADAS_CACHE");
    REEMPLAZO_CACHE = config_get_string_value(cpu_config, "REEMPLAZO_CACHE");
    RETARDO_CACHE = config_get_int_value(cpu_config, "RETARDO_CACHE");
    LOG_LEVEL = config_get_string_value(cpu_config, "LOG_LEVEL");

    return cpu_config;
}

void imprimir_configs(){
    log_info(cpu_logger, "IP_MEMORIA: %s", IP_MEMORIA);
    log_info(cpu_logger, "PUERTO_MEMORIA: %d", PUERTO_MEMORIA);
    log_info(cpu_logger, "IP_KERNEL: %s", IP_KERNEL);
    log_info(cpu_logger, "PUERTO_KERNEL_DISPATCH: %d", PUERTO_KERNEL_DISPATCH);
    log_info(cpu_logger, "PUERTO_KERNEL_INTERRUPT: %d", PUERTO_KERNEL_INTERRUPT);
    log_info(cpu_logger, "ENTRADAS_TLB: %d", ENTRADAS_TLB);
    log_info(cpu_logger, "REEMPLAZO_TLB: %s", REEMPLAZO_TLB);
    log_info(cpu_logger, "ENTRADAS_CACHE: %d", ENTRADAS_CACHE);
    log_info(cpu_logger, "REEMPLAZO_CACHE: %s", REEMPLAZO_CACHE);
    log_info(cpu_logger, "RETARDO_CACHE: %d", RETARDO_CACHE);
    log_info(cpu_logger, "LOG_LEVEL: %s", LOG_LEVEL);

}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s [identificador]\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    char* identificador_cpu = argv[1];

    // Configurar path de log con identificador
    char log_file[128];
    snprintf(log_file, sizeof(log_file), "/home/utnso/tp-2025-1c-Planificadores-Seriales/cpu/cpu_%s.log", identificador_cpu);
    
    // Inicialización básica de CPU
    inicializar_cpu();

    log_info(cpu_logger, "CPU inicializado y creando hilos...");

    pthread_t hilo_kernel_dispatch;
    pthread_t hilo_kernel_interrupt;
    pthread_t hilo_cpu_memoria;

    // Conexion kernel dispatch
    log_info(cpu_logger, "Iniciando conexión con Kernel Dispatch...");
    if (pthread_create(&hilo_kernel_dispatch, NULL, conectar_kernel_dispatch, &fd_kernel_dispatch) != 0) {
        log_error(cpu_logger, "Error al crear hilo de conexión con kernel_dispatch");
        exit(EXIT_FAILURE);
    }

    //Conexion kernel interrupt
    log_info(cpu_logger, "Iniciando conexión con Kernel Interrupt...");
    if (pthread_create(&hilo_kernel_interrupt, NULL, conectar_kernel_interrupt, &fd_kernel_interrupt) != 0) {
        log_error(cpu_logger, "Error al crear hilo de conexión con kernel_interrupt");
        exit(EXIT_FAILURE);
    }

    // Esperar a que se establezca la conexión con kernel
    log_info(cpu_logger, "Esperando confirmación de conexión con kernel...");
    sem_wait(&sem_cpu_kernel_hs);
    log_info(cpu_logger, "Handshake con kernel completado");

    // Atender memoria
    if (pthread_create(&hilo_cpu_memoria, NULL, conectar_cpu_memoria, &fd_memoria) != 0) {
        log_error(cpu_logger, "Error al crear hilo con Memoria");
        exit(EXIT_FAILURE);
    } else {
        log_info(cpu_logger, "Creado hilo con Memoria");
    }

    // Esperar a que se establezca la conexión con memoria
    log_info(cpu_logger, "Esperando handshake de memoria...");
    sem_wait(&sem_memoria_cpu_hs);
    log_info(cpu_logger, "Handshake con memoria completado");


    // Enviar identificador al Kernel
    /*log_info(cpu_logger, "Enviando identificador '%s' al Kernel", identificador_cpu);
    t_paquete* paquete_identificador = crear_paquete(CPU, cpu_logger);
    agregar_a_paquete(paquete_identificador, identificador_cpu, strlen(identificador_cpu) + 1);
    enviar_paquete(paquete_identificador, fd_kernel_dispatch);
    enviar_paquete(paquete_identificador, fd_kernel_interrupt);
    eliminar_paquete(paquete_identificador);*/

    // Inicializar caché
    log_info(cpu_logger, "Inicializando caché con %d entradas y algoritmo %s", ENTRADAS_CACHE, REEMPLAZO_CACHE);
    inicializar_cache(ENTRADAS_CACHE, REEMPLAZO_CACHE);
    log_info(cpu_logger, "Caché inicializada exitosamente");

    // Iniciar MMU
    log_info(cpu_logger, "Inicializando MMU");
    iniciar_MMU();
    log_info(cpu_logger, "MMU inicializada exitosamente");

    // Iniciar ciclo principal de CPU
    log_info(cpu_logger, "Iniciando ciclo principal de CPU");
    iniciar_cpu(fd_memoria, fd_kernel_dispatch, fd_kernel_interrupt);


    pthread_join(hilo_kernel_dispatch,NULL);
    pthread_join(hilo_kernel_interrupt,NULL);
    pthread_join(hilo_cpu_memoria, NULL);

    log_debug(cpu_logger,"Se ha desconectado de cpu");

    printf("\nCPU DESCONECTADO!\n\n");

    finalizar_MMU();  // Del código cpu
    return EXIT_SUCCESS;
}

void iniciar_cpu(int conexion_memoria, int conexion_dispatch, int conexion_interrupt) {
    log_info(cpu_logger, "Iniciando ciclo principal de ejecución de CPU");
    
    // Validar conexiones antes de comenzar
    if (conexion_dispatch <= 0) {
        log_error(cpu_logger, "Conexión dispatch inválida: %d", conexion_dispatch);
        return;
    }
    if (conexion_interrupt <= 0) {
        log_error(cpu_logger, "Conexión interrupt inválida: %d", conexion_interrupt);
        return;
    }
    if (conexion_memoria <= 0) {
        log_error(cpu_logger, "Conexión memoria inválida: %d", conexion_memoria);
        return;
    }
    
    log_info(cpu_logger, "Conexiones validadas - Dispatch: %d, Interrupt: %d, Memoria: %d", 
             conexion_dispatch, conexion_interrupt, conexion_memoria);
    
    while (1) {
        log_info(cpu_logger, "Esperando nuevo proceso del dispatch...");
        
        // 1. Esperar PCB del kernel (dispatch)
        t_pcb pcb = {0}; // Inicializar en ceros
        
        if (!recibir_pcb(conexion_dispatch, &pcb)) {
            log_error(cpu_logger, "Error al recibir PCB del dispatch");
            sleep(1); // Pausa antes de reintentar
            continue;
        }
        
        pcb_en_ejecucion = pcb;
        log_info(cpu_logger, "Proceso recibido - PID: %d - PC: %d", pcb.pid, pcb.pc);
        
        // 2. Ciclo de ejecución de instrucciones
        ExecutionResult result;
        int instrucciones_ejecutadas = 0;
        const int MAX_INSTRUCCIONES = 1000; // Límite de seguridad
        
        do {
            if (instrucciones_ejecutadas >= MAX_INSTRUCCIONES) {
                log_warning(cpu_logger, "PID: %d - Límite de instrucciones alcanzado, forzando interrupción", pcb.pid);
                result = INTERRUPT_RECEIVED;
                break;
            }
            
            result = ciclo_instruccion(&pcb, conexion_memoria, conexion_interrupt);
            instrucciones_ejecutadas++;
            
            // Manejar casos especiales (sin salir del ciclo)
            switch(result) {
                case INTERRUPT_RECEIVED:
                    log_info(cpu_logger, "PID: %d - Interrupción recibida", pcb.pid);
                    break;
                case SYSCALL_IO:
                    log_info(cpu_logger, "PID: %d - Syscall IO detectada", pcb.pid);
                    break;
                case CONTINUE_EXECUTION:
                    // Continuar ejecutando instrucciones
                    break;
                default:
                    break;
            }
            
        } while (result == CONTINUE_EXECUTION);
        
        log_info(cpu_logger, "PID: %d - Fin del ciclo de ejecución. Instrucciones ejecutadas: %d, Resultado: %d", 
                 pcb.pid, instrucciones_ejecutadas, result);
        
        // 3. Manejar resultado final de la ejecución
        switch(result) {
            case SYSCALL_INIT_PROC:
                log_info(cpu_logger, "PID: %d - INIT_PROC completado", pcb.pid);
                break;
            case SYSCALL_DUMP_MEMORY:
                log_info(cpu_logger, "PID: %d - DUMP_MEMORY completado", pcb.pid);
                break;
            case PROCESS_EXIT:
                log_info(cpu_logger, "PID: %d - Proceso terminado", pcb.pid);
                limpiar_tlb_proceso(pcb.pid);
                if (cache_habilitada()) {
                    desalojar_proceso_cache(pcb.pid, conexion_memoria);
                }
                break;
            default:
                log_warning(cpu_logger, "Resultado de ejecución no manejado: %d", result);
        }
        
        // 4. Enviar PCB de vuelta al kernel
        log_info(cpu_logger, "PID: %d - Enviando PCB de vuelta al kernel", pcb.pid);
        t_paquete* paquete_pcb = crear_paquete(PCB_DEVUELTO, cpu_logger);
        agregar_a_paquete(paquete_pcb, &pcb.pid, sizeof(int));
        agregar_a_paquete(paquete_pcb, &pcb.pc, sizeof(int));
        agregar_a_paquete(paquete_pcb, &pcb.tiempo_estimado, sizeof(int));
        agregar_a_paquete(paquete_pcb, &pcb.tiempo_inicio_rafaga, sizeof(int));
        agregar_a_paquete(paquete_pcb, &pcb.estado, sizeof(estado_proceso_t));
        
        enviar_paquete(paquete_pcb, conexion_dispatch);
        eliminar_paquete(paquete_pcb);
        
        log_info(cpu_logger, "PID: %d - PCB enviado exitosamente", pcb.pid);
        
        // Limpiar recursos del PCB si es necesario
        if (pcb.metricas_tiempo) {
            queue_destroy(pcb.metricas_tiempo);
        }
        if (pcb.metricas_estado) {
            queue_destroy(pcb.metricas_estado);
        }
    }
}

void* conectar_kernel_dispatch(void* arg) {
    int* fd_kernel_ptr = (int*)arg;
    fd_kernel_dispatch = *fd_kernel_ptr;

    log_info(cpu_logger, "Iniciando conexión con Kernel Dispatch en %s:%d", IP_KERNEL, PUERTO_KERNEL_DISPATCH);

    while((fd_kernel_dispatch)<=0) {
        fd_kernel_dispatch = crear_conexion(cpu_logger, IP_KERNEL, PUERTO_KERNEL_DISPATCH);

        log_info(cpu_logger, "fd_kernel_dispatch: %d", fd_kernel_dispatch);

        if((fd_kernel_dispatch)<=0) 
            log_error(cpu_logger, "Intentando conectar a kernel_Dispatch");
    }
    
    log_info(cpu_logger, "Conectado exitosamente al servidor de kernel_Dispatch en %s:%d", IP_KERNEL, PUERTO_KERNEL_DISPATCH);

    // Señalizar que el handshake con kernel está completo
    sem_post(&sem_cpu_kernel_hs);

    log_info(cpu_logger, "Conexión con Kernel Dispatch establecida, listo para syscalls");

   while (1) {
        // Esperar hasta que haya una instrucción syscall pendiente
        log_info(cpu_logger, "Esperando syscall...");
        sem_wait(&sem_instruccion_lista);
        
        // Hay una syscall para procesar
        pthread_mutex_lock(&mutex_instruccion_pendiente);

        char syscall_a_procesar[256];
        strncpy(syscall_a_procesar, instruccion_pendiente.instruccion, sizeof(syscall_a_procesar));
        
        pthread_mutex_unlock(&mutex_instruccion_pendiente);
        
        log_info(cpu_logger, "Procesando syscall: %s", syscall_a_procesar);
        
        // Verificar conexión
        if(fd_kernel_dispatch <= 0) {
            log_error(cpu_logger, "No hay conexión con Kernel Dispatch");
            
            // Marcar como procesada (con error)
            pthread_mutex_lock(&mutex_instruccion_pendiente);
            instruccion_pendiente.procesada = true;
            instruccion_pendiente.es_syscall = false;
            pthread_mutex_unlock(&mutex_instruccion_pendiente);
            
            sem_post(&sem_instruccion_procesada);
            continue;
        }
        
        // Crear y enviar paquete con la syscall
        t_paquete* paquete = crear_paquete(SYSCALL, cpu_logger);
        agregar_a_paquete(paquete, syscall_a_procesar, strlen(syscall_a_procesar) + 1);
        
        log_info(cpu_logger, "Antes de enviar_paquete");
        enviar_paquete(paquete, fd_kernel_dispatch);
        log_info(cpu_logger, "Después de enviar_paquete");
        
        log_info(cpu_logger, "Esperando confirmación del Kernel para syscall: %s", syscall_a_procesar);
        int cod_op = recibir_operacion(cpu_logger, fd_kernel_dispatch);
        log_info(cpu_logger, "Respuesta recibida del Kernel: %d", cod_op);
        
        // Procesar respuesta
        bool syscall_exitosa = false;
        switch(cod_op) {
            case SYSCALL_OK:
                log_info(cpu_logger, "Syscall procesada exitosamente por el Kernel");
                printf("CPU > Syscall %s completada exitosamente\n", syscall_a_procesar);
                syscall_exitosa = true;
                break;
                
            case SYSCALL_ERROR:
                log_warning(cpu_logger, "Error al procesar syscall en el Kernel");
                printf("CPU > Error al procesar syscall %s\n", syscall_a_procesar);
                break;
                
            case -1:
                log_error(cpu_logger, "Conexión con Kernel perdida durante syscall");
                printf("CPU > Conexión con Kernel perdida\n");
                // Intentar reconectar o manejar error
                break;
                
            default:
                log_warning(cpu_logger, "Respuesta inesperada del Kernel: %d", cod_op);
                printf("CPU > Respuesta inesperada del Kernel: %d\n", cod_op);
                break;
        }
        
        // Marcar syscall como procesada y señalizar
        pthread_mutex_lock(&mutex_instruccion_pendiente);
        instruccion_pendiente.procesada = true;
        instruccion_pendiente.es_syscall = false; // Reset para la próxima
        pthread_mutex_unlock(&mutex_instruccion_pendiente);
        
        sem_post(&sem_instruccion_procesada);
        
        log_info(cpu_logger, "Syscall %s completamente procesada, resultado: %s", 
        syscall_a_procesar, syscall_exitosa ? "EXITOSA" : "ERROR");
    }
    

    close(fd_kernel_dispatch);
    log_info(cpu_logger, "Desconectado de Kernel Dispatch");
    return NULL;
}

void* conectar_kernel_interrupt(void* arg) {
    int* fd_kernel_ptr = (int*)arg;
    fd_kernel_interrupt = *fd_kernel_ptr;

    log_info(cpu_logger, "Iniciando conexión con Kernel Interrupt en %s:%d", IP_KERNEL, PUERTO_KERNEL_INTERRUPT);

    while((fd_kernel_interrupt) <= 0) {
        fd_kernel_interrupt = crear_conexion(cpu_logger, IP_KERNEL, PUERTO_KERNEL_INTERRUPT);
        log_info(cpu_logger, "fd_kernel_interrupt: %d", fd_kernel_interrupt);

        if((fd_kernel_interrupt) <= 0) 
            log_error(cpu_logger, "Intentando conectar a kernel_Interrupt");
    }
    
    log_info(cpu_logger, "Conectado exitosamente al servidor de kernel_Interrupt en %s:%d", IP_KERNEL, PUERTO_KERNEL_INTERRUPT);

    // Señalizar que el handshake con kernel está completo
    sem_post(&sem_cpu_kernel_hs);

    // Este hilo solo escucha interrupciones, no necesita procesamiento adicional
    while(1) {
        sleep(1); // Simplemente mantener la conexión abierta
    }

    close(fd_kernel_interrupt);
    log_info(cpu_logger, "Desconectado de Kernel Interrupt");
    return NULL;
}

void* conectar_cpu_memoria(void* arg) {
    int* fd_memoria_ptr = (int*)arg;
    fd_memoria = *fd_memoria_ptr;

    fd_memoria = crear_conexion(cpu_logger, IP_MEMORIA, PUERTO_MEMORIA);  // Asumimos estas constantes cargadas
    if (fd_memoria == -1) {
        log_error(cpu_logger, "No se pudo conectar a Memoria");
        exit(EXIT_FAILURE);
    }

    log_info(cpu_logger, "Iniciando conexión a Memoria...");

    // Enviar handshake - código de identificación
    int identificador = CPU;
    send(fd_memoria, &identificador, sizeof(int), 0);
    log_info(cpu_logger, "Handshake enviado a Memoria");

    // Esperar confirmación de memoria
    bool confirmacion;
    if(recv(fd_memoria, &confirmacion, sizeof(bool), MSG_WAITALL) <= 0) {
        log_error(cpu_logger, "Error en handshake con Memoria");
        close(fd_memoria);
        return NULL;
    }
    
    if(!confirmacion) {
        log_error(cpu_logger, "Memoria rechazó la conexión");
        close(fd_memoria);
        return NULL;
    }

    log_info(cpu_logger, "Cliente Memoria: %d conectado", fd_memoria);

    // Señalizar que el handshake con memoria está completo
    sem_post(&sem_memoria_cpu_hs);
    
    log_info(cpu_logger, "Iniciando bucle de solicitud de estado y procesamiento...");

    //Esperar antes de recibir el estado
    sleep(1);

    // Recibir respuesta
    int cod_op = recibir_operacion(cpu_logger, fd_memoria);
    if(cod_op == -1) {
        log_error(cpu_logger, "Conexión con Memoria perdida");
        close(fd_memoria);
        return NULL;
    }

    if(cod_op == ARCHIVO_LISTO) {

        log_info(cpu_logger, "Archivo listo en Memoria, comenzando SOLICITUD_INSTRUCCION");

        // Señalizar que el archivo está listo
        sem_post(&sem_archivo_listo_solicitado);

        // Llamar a la función recibir_instruccion para manejar la recepción de instrucciones
        //recibir_instruccion(fd_memoria);
    } else {
        log_error(cpu_logger, "Se esperaba ARCHIVO_LISTO pero se recibió: %d", cod_op);
        close(fd_memoria);
    }
            
    // Solo cerrar si no se procesaron instrucciones correctamente
    //log_info(cpu_logger, "Desconectado de Memoria");
    //return NULL;
}


//Función para recibir instrucciones
void recibir_instruccion(int fd_memoria) {

    if(fd_memoria <= 0) {
        log_error(cpu_logger, "Descriptor de archivo inválido");
        return;
    }

    log_info(cpu_logger, "Iniciando recepción de instrucciones desde Memoria");
    bool continuar = true;

    // Esperar a que el archivo esté listo
    sem_wait(&sem_archivo_listo_solicitado);

    while(continuar) {
        
        // Verificar si hay syscall en proceso
        if(syscall_en_proceso) {
            log_info(cpu_logger, "Syscall en proceso, esperando...");
            sleep(1);
            continue;
        }

        // Solicitar instrucción de forma thread-safe
        pthread_mutex_lock(&mutex_comunicacion_memoria);
        
        log_info(cpu_logger, "Enviando SOLICITUD_INSTRUCCION (cod_op: %d)", SOLICITUD_INSTRUCCION);

        t_paquete* paquete = crear_paquete(SOLICITUD_INSTRUCCION, cpu_logger);
        enviar_paquete(paquete, fd_memoria);
        eliminar_paquete(paquete);

        log_info(cpu_logger, "SOLICITUD_INSTRUCCION enviada");
        
        sleep(1); // Simular retardo de memoria

        // Esperar respuesta de Memoria directamente
        int cod_op = recibir_operacion(cpu_logger, fd_memoria);
        log_info(cpu_logger, "Recibido cod_op: %d", cod_op);

        pthread_mutex_unlock(&mutex_comunicacion_memoria);

        if(cod_op <= 0) {
        log_warning(cpu_logger, "Error en código de operación recibido: %d", cod_op);
        if(cod_op == 0) {
            log_info(cpu_logger, "Reintentando solicitud de instrucción...");
            continue;
        } else {
        break;
        }
        }

        switch(cod_op) {
            case PAQUETE: {
                log_info(cpu_logger, "Recibiendo paquete de instrucción");
                
                t_list* lista = recibir_paquete_desde_buffer(cpu_logger, fd_memoria);
                
                if (lista == NULL || list_size(lista) == 0) {
                    log_error(cpu_logger, "Lista vacía recibida");
                    if (lista) list_destroy(lista);
                    continue;
                }
                
                char* instruccion = list_get(lista, 0);
                
                if (instruccion == NULL || strlen(instruccion) == 0) {
                    log_error(cpu_logger, "Instrucción vacía");
                    list_destroy_and_destroy_elements(lista, free);
                    continue;
                }
                
                log_info(cpu_logger, "Ejecutando: %s", instruccion);
                
                // Procesar instrucción
                procesar_instruccion(instruccion);
                
                // Limpiar recursos
                list_destroy_and_destroy_elements(lista, free);
                
                log_info(cpu_logger, "Instrucción procesada");
                break;
            }
            
            case EXIT:
                log_info(cpu_logger, "Instrucción recibida: EXIT");
                printf("CPU > Fin de ejecución\n");
                continuar = false;
                break;

            default:
                log_warning(cpu_logger, "Código de operación no reconocido: %d", cod_op);
            break;
        }

        // Pequeña pausa para no saturar el sistema
        sleep(1);
    }
    // Cerrar la conexión cuando terminamos
    close(fd_memoria);
    log_info(cpu_logger, "Finalizada recepción de instrucciones desde Memoria");
}

void procesar_instruccion(char* instruccion) {
    log_info(cpu_logger, "Procesando instrucción: %s", instruccion);
    // Verificar si es una syscall
    if(es_syscall(instruccion)) {
        syscall_en_proceso = true;
        log_info(cpu_logger, "Detectada syscall, enviando al hilo de kernel dispatch");
        
        // Enviar la syscall al hilo de kernel dispatch
        pthread_mutex_lock(&mutex_instruccion_pendiente);
        
        // Preparar la instrucción para el hilo de kernel
        strncpy(instruccion_pendiente.instruccion, instruccion, sizeof(instruccion_pendiente.instruccion) - 1);
        instruccion_pendiente.instruccion[sizeof(instruccion_pendiente.instruccion) - 1] = '\0';
        instruccion_pendiente.es_syscall = true;
        instruccion_pendiente.procesada = false;
        
        // Señalar que hay una instrucción lista
        sem_post(&sem_instruccion_lista);
        
        pthread_mutex_unlock(&mutex_instruccion_pendiente);
        
        // Esperar a que sea procesada
        sem_wait(&sem_instruccion_procesada);
        
        log_info(cpu_logger, "Syscall %s procesada completamente", instruccion);
        syscall_en_proceso = false; // Resetear el estado de syscall
    }
    else {

        // Decodificar la instrucción (del código cpu)
        t_instruccion instruction = decode_instruction(instruccion);

        // Ejecutar la instrucción (simplificado para este ejemplo)
        switch(instruction.tipo) {
            case NOOP:
                printf("CPU > Ejecutando NOOP\n");
                sleep(1);
                break;
            case WRITE:
                /*printf("CPU > Ejecutando WRITE en dirección %d\n", instruction.direccion);
                sleep(1);
                break;*/
                log_info(cpu_logger, "## PID: %d - Ejecutando: WRITE - %d %s", 
                    pcb_en_ejecucion.pid, instruction.direccion, instruction.datos);

            int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
            int nro_pagina = instruction.direccion / tamanio_pagina;
            int offset = instruction.direccion % tamanio_pagina;
            
            if (cache_habilitada()) {
                if (escribir_en_cache(nro_pagina, pcb_en_ejecucion.pid, offset, instruction.datos, strlen(instruction.datos) + 1)) {
                    log_info(cpu_logger, "PID: %d - ESCRIBIR en caché - Página: %d, Offset: %d", 
                            pcb_en_ejecucion.pid, nro_pagina, offset);
                    break;
                }
                
                int* direccion_fisica = obtener_direccion_fisica_con_cache(instruction.direccion, pcb_en_ejecucion.pid, fd_memoria);
                if (!direccion_fisica) {
                    log_error(cpu_logger, "PID: %d - Error en traducción de dirección: %d", 
                             pcb_en_ejecucion.pid, instruction.direccion);
                    return PROCESS_EXIT;
                }
                
                if (escribir_en_cache(nro_pagina, pcb_en_ejecucion.pid, offset, instruction.datos, strlen(instruction.datos) + 1)) {
                    log_info(cpu_logger, "PID: %d - ESCRIBIR en caché (después de cargar) - Página: %d, Offset: %d", 
                            pcb_en_ejecucion.pid, nro_pagina, offset);
                } else {
                    escribir_en_memoria_simple(direccion_fisica, instruction.datos, strlen(instruction.datos) + 1, fd_memoria, pcb_en_ejecucion.pid);
                }
            } else {
                int* direccion_fisica = obtener_direccion_fisica(instruction.direccion, pcb_en_ejecucion.pid, fd_memoria);
                if (!direccion_fisica) {
                    log_error(cpu_logger, "PID: %d - Error en traducción de dirección: %d", 
                             pcb_en_ejecucion.pid, instruction.direccion);
                    return PROCESS_EXIT;
                }
                escribir_en_memoria_simple(direccion_fisica, instruction.datos, strlen(instruction.datos) + 1, fd_memoria, pcb_en_ejecucion.pid);
            }
            break;
        
            case READ:
                printf("CPU > Ejecutando READ en dirección %d\n", instruction.direccion);
                sleep(1);
                break;
            case GOTO:
                printf("CPU > Ejecutando GOTO a posición %d\n", instruction.valor);
                sleep(1);
                break;
            default:
                printf("CPU > Instrucción no reconocida: %s\n", instruccion);
                log_warning(cpu_logger, "Instrucción no reconocida: %s", instruccion);
                break;
        }
        
        free_instruction(&instruction);
    }
    
    log_info(cpu_logger, "Instrucción %s procesada completamente", instruccion);
}

bool es_syscall(char* instruccion) {
    // Verificar si la instrucción es una syscall
    return (strncmp(instruccion, "IO", 2) == 0 ||
            strncmp(instruccion, "INIT_PROC", 9) == 0 ||
            strncmp(instruccion, "DUMP_MEMORY", 11) == 0 ||
            strncmp(instruccion, "EXIT", 4) == 0
    );
}

// Funciones del código cpu
t_instruccion decode_instruction(char* instruction_string) {
    t_instruccion instruction = {0};
    instruction.tipo = INVALID_INSTRUCTION;
    
    char* instruction_copy = strdup(instruction_string);
    char* token = strtok(instruction_copy, " ");
    
    if (!token) {
        free(instruction_copy);
        return instruction;
    }
    
    if (strcmp(token, "NOOP") == 0) {
        instruction.tipo = NOOP;
    }
    else if (strcmp(token, "WRITE") == 0) {
        instruction.tipo = WRITE;
        token = strtok(NULL, " ");
        if (token) instruction.direccion = atoi(token);
        token = strtok(NULL, " ");
        if (token) instruction.datos = strdup(token);
    }
    else if (strcmp(token, "READ") == 0) {
        instruction.tipo = READ;
        token = strtok(NULL, " ");
        if (token) instruction.direccion = atoi(token);
        token = strtok(NULL, " ");
        if (token) instruction.tamanio = atoi(token);
    }
    else if (strcmp(token, "GOTO") == 0) {
        instruction.tipo = GOTO;
        token = strtok(NULL, " ");
        if (token) instruction.valor = atoi(token);
    }
    else if (strcmp(token, "IO") == 0) {
        instruction.tipo = IO;
        token = strtok(NULL, " ");
        if (token) instruction.dispositivo = strdup(token);
        token = strtok(NULL, " ");
        if (token) instruction.tiempo = atoi(token);
    }
    else if (strcmp(token, "INIT_PROC") == 0) {
        instruction.tipo = INIT_PROC;
        token = strtok(NULL, " ");
        if (token) instruction.archivo = strdup(token);
        token = strtok(NULL, " ");
        if (token) instruction.tamanio = atoi(token);
    }
    else if (strcmp(token, "DUMP_MEMORY") == 0) {
        instruction.tipo = DUMP_MEMORY;
    }
    else if (strcmp(token, "EXIT") == 0) {
        instruction.tipo = EXIT;
    }
    
    free(instruction_copy);
    return instruction;
}


char* fetch_instruction(t_pcb* pcb, int conexion_memoria) {
    /*log_info(cpu_logger, "PID: %d - FETCH - Program Counter: %d", pcb->pid, pcb->pc);

        // Crear paquete de solicitud
        t_paquete* paquete_solicitud = crear_paquete(SOLICITUD_INSTRUCCION, cpu_logger);
        agregar_a_paquete(paquete_solicitud, &(pcb->pid), sizeof(pcb->pid));
        agregar_a_paquete(paquete_solicitud, &(pcb->pc), sizeof(pcb->pc));

        log_info(cpu_logger, "== DEBUG - Se creó el paquete");
        // Enviar solicitud a memoria
        enviar_paquete(paquete_solicitud, conexion_memoria);
        log_info(cpu_logger, "== DEBUG - Se envió el paquete");
        eliminar_paquete(paquete_solicitud);

        usleep(1000);
        int cod_op = recibir_operacion(cpu_logger, conexion_memoria);
        log_debug(cpu_logger, "FETCH: cod_op recibido: %d", cod_op);
        if (cod_op == 0) {
            log_warning(cpu_logger, "FETCH: cod_op == 0, reintentando solicitud de instrucción");
            sleep(1);
        }
    

    if (cod_op == PAQUETE) {
        t_list* lista = recibir_paquete_desde_buffer(cpu_logger, conexion_memoria);
        if (!lista) {
            log_error(cpu_logger, "FETCH: lista nula recibida desde memoria");
            return NULL;
        }
        int tam_lista = list_size(lista);
        log_debug(cpu_logger, "FETCH: tamaño de lista recibida: %d", tam_lista);
        if (tam_lista > 0) {
            char* instruccion = list_get(lista, 0);
            if (!instruccion) {
                log_error(cpu_logger, "FETCH: instrucción nula en la lista");
                list_destroy_and_destroy_elements(lista, free);
                return NULL;
            }
            log_info(cpu_logger, "FETCH: instrucción recibida: '%s'", instruccion);
            // Copiar instrucción para liberar lista correctamente
            char* copia = strdup(instruccion);
            list_destroy_and_destroy_elements(lista, free);
            return copia;
        } else {
            log_error(cpu_logger, "FETCH: lista vacía recibida desde memoria");
            list_destroy_and_destroy_elements(lista, free);
            return NULL;
        }
    } else {
        log_error(cpu_logger, "FETCH: código de operación inesperado: %d", cod_op);
        return NULL;
    } */
   if(conexion_memoria <= 0) {
        log_error(cpu_logger, "Descriptor de archivo inválido");
        return;
    }

    log_info(cpu_logger, "Iniciando recepción de instrucciones desde Memoria");
    bool continuar = true;

    // Esperar a que el archivo esté listo
    sem_wait(&sem_archivo_listo_solicitado);
    
    int cod_op;
    if (recv(conexion_memoria, &cod_op, sizeof(int), MSG_WAITALL) <= 0) {
        log_error(cpu_logger, "Error al recibir señal inicial desde Memoria");
        return;
    }

    if (cod_op != ARCHIVO_LISTO) {
        log_error(cpu_logger, "Código recibido inesperado: %d", cod_op);
        return;
    }
    while(continuar) {
        
        // Verificar si hay syscall en proceso
        if(syscall_en_proceso) {
            log_info(cpu_logger, "Syscall en proceso, esperando...");
            sleep(1);
            continue;
        }

        // Solicitar instrucción de forma thread-safe
        pthread_mutex_lock(&mutex_comunicacion_memoria);
        
        log_info(cpu_logger, "Enviando SOLICITUD_INSTRUCCION (cod_op: %d)", SOLICITUD_INSTRUCCION);

        t_paquete* paquete = crear_paquete(SOLICITUD_INSTRUCCION, cpu_logger);
        enviar_paquete(paquete, conexion_memoria);
        eliminar_paquete(paquete);

        log_info(cpu_logger, "SOLICITUD_INSTRUCCION enviada");
        
        sleep(1); // Simular retardo de memoria

        // Esperar respuesta de Memoria directamente
        int cod_op = recibir_operacion(cpu_logger, fd_memoria);
        log_info(cpu_logger, "Recibido cod_op: %d", cod_op);

        pthread_mutex_unlock(&mutex_comunicacion_memoria);

        if(cod_op <= 0) {
        log_warning(cpu_logger, "Error en código de operación recibido: %d", cod_op);
        if(cod_op == 0) {
            log_info(cpu_logger, "Reintentando solicitud de instrucción...");
            continue;
        } else {
        break;
        }
        }

        switch(cod_op) {
            case PAQUETE: {
                log_info(cpu_logger, "Recibiendo paquete de instrucción");
                
                t_list* lista = recibir_paquete_desde_buffer(cpu_logger, conexion_memoria);
                
                if (lista == NULL || list_size(lista) == 0) {
                    log_error(cpu_logger, "Lista vacía recibida");
                    if (lista) list_destroy(lista);
                    continue;
                }
                
                char* instruccion = list_get(lista, 0);
                
                if (instruccion == NULL || strlen(instruccion) == 0) {
                    log_error(cpu_logger, "Instrucción vacía");
                    list_destroy_and_destroy_elements(lista, free);
                    continue;
                }
                
                log_info(cpu_logger, "Ejecutando: %s", instruccion);
                return instruccion;
                // Procesar instrucción
                //procesar_instruccion(instruccion);
                
                // Limpiar recursos
                //list_destroy_and_destroy_elements(lista, free);
                
                //log_info(cpu_logger, "Instrucción procesada");
                //break;
            }
            default:
                log_warning(cpu_logger, "Código de operación no reconocido: %d", cod_op);
                return NULL;
            break;
        }

        // Pequeña pausa para no saturar el sistema
        sleep(1);
    }
    // Cerrar la conexión cuando terminamos
    close(fd_memoria);
    log_info(cpu_logger, "Finalizada recepción de instrucciones desde Memoria");
}


ExecutionResult execute_instruction(t_instruccion* instruction, t_pcb* pcb, int conexion_memoria) {
    switch (instruction->tipo) {
        case NOOP:
            log_info(cpu_logger, "## PID: %d - Ejecutando: NOOP", pcb->pid);
            break;
            
        case WRITE: {
            log_info(cpu_logger, "## PID: %d - Ejecutando: WRITE - %d %s", 
                    pcb->pid, instruction->direccion, instruction->datos);
            
            int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
            int nro_pagina = instruction->direccion / tamanio_pagina;
            int offset = instruction->direccion % tamanio_pagina;
            
            if (cache_habilitada()) {
                if (escribir_en_cache(nro_pagina, pcb->pid, offset, instruction->datos, strlen(instruction->datos) + 1)) {
                    log_info(cpu_logger, "PID: %d - ESCRIBIR en caché - Página: %d, Offset: %d", 
                            pcb->pid, nro_pagina, offset);
                    break;
                }
                
                int* direccion_fisica = obtener_direccion_fisica_con_cache(instruction->direccion, pcb->pid, conexion_memoria);
                if (!direccion_fisica) {
                    log_error(cpu_logger, "PID: %d - Error en traducción de dirección: %d", 
                             pcb->pid, instruction->direccion);
                    return PROCESS_EXIT;
                }
                
                if (escribir_en_cache(nro_pagina, pcb->pid, offset, instruction->datos, strlen(instruction->datos) + 1)) {
                    log_info(cpu_logger, "PID: %d - ESCRIBIR en caché (después de cargar) - Página: %d, Offset: %d", 
                            pcb->pid, nro_pagina, offset);
                } else {
                    escribir_en_memoria_simple(direccion_fisica, instruction->datos, strlen(instruction->datos) + 1, conexion_memoria, pcb->pid);
                }
            } else {
                int* direccion_fisica = obtener_direccion_fisica(instruction->direccion, pcb->pid, conexion_memoria);
                if (!direccion_fisica) {
                    log_error(cpu_logger, "PID: %d - Error en traducción de dirección: %d", 
                             pcb->pid, instruction->direccion);
                    return PROCESS_EXIT;
                }
                escribir_en_memoria_simple(direccion_fisica, instruction->datos, strlen(instruction->datos) + 1, conexion_memoria, pcb->pid);
            }
            break;
        }
        
        case READ: {
            log_info(cpu_logger, "## PID: %d - Ejecutando: READ - %d %d", 
                    pcb->pid, instruction->direccion, instruction->tamanio);
            
            int tamanio_pagina = config_get_int_value(cpu_config, "TAMANIO_PAGINA");
            int nro_pagina = instruction->direccion / tamanio_pagina;
            int offset = instruction->direccion % tamanio_pagina;
            
            if (cache_habilitada()) {
                void* buffer = malloc(instruction->tamanio);
                if (leer_de_cache(nro_pagina, pcb->pid, offset, buffer, instruction->tamanio)) {
                    log_info(cpu_logger, "PID: %d - LEER de caché - Página: %d, Offset: %d, Tamaño: %d", 
                            pcb->pid, nro_pagina, offset, instruction->tamanio);
                    free(buffer);
                    break;
                }
                
                int* direccion_fisica = obtener_direccion_fisica_con_cache(instruction->direccion, pcb->pid, conexion_memoria);
                if (!direccion_fisica) {
                    log_error(cpu_logger, "PID: %d - Error en traducción de dirección: %d", 
                             pcb->pid, instruction->direccion);
                    free(buffer);
                    return PROCESS_EXIT;
                }
                
                if (leer_de_cache(nro_pagina, pcb->pid, offset, buffer, instruction->tamanio)) {
                    log_info(cpu_logger, "PID: %d - LEER de caché (después de cargar) - Página: %d, Offset: %d, Tamaño: %d", 
                            pcb->pid, nro_pagina, offset, instruction->tamanio);
                } else {
                    void* datos_leidos = leer_de_memoria_simple(direccion_fisica, instruction->tamanio, conexion_memoria, pcb->pid);
                    log_info(cpu_logger, "PID: %d - Acción: LEER - Dirección Física: %p - Valor: %s", 
                            pcb->pid, direccion_fisica, (char*)datos_leidos);
                    free(datos_leidos);
                }
                free(buffer);
            } else {
                int* direccion_fisica = obtener_direccion_fisica(instruction->direccion, pcb->pid, conexion_memoria);
                if (!direccion_fisica) {
                    log_error(cpu_logger, "PID: %d - Error en traducción de dirección: %d", 
                             pcb->pid, instruction->direccion);
                    return PROCESS_EXIT;
                }
                
                void* datos_leidos = leer_de_memoria_simple(direccion_fisica, instruction->tamanio, conexion_memoria, pcb->pid);
                log_info(cpu_logger, "PID: %d - Acción: LEER - Dirección Física: %p - Valor: %s", 
                        pcb->pid, direccion_fisica, (char*)datos_leidos);
                free(datos_leidos);
            }
            break;
        }
        
        case GOTO:
            log_info(cpu_logger, "## PID: %d - Ejecutando: GOTO - Valor: %d", pcb->pid, instruction->valor);
            pcb->pc = instruction->valor;
            break;
            
        case IO:
            log_info(cpu_logger, "## PID: %d - Ejecutando: IO - Dispositivo: %s, Tiempo: %d", 
                    pcb->pid, instruction->dispositivo, instruction->tiempo);
            return SYSCALL_IO;
            
        case INIT_PROC:
            log_info(cpu_logger, "## PID: %d - Ejecutando: INIT_PROC - Archivo: %s, Tamaño: %d", 
                    pcb->pid, instruction->archivo, instruction->tamanio);
            return SYSCALL_INIT_PROC;
            
        case DUMP_MEMORY:
            log_info(cpu_logger, "## PID: %d - Ejecutando: DUMP_MEMORY", pcb->pid);
            return SYSCALL_DUMP_MEMORY;
            
        case EXIT:
            log_info(cpu_logger, "## PID: %d - Ejecutando: EXIT", pcb->pid);
            return PROCESS_EXIT;
            
        default:
            log_warning(cpu_logger, "PID: %d - Instrucción no implementada: %d", pcb->pid, instruction->tipo);
            break;
    }
    
    return CONTINUE_EXECUTION;
}


bool check_interrupt(int conexion_interrupt, int pid) {
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(conexion_interrupt, &readfds);
    
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    
    int result = select(conexion_interrupt + 1, &readfds, NULL, NULL, &timeout);
    
    if (result > 0 && FD_ISSET(conexion_interrupt, &readfds)) {
        int interrupted_pid;
        if (recibir_datos(conexion_interrupt, &interrupted_pid, sizeof(interrupted_pid))) {
            if (interrupted_pid == pid) {
                log_info(cpu_logger, "## Llega interrupción al puerto Interrupt");
                return true;
            }
        }
    }
    
    return false;
}

void free_instruction(t_instruccion* instruction) {
    if (instruction->datos) {
        free(instruction->datos);
        instruction->datos = NULL;
    }
    if (instruction->dispositivo) {
        free(instruction->dispositivo);
        instruction->dispositivo = NULL;
    }
    if (instruction->archivo) {
        free(instruction->archivo);
        instruction->archivo = NULL;
    }
}

ExecutionResult ciclo_instruccion(t_pcb* pcb, int conexion_memoria, int conexion_interrupt) {
    // FETCH: Obtener la próxima instrucción
    /*char* instruction_string = fetch_instruction(pcb, conexion_memoria);
    if (!instruction_string) {
        log_error(cpu_logger, "PID: %d - Error en FETCH", pcb->pid);
        return PROCESS_EXIT;
    }
    
    // DECODE: Interpretar la instrucción
    t_instruccion instruction = decode_instruction(instruction_string);
    free(instruction_string);
    
    if (instruction.tipo == INVALID_INSTRUCTION) {
        log_error(cpu_logger, "PID: %d - Instrucción inválida", pcb->pid);
        free_instruction(&instruction);
        return PROCESS_EXIT;
    }
    
    // EXECUTE: Ejecutar la instrucción
    ExecutionResult result = execute_instruction(&instruction, pcb, conexion_memoria);
    
    // Si no fue GOTO, incrementar PC
    if (instruction.tipo != GOTO) {
        pcb->pc++;
    }
    
    free_instruction(&instruction);
    
    // Verificar interrupciones
    if (result == CONTINUE_EXECUTION && check_interrupt(conexion_interrupt, pcb->pid)) {
        return INTERRUPT_RECEIVED;
    }
        return result;
    */
    recibir_instruccion(conexion_memoria);
    return CONTINUE_EXECUTION;
}

bool recibir_pcb(int socket, t_pcb* pcb) {
    log_trace(cpu_logger, "Esperando recibir PCB del socket %d", socket);
    
    int cod_op = recibir_operacion(cpu_logger, socket);
    log_trace(cpu_logger, "Código de operación recibido: %d", cod_op);
    
    if (cod_op == -1) {
        log_trace(cpu_logger, "Conexión cerrada por el servidor");
        return false;
    }
    
    // Verificar que el código de operación sea el esperado para un PCB
    if (cod_op != PCB_A_EJECUTAR) {
        log_error(cpu_logger, "Código de operación inesperado. Esperado: %d (PCB_A_EJECUTAR), Recibido: %d", PCB_A_EJECUTAR, cod_op);
        return false;
    }
    
    log_trace(cpu_logger, "Recibiendo paquete PCB...");
    t_list* lista_datos = recibir_paquete_desde_buffer(cpu_logger, socket);
    
    if (lista_datos == NULL) {
        log_error(cpu_logger, "Error al recibir paquete PCB - lista nula");
        return false;
    }
    
    int elementos_recibidos = list_size(lista_datos);
    log_trace(cpu_logger, "Elementos en el paquete PCB: %d", elementos_recibidos);
    
    if (elementos_recibidos < 5) {
        log_error(cpu_logger, "Paquete PCB incompleto - elementos recibidos: %d, esperados: 5", elementos_recibidos);
        list_destroy_and_destroy_elements(lista_datos, free);
        return false;
    }
    
    // Extraer datos del paquete con validación
    void* pid_data = list_get(lista_datos, 0);
    void* pc_data = list_get(lista_datos, 1);
    void* tiempo_estimado_data = list_get(lista_datos, 2);
    void* tiempo_inicio_rafaga_data = list_get(lista_datos, 3);
    void* estado_data = list_get(lista_datos, 4);
    
    if (!pid_data || !pc_data || !tiempo_estimado_data || !tiempo_inicio_rafaga_data || !estado_data) {
        log_error(cpu_logger, "Datos del PCB corruptos - algún elemento es NULL");
        list_destroy_and_destroy_elements(lista_datos, free);
        return false;
    }
    
    // Copiar datos al PCB
    memcpy(&(pcb->pid), pid_data, sizeof(int));
    memcpy(&(pcb->pc), pc_data, sizeof(int));
    memcpy(&(pcb->tiempo_estimado), tiempo_estimado_data, sizeof(int));
    memcpy(&(pcb->tiempo_inicio_rafaga), tiempo_inicio_rafaga_data, sizeof(int));
    memcpy(&(pcb->estado), estado_data, sizeof(estado_proceso_t));
    
    // Validar datos recibidos
    if (pcb->pid < 0 || pcb->pc < 0) {
        log_error(cpu_logger, "PCB con datos inválidos - PID: %d, PC: %d", pcb->pid, pcb->pc);
        list_destroy_and_destroy_elements(lista_datos, free);
        return false;
    }
    
    // Inicializar las colas de métricas si no existen
    if (pcb->metricas_tiempo == NULL) {
        pcb->metricas_tiempo = queue_create();
    }
    if (pcb->metricas_estado == NULL) {
        pcb->metricas_estado = queue_create();
    }
    
    list_destroy_and_destroy_elements(lista_datos, free);
    
    log_trace(cpu_logger, "PCB recibido correctamente - PID: %d, PC: %d, Estimación: %d, Estado: %d", 
              pcb->pid, pcb->pc, pcb->tiempo_estimado, pcb->estado);
    return true;
}

int escribir_en_memoria_simple(int* direccion_fisica, void* datos, size_t tamanio, int conexion_memoria, int pid) {
    log_info(cpu_logger, "PID: %d - Acceso Espacio Usuario - WRITE - Dir: %p - Tamaño: %zu", 
             pid, direccion_fisica, tamanio);
    
    t_paquete* paquete_escritura = crear_paquete(WRITE, cpu_logger);
    int operacion = 2; // Escritura en espacio de usuario
    agregar_a_paquete(paquete_escritura, &operacion, sizeof(int));
    agregar_a_paquete(paquete_escritura, &pid, sizeof(int));
    int direccion_int = (int)(intptr_t)direccion_fisica;
    agregar_a_paquete(paquete_escritura, &direccion_int, sizeof(int));
    agregar_a_paquete(paquete_escritura, &tamanio, sizeof(size_t));
    agregar_a_paquete(paquete_escritura, datos, tamanio);
    
    enviar_paquete(paquete_escritura, conexion_memoria);
    eliminar_paquete(paquete_escritura);
    
    char respuesta[3];
    if (!recibir_datos(conexion_memoria, respuesta, 2)) {
        log_error(cpu_logger, "Error al recibir confirmación de escritura de memoria");
        return -1;
    }
    respuesta[2] = '\0';
    
    if (strcmp(respuesta, "OK") == 0) {
        log_trace(cpu_logger, "Escritura confirmada por memoria");
        return 0;
    } else {
        log_error(cpu_logger, "Memoria respondió con error: %s", respuesta);
        return -1;
    }
}

void* leer_de_memoria_simple(int* direccion_fisica, size_t tamanio, int conexion_memoria, int pid) {
    (void)pid;

    log_info(cpu_logger, "PID: %d - Acceso Espacio Usuario - READ - Dir: %p - Tamaño: %zu", pid, direccion_fisica, tamanio);
    t_paquete* paquete_lectura = crear_paquete(READ, cpu_logger);
    int operacion = 0; // READ
    agregar_a_paquete(paquete_lectura, &operacion, sizeof(int));
    agregar_a_paquete(paquete_lectura, &direccion_fisica, sizeof(int*));
    agregar_a_paquete(paquete_lectura, &tamanio, sizeof(size_t));
    
    enviar_paquete(paquete_lectura, conexion_memoria);
    eliminar_paquete(paquete_lectura);
    
    void* datos = malloc(tamanio);
    if (!recibir_datos(conexion_memoria, datos, tamanio)) {
        free(datos);
        return NULL;
    }
    
    log_trace(cpu_logger, "Datos leídos correctamente de memoria");
    return datos;
}

bool recibir_datos(int socket, void* buffer, size_t tamanio) {
    size_t bytes_recibidos_total = 0;
    char* buffer_ptr = (char*)buffer;
    
    while (bytes_recibidos_total < tamanio) {
        ssize_t bytes_received = recv(socket, 
                                    buffer_ptr + bytes_recibidos_total, 
                                    tamanio - bytes_recibidos_total, 
                                    0);
        
        if (bytes_received <= 0) {
            log_error(cpu_logger, 
                     "Error al recibir datos - Bytes esperados: %zu, Recibidos: %zu", 
                     tamanio, bytes_recibidos_total);
            return false;
        }
        
        bytes_recibidos_total += bytes_received;
    }
    
    log_trace(cpu_logger, 
             "Datos recibidos correctamente - Bytes: %zu", 
             bytes_recibidos_total);
    return true;
}