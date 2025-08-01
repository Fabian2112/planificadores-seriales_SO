#include "io.h"

int main() {
    // Inicialización
    inicializar_io();
    
    conectar_con_kernel();
    
    // Esperar conexión del kernel
    log_info(io_logger, "Dispositivo I/O '%s' esperando conexión en puerto %d", NOMBRE_IO, PUERTO_KERNEL);
    
    log_info(io_logger, "Kernel conectado");
    
    log_info(io_logger, "=== DISPOSITIVO I/O OPERATIVO ===");
    
    // Atender operaciones (bloqueante, espera solicitudes del Kernel)
    atender_operaciones_io();
    
    // Finalización
    log_info(io_logger, "Cerrando conexiones...");

    finalizar_io();
    
    return EXIT_SUCCESS;
}

