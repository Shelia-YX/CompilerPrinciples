#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "./IR/ir.h"

// 用于外部调用
void mips_codegen(FILE* out, IRList ir);

#endif
