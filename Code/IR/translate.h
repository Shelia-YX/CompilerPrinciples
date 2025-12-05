#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "ir.h"
#include "../node.h"
extern int has_fatal_error;

IRList translate_Program(Node* root);
IRList translate_Exp(Node* n, Operand place);

/* 在文件顶部添加这些辅助函数声明 */
static Type get_specifier_type(Node* spec_node);
static int get_type_size(Type type);
static int get_struct_field_offset(Type struct_type, const char* field_name);
static void set_current_type(Type type);
static Type get_current_type(void);

static void set_current_type(Type type);

static Type get_current_type(void);

#endif
