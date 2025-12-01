#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "ir.h"
#include "../node.h"

IRList translate_Program(Node* root);
IRList translate_Exp(Node* n, Operand place);

#endif
