#include "temp.h"
#include "ir.h"

static int temp_cnt = 0;
static int label_cnt = 0;
static int var_cnt = 0;

void temp_init(void) {
    temp_cnt = label_cnt = var_cnt = 0;
}

Operand new_temp(void)   { return op_temp(++temp_cnt); }
Operand new_label(void)  { return op_label(++label_cnt); }
Operand new_variable(void) { return op_variable(++var_cnt); }
