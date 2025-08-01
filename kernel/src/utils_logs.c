#include "../include/utils_logs.h"

t_log* kernel_logger = NULL;

/*void inicializar_logs() {
    kernel_logger = log_create("kernel.log", "KERNEL", true, LOG_LEVEL_INFO);
}*/

void destruir_logs() {
    if (kernel_logger != NULL) {
        log_destroy(kernel_logger);
    }
}