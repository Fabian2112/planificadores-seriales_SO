#include "../include/pcb.h"
#include <stdlib.h>
#include <string.h>
#include <commons/string.h>



char* estado_str(estado_proceso_t estado) {
    switch (estado) {
        case ESTADO_NEW: return "NEW";
        case ESTADO_READY: return "READY";
        case ESTADO_EXEC: return "EXEC";
        case ESTADO_BLOCKED: return "BLOCKED";
        case ESTADO_EXIT: return "EXIT";
        case ESTADO_SUSP_READY: return "SUSP_READY";
        case ESTADO_SUSP_BLOCKED: return "SUSP_BLOCKED";
        default: return "DESCONOCIDO";
    }
}