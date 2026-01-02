#ifndef READ_H
#define READ_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "../IR/ir.h"

IRList ir_parse_file(const char* filename);
void const_propagation_local(IRList* list);
void local_dce(IRList* list);


#ifdef __cplusplus
}
#endif

#endif