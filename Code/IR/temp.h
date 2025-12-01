#ifndef TEMP_H
#define TEMP_H

#include "ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 临时变量和标签计数器 */
void temp_init(void);
Operand new_temp(void);
Operand new_label(void);
Operand new_variable(void);


#ifdef __cplusplus
}
#endif

#endif // TEMP_H