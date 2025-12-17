#include "translate.h"
#include "temp.h"
#include "ir.h"
#include "../node.h"
#include "../semantic/type.h"
#include "../semantic/symbol_table.h"
#include "../semantic/semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int has_fatal_error = 0;

/*=====================*
 *   一些 AST 宏简化   *
 *=====================*/

#define NODE_NAME(n)   ((n)->name)
#define NODE_TEXT(n)   ((n)->value)
#define CHILD(n)       ((n)->child)
#define NEXT(n)        ((n)->nxt)

/*==============================*
 *  简单的 name -> Operand 映射 *
 *==============================*/

typedef struct VarBinding_ {
    char*   name;
    Operand op;
    Type    type;
    Operand addr_op;
    int is_param;
    struct VarBinding_* next;
} VarBinding;

static VarBinding* var_table = NULL;

static void add_var_binding(const char* name, Operand op, Type type, int is_param) {
    VarBinding* p = var_table;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            p->op = op;
            if (p->type) type_free(p->type);
            p->type = type_deepcopy(type);
            p->is_param = is_param;
            return;
        }
        p = p->next;
    }
    
    VarBinding* nb = (VarBinding*)malloc(sizeof(VarBinding));
    nb->name = xstrdup(name);
    nb->op   = op;
    nb->type = type_deepcopy(type);
    nb->addr_op = NULL;
    nb->is_param = is_param;
    
    nb->next = var_table;
    var_table = nb;
    
    const char* type_str = "未知";
    const char* var_type_str = is_param ? "参数" : "局部变量";
    
    if (type) {
        switch (type->kind) {
            case T_BASIC: type_str = "基本类型"; break;
            case T_ARRAY: 
                type_str = "数组"; 
                if (type->u.array.elem && type->u.array.elem->kind == T_STRUCT) {
                    // printf("绑定结构体数组: %s[%d]\n", name, type->u.array.size);
                }
                break;
            case T_STRUCT: 
                type_str = "结构体";
                break;
        }
    }
    
    // printf("添加变量绑定: %s -> v%d (%s, 类型: %s)\n", 
        //    name, op->u.var_no, var_type_str, type_str);
}

static Type get_var_type(const char* name) {
    VarBinding* p = var_table;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            return p->type;
        }
        p = p->next;
    }

    Symbol* sym = symtab_lookup(name);
    if (sym) {
        return sym->type;
    }
    
    return NULL;
}

static Operand get_var_operand(const char* name) {
    VarBinding* p = var_table;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            return p->op;
        }
        p = p->next;
    }

    Symbol* sym = symtab_lookup(name);
    if (sym && sym->kind == SYM_VAR) {
        Operand op = new_variable();
        add_var_binding(name, op, sym->type, 0);
        
        // printf("从符号表找到变量 %s，创建操作数 v%d\n", name, op->u.var_no);
        
        return op;
    }
    return NULL;
}


/*=====================*
 *   函数声明（内部）  *
 *=====================*/

static IRList translate_ExtDefList(Node* n);
static IRList translate_ExtDef(Node* n);
static IRList translate_FunDec(Node* n);
static IRList translate_VarList(Node* n);
static IRList translate_ParamDec(Node* n);

static IRList translate_CompSt(Node* n);
static IRList translate_StmtList(Node* n);
static IRList translate_Stmt(Node* n);

static IRList translate_DefList(Node* n);
static IRList translate_Def(Node* n);
static IRList translate_DecList(Node* n);
static IRList translate_Dec(Node* n);

static IRList translate_Cond(Node* n, Operand label_true, Operand label_false);
static IRList translate_Args(Node* n, Operand* arg_list, int* arg_cnt);

static IRList translate_StructFieldAccess(Node* struct_node, const char* field_name, Operand place);
static IRList translate_StructFieldAssign(Node* struct_node, const char* field_name, Node* value_node);
static IRList handle_struct_variable(const char* name, Type type);
static int get_struct_field_offset(Type struct_type, const char* field_name);

static void debug_print_ast_structure(Node* n, int depth);
static IRList handle_struct_field_array_access(Node* exp, Operand place);
static int is_struct_field_access(Node* exp);
static Node* find_id_parent_node(Node* exp);
static Node* find_id_node(Node* exp);
static VarBinding* find_var_binding(const char* name);
/*=====================*
 *   Type 辅助函数    *
 *=====================*/

static Type current_def_type = NULL;
static void set_current_type(Type type) { current_def_type = type;}
static Type get_current_type(void) { return current_def_type;}

void translate_cleanup(void) {
    VarBinding* p = var_table;
    while (p) {
        VarBinding* next = p->next;
        free(p->name);
        if (p->type) type_free(p->type);
        free(p);
        p = next;
    }
    var_table = NULL;
}

static Type get_specifier_type(Node* spec_node) {
    if (!spec_node) return NULL;
    
    Node* child = CHILD(spec_node);
    if (!child) return NULL;
    
    if (strcmp(child->name, "TYPE") == 0) {
        // 基本类型处理...
        Node* c1 = NEXT(spec_node);

        if(c1 == NULL){
            return NULL;
        } 

        if(strcmp(NODE_NAME(c1), "VarDec") == 0 && CHILD(c1) && strcmp(NODE_NAME(CHILD(c1)), "VarDec") == 0){
            Node* index = NEXT(NEXT(CHILD(c1)));
            return type_make_array(TY_INT,atoi(NODE_TEXT(index)));
        }
        return NULL;
    } 
    else if (strcmp(child->name, "StructSpecifier") == 0) {
        // printf("处理StructSpecifier\n");
        
        // 更可靠地提取结构体名
        const char* struct_name = NULL;
        
        // 检查子节点
        Node* spec_child = CHILD(child);
        while (spec_child) {
            if (strcmp(spec_child->name, "STRUCT") == 0) {
                // 跳过 STRUCT 关键字
            }
            else if (strcmp(spec_child->name, "OptTag") == 0) {
                // OptTag -> ID 或 空
                Node* id_node = CHILD(spec_child);
                if (id_node && strcmp(id_node->name, "ID") == 0) {
                    struct_name = NODE_TEXT(id_node);
                    break;
                }
            }
            else if (strcmp(spec_child->name, "Tag") == 0) {
                // Tag -> ID
                Node* id_node = CHILD(spec_child);
                if (id_node && strcmp(id_node->name, "ID") == 0) {
                    struct_name = NODE_TEXT(id_node);
                    break;
                }
            }
            spec_child = NEXT(spec_child);
        }
        
        if (!struct_name) {
            // printf("警告：匿名结构体\n");
            return type_make_basic(TY_INT); // 返回基本类型作为占位
        }
        
        // printf("查找结构体标签: %s\n", struct_name);
        
        // 从符号表中获取
        Symbol* sym = symtab_lookup(struct_name);
        if (sym && sym->kind == SYM_STRUCT_TAG) {
            // printf("找到结构体标签 %s\n", struct_name);
            return type_deepcopy(sym->type);
        } else {
            // printf("错误：找不到结构体 %s，符号表内容:\n", struct_name);
            symtab_print_all();
            return type_make_basic(TY_INT);
        }
    }
    
    return NULL;
}

static int get_type_size(Type type) {
    if (!type) {
        // printf("get_type_size: type is NULL\n");
        return 0;
    }
    
    // printf("get_type_size: 类型kind = %d\n", type->kind);
    
    switch (type->kind) {
        case T_BASIC:
            return 4;
        case T_ARRAY: {
            int elem_size = get_type_size(type->u.array.elem);
            return type->u.array.size * elem_size;
        }
        case T_STRUCT: {
            int total_size = 0;
            FieldList field = type->u.structure;
            while (field) {
                int field_size = get_type_size(field->type);
                total_size += field_size;
                // printf("get_type_size: 字段 %s 大小 = %d\n", field->name, field_size);
                field = field->tail;
            }
            // printf("get_type_size: 结构体总大小 = %d 字节\n", total_size);
            return total_size;
        }
        default:
            return 0;
    }
}

static int get_struct_field_offset(Type struct_type, const char* field_name) {
    if (!struct_type || struct_type->kind != T_STRUCT || !field_name) {
        return -1;
    }
    
    int offset = 0;
    FieldList field = struct_type->u.structure;
    
    while (field) {
        // printf("%s %s\n",field->name, field_name);
        if (field->name && strcmp(field->name, field_name) == 0) {
            return offset;
        }
        offset += get_type_size(field->type);
        field = field->tail;
    }
    
    return -1; // 找不到字段
}

/*=====================*
 *   调试辅助函数      *
 *=====================*/

// 改进的调试函数
static void print_type_info(Type type, const char* prefix) {
    if (!type) {
    //  printf("%s: NULL\n", prefix);
        return;
    }
    
//  printf("%s: kind=%d", prefix, type->kind);
    
    if (type->kind == T_ARRAY) {
    //  printf(", size=%d\n", type->u.array.size);
        // 递归打印元素类型
        char new_prefix[100];
        snprintf(new_prefix, sizeof(new_prefix), "%s->elem", prefix);
        print_type_info(type->u.array.elem, new_prefix);
    } else if (type->kind == T_BASIC) {
    //  printf(", basic type\n");
    } else if (type->kind == T_STRUCT) {
    //  printf(", struct type\n");
    }
}

// 检查类型是否为多维数组
static int is_multi_dimension_array(Type type) {
    if (!type) return 0;
    
    int dimension = 0;
    Type temp = type;
    
    while (temp && temp->kind == T_ARRAY) {
        dimension++;
        if (dimension > 1) {
            return 1;  // 是多维数组
        }
        temp = temp->u.array.elem;
    }
    
    return 0;  // 不是多维数组
}

 // 获取数组维度
static int get_array_dimension(Type type) {
    if (!type) return 0;
    
    int dimension = 0;
    Type temp = type;
    
    while (temp && temp->kind == T_ARRAY) {
        dimension++;
        temp = temp->u.array.elem;
    }
    
    return dimension;
}

// 调试函数：打印AST结构
static void debug_print_ast_structure(Node* n, int depth) {
    if (!n) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    // printf("%s", NODE_NAME(n));
    if (NODE_TEXT(n)) printf(" (%s)", NODE_TEXT(n));
    // printf("\n");
    
    Node* child = CHILD(n);
    while (child) {
        // debug_print_ast_structure(child, depth + 1);
        child = NEXT(child);
    }
}

/*=====================*
 *   Relop 辅助函数    *
 *=====================*/

static RelopKind relop_from_node(Node* relop_node) {
    const char* t = NODE_TEXT(relop_node);
    
    if (!t) {
        return RELOP_EQ;
    }
    
    if (strcmp(t, "==") == 0) return RELOP_EQ;
    if (strcmp(t, "!=") == 0) return RELOP_NE;
    if (strcmp(t, "<")  == 0) return RELOP_LT;
    if (strcmp(t, ">")  == 0) return RELOP_GT;
    if (strcmp(t, "<=") == 0) return RELOP_LE;
    if (strcmp(t, ">=") == 0) return RELOP_GE;
    
    return RELOP_EQ;
}

/*=====================*
 *   顶层入口函数      *
 *=====================*/

IRList translate_Program(Node* root) {
    IRList list = irlist_create();
    if (!root) return list;

    // 第一步：进行语义分析，建立符号表
    semantic_init();
    semantic_analyze(root);
    
    // 第二步：生成中间代码
    symtab_enter_scope();

    Node* extDefList = CHILD(root);
    IRList sub = translate_ExtDefList(extDefList);
    irlist_concat(&list, sub);

    symtab_leave_scope();

    return list;
}

static IRList translate_ExtDefList(Node* n) {
    IRList list = irlist_create();
    if (!n || !CHILD(n)) return list;

    Node* extDef = CHILD(n);
    Node* rest   = NEXT(extDef);

    IRList c1 = translate_ExtDef(extDef);
    IRList c2 = translate_ExtDefList(rest);
    irlist_concat(&list, c1);
    irlist_concat(&list, c2);
    return list;
}

static IRList translate_ExtDef(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* specifier = CHILD(n);
    Node* second    = NEXT(specifier);
    if (!second) return list;

    if (strcmp(second->name, "FunDec") == 0) {
        Node* funDec = second;
        Node* compSt = NEXT(funDec);

        IRList f = translate_FunDec(funDec);
        IRList b = translate_CompSt(compSt);
        irlist_concat(&list, f);
        irlist_concat(&list, b);
        
        symtab_leave_scope();
    }
    
    return list;
}

static IRList translate_FunDec(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* id  = CHILD(n);
    Node* lp  = NEXT(id);
    Node* nxt = NEXT(lp);

    const char* func_name = NODE_TEXT(id);
    Operand func = op_function((char*)func_name);
    irlist_append(&list, ir_function(func));
    
    // printf("函数定义: %s\n", func_name);

    symtab_enter_scope();

    if (nxt && strcmp(NODE_NAME(nxt), "VarList") == 0) {
        IRList params = translate_VarList(nxt);
        irlist_concat(&list, params);
    }
    
    return list;
}

static IRList translate_VarList(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* paramDec = CHILD(n);
    Node* comma_or_end = NEXT(paramDec);

    IRList p = translate_ParamDec(paramDec);
    irlist_concat(&list, p);

    if (comma_or_end && strcmp(NODE_NAME(comma_or_end), "COMMA") == 0) {
        Node* varList2 = NEXT(comma_or_end);
        IRList more = translate_VarList(varList2);
        irlist_concat(&list, more);
    }
    return list;
}

static IRList translate_ParamDec(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* spec   = CHILD(n);
    Node* varDec = NEXT(spec);
    
    Type type = get_specifier_type(spec);
    
    Node* p = varDec;
    while (p && strcmp(NODE_NAME(p), "ID") != 0) {
        p = CHILD(p);
    }
    if (p && strcmp(NODE_NAME(p), "ID") == 0) {
        const char* name = NODE_TEXT(p);
        Operand v = new_variable();
        
        add_var_binding(name, v, type, 1);
        irlist_append(&list, ir_param(v));

        // 打印参数信息
        if (type && type->kind == T_STRUCT) {
            // printf("结构体参数: %s -> v%d (类型: 结构体)\n", name, v->u.var_no);
        }
        else if(type && type->kind == T_ARRAY) {
            has_fatal_error = 1;
            printf("错误: 函数参数不支持数组类型 (%s)\n", name);
            exit(0);
        }
        else {
            // printf("普通参数: %s -> v%d\n", name, v->u.var_no);
        }
    }
    // if (type) type_free(type);
    return list;
}

/*=====================*
 *   语句块 / 语句翻译  *
 *=====================*/

static IRList translate_CompSt(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* child = CHILD(n);
    
    if (!child || strcmp(child->name, "LC") != 0) {
        return list;
    }
    
    symtab_enter_scope();
    
    Node* second = NEXT(child);
    if (!second) {
        symtab_leave_scope();
        return list;
    }
    
    if (strcmp(second->name, "DefList") == 0) {
        IRList def_ir = translate_DefList(second);
        irlist_concat(&list, def_ir);
        
        Node* third = NEXT(second);
        if (third && strcmp(third->name, "StmtList") == 0) {
            IRList stmt_ir = translate_StmtList(third);
            irlist_concat(&list, stmt_ir);
        }
    } 
    else if (strcmp(second->name, "StmtList") == 0) {
        IRList stmt_ir = translate_StmtList(second);
        irlist_concat(&list, stmt_ir);
    }
    
    symtab_leave_scope();
    
    return list;
}

static IRList translate_StmtList(Node* n) {
    IRList list = irlist_create();
    if (!n || !CHILD(n)) return list;

    Node* stmt = CHILD(n);
    Node* rest = NEXT(stmt);

    IRList s1 = translate_Stmt(stmt);
    IRList s2 = translate_StmtList(rest);
    irlist_concat(&list, s1);
    irlist_concat(&list, s2);
    return list;
}

static IRList translate_Stmt(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* child = CHILD(n);
    if (!child) return list;
    
    if (strcmp(child->name, "CompSt") == 0) {
        IRList b = translate_CompSt(child);
        irlist_concat(&list, b);
    }
    else if (strcmp(child->name, "RETURN") == 0) {
        Node* exp = NEXT(child);
        if (exp) {
            Operand t = new_temp();
            IRList e = translate_Exp(exp, t);
            irlist_concat(&list, e);
            irlist_append(&list, ir_return(t));
        }
    }
    else if (strcmp(child->name, "IF") == 0) {
        Node* lp = NEXT(child);
        Node* exp = NEXT(lp);
        Node* rp = NEXT(exp);
        Node* stmt1 = NEXT(rp);
        Node* else_kw = NEXT(stmt1);
        Node* stmt2 = else_kw ? NEXT(else_kw) : NULL;

        if (exp && stmt1) {
            if (else_kw && stmt2) {
                Operand label_true = new_label();
                Operand label_false = new_label();
                Operand label_end = new_label();

                IRList cond = translate_Cond(exp, label_true, label_false);
                irlist_concat(&list, cond);

                irlist_append(&list, ir_label(label_true));
                IRList then_ir = translate_Stmt(stmt1);
                irlist_concat(&list, then_ir);
                irlist_append(&list, ir_goto(label_end));

                irlist_append(&list, ir_label(label_false));
                IRList else_ir = translate_Stmt(stmt2);
                irlist_concat(&list, else_ir);
                
                irlist_append(&list, ir_label(label_end));
            } else {
                Operand label_true = new_label();
                Operand label_false = new_label();

                IRList cond = translate_Cond(exp, label_true, label_false);
                irlist_concat(&list, cond);

                irlist_append(&list, ir_label(label_true));
                IRList then_ir = translate_Stmt(stmt1);
                irlist_concat(&list, then_ir);
                irlist_append(&list, ir_label(label_false));
            }
        }
    }
    else if (strcmp(child->name, "WHILE") == 0) {
        Node* lp = NEXT(child);
        Node* exp = NEXT(lp);
        Node* rp = NEXT(exp);
        Node* stmt = NEXT(rp);

        if (exp && stmt) {
            Operand label_begin = new_label();
            Operand label_body = new_label();
            Operand label_end = new_label();

            irlist_append(&list, ir_label(label_begin));
        
            Operand cond_result = new_temp();
            IRList cond_ir = translate_Exp(exp, cond_result);
            irlist_concat(&list, cond_ir);
        
            Operand zero = op_constant(0);
            irlist_append(&list, ir_if(cond_result, RELOP_NE, zero, label_body));
            irlist_append(&list, ir_goto(label_end));
        
            irlist_append(&list, ir_label(label_body));
            IRList body_ir = translate_Stmt(stmt);
            irlist_concat(&list, body_ir);
        
            irlist_append(&list, ir_goto(label_begin));
        
            irlist_append(&list, ir_label(label_end));
        }
    }
    else if (strcmp(child->name, "Exp") == 0) {
        IRList e = translate_Exp(child, NULL);
        irlist_concat(&list, e);
    }
    
    return list;
}

/*=====================*
 *   变量定义翻译      *
 *=====================*/

static IRList translate_DefList(Node* n) {
    IRList list = irlist_create();
    if (!n) {
        return list;
    }
    
    if (!n->child) {
        return list;
    }
    
    Node* def = n->child;
    if (!def || strcmp(def->name, "Def") != 0) {
        return list;
    }
    
    IRList def_ir = translate_Def(def);
    irlist_concat(&list, def_ir);
    
    Node* next_def_list = def->nxt;
    if (next_def_list && next_def_list->name && strcmp(next_def_list->name, "DefList") == 0) {
        IRList rest_ir = translate_DefList(next_def_list);
        irlist_concat(&list, rest_ir);
    }
    
    return list;
}

static IRList translate_Def(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* spec = CHILD(n);
    Node* decList = NEXT(spec);
    
    Type type = get_specifier_type(spec);
    // printf("Def节点的类型kind: %d\n", type ? type->kind : -1);
    
    if (type && type->kind == T_STRUCT) {
        // printf("这是一个结构体类型定义\n");
    }
    
    set_current_type(type);

    if (!decList) {
        if (type) type_free(type);
        set_current_type(NULL);
        return list;
    }

    IRList d = translate_DecList(decList);
    irlist_concat(&list, d);
    
    set_current_type(NULL);
    if (type) type_free(type);
    
    return list;
}

static IRList translate_DecList(Node* n) {
    IRList list = irlist_create();
    if (!n) {
        return list;
    }
    
    Node* dec = CHILD(n);
    if (!dec) {
        return list;
    }
    
    Node* comma_or_end = NEXT(dec);

    IRList d1 = translate_Dec(dec);
    irlist_concat(&list, d1);

    if (comma_or_end && comma_or_end->name && 
        strcmp(comma_or_end->name, "COMMA") == 0) {
        Node* decList2 = NEXT(comma_or_end);
        
        if (decList2) {
            IRList d2 = translate_DecList(decList2);
            irlist_concat(&list, d2);
        }
    }
    
    return list;
}

static IRList translate_Dec(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* varDec = CHILD(n);
    Type current_type = get_current_type();
    
    // 先检测是否为数组（在查找ID之前）
    int is_array = 0;
    int array_size = 0;
    const char* array_name = NULL;
    
    // 递归检测VarDec节点是否包含数组
    Node* child = CHILD(varDec);
    while (child) {
        // 检查是否有LB标记数组
        if (strcmp(NODE_NAME(child), "LB") == 0) {
            is_array = 1;
            Node* int_node = NEXT(child);
            if (int_node && strcmp(int_node->name, "INT") == 0) {
                array_size = atoi(NODE_TEXT(int_node));
            }
            break;
        }
        // 如果有ID节点，记录变量名
        if (strcmp(NODE_NAME(child), "ID") == 0 && !array_name) {
            array_name = NODE_TEXT(child);
        }
        child = NEXT(child);
    }
    
    // 如果上面没找到ID，深入查找
    if (!array_name) {
        Node* p = varDec;
        while (p && strcmp(NODE_NAME(p), "ID") != 0) {
            p = CHILD(p);
        }
        if (p) array_name = NODE_TEXT(p);
    }
    
    if (!array_name) return list;
    
    const char* name = array_name;
    // printf("处理变量: %s, is_array=%d, array_size=%d\n", name, is_array, array_size);
    
    if (current_type && current_type->kind == T_STRUCT) {
        // 处理结构体变量...
        
        if (is_array) {
            // printf("发现结构体数组: %s[%d]\n", name, array_size);
            // 处理结构体数组的特殊逻辑
            int elem_size = get_type_size(current_type);
            int total_size = array_size * elem_size;
            
            Operand array_op = new_variable();
            irlist_append(&list, ir_dec(array_op, total_size));
            
            // 创建数组类型
            Type elem_type = type_deepcopy(current_type);
            Type array_type = type_make_array(elem_type, array_size);
            
            add_var_binding(name, array_op, array_type,0);
            type_free(array_type);
        } else {
            // 普通结构体变量
            IRList struct_ir = handle_struct_variable(name, current_type);
            irlist_concat(&list, struct_ir);
        }
    } else {
        // 非结构体类型
        if (is_array) {
            // printf("处理数组声明: %s, array_size=%d\n", name, array_size);
            
            // 首先检查 current_type 是否已经是数组类型（表示多维数组）
            if (current_type && current_type->kind == T_ARRAY) {
                // printf("检测到多维数组声明: %s\n", name);
                
                // 计算总维度
                int total_dimension = 1; // 已经有一维了
                Type temp = current_type;
                while (temp && temp->kind == T_ARRAY) {
                    total_dimension++;
                    temp = temp->u.array.elem;
                }
                
                // printf("错误：不支持多维数组 '%s' (总维度=%d)\n", name, total_dimension);
                // printf("请使用一维数组或结构体数组替代\n");
                
                has_fatal_error = 1;
                // 可以选择直接返回错误，或者继续处理但标记为错误
                return list;
            }
            
            // 检查 VarDec 节点是否包含多个数组维度
            Node* child = varDec;
            int dimension_count = 0;
            
            // 递归检查所有 LB 节点
            while (child) {
                if (strcmp(NODE_NAME(child), "LB") == 0) {
                    dimension_count++;
                    
                    // 如果发现多个 LB，说明是多维数组
                    if (dimension_count > 1) {
                        // printf("错误：不支持多维数组 '%s' (发现 %d 个维度)\n", 
                            //    name, dimension_count);
                        // printf("语法示例: int mat[4][4] 是不支持的\n");
                        // printf("请改为: int mat[16] 或使用结构体\n");
                        return list;
                    }
                }
                child = NEXT(child);
            }
            
            // 检查嵌套的 VarDec 中是否还有数组
            Node* inner_varDec = CHILD(varDec);
            while (inner_varDec) {
                if (strcmp(NODE_NAME(inner_varDec), "VarDec") == 0) {
                    // 检查内层 VarDec 是否包含数组
                    Node* inner_child = CHILD(inner_varDec);
                    while (inner_child) {
                        if (strcmp(NODE_NAME(inner_child), "LB") == 0) {
                            // printf("错误：不支持嵌套数组声明 '%s'\n", name);
                            // printf("这可能是多维数组声明，如: int mat[4][4]\n");
                            has_fatal_error = 1;
                            return list;
                        }
                        inner_child = NEXT(inner_child);
                    }
                }
                inner_varDec = NEXT(inner_varDec);
            }
            
            // 如果通过检查，创建一维数组
            int elem_size = 4; // 默认大小
            if (current_type) {
                elem_size = get_type_size(current_type);
                // printf("数组元素类型大小: %d\n", elem_size);
            }
            
            int total_size = array_size * elem_size;
            // printf("为一维数组 %s[%d] 分配 %d 字节空间\n", name, array_size, total_size);
            
            Operand actual_array = new_variable();
            irlist_append(&list, ir_dec(actual_array, total_size));
            
            Operand pointer_var = new_variable();

            Operand addr_op = (Operand)malloc(sizeof(*addr_op));
            addr_op->kind = OP_ADDRESS;
            addr_op->u.var_no = actual_array->u.var_no;
            addr_op->addr_of = actual_array;

            irlist_append(&list, ir_assign(pointer_var, addr_op));

            add_var_binding(name, pointer_var, current_type, 0);
            
            // 检查是否有初始化
            Node* assign_op = NEXT(varDec);
            if (assign_op && strcmp(NODE_NAME(assign_op), "ASSIGNOP") == 0) {
                // printf("警告: 数组 %s 有初始化，但当前实现可能不支持\n", name);
                // 这里需要处理数组初始化
            }
        } else {
            // 普通变量
            Operand var_op = new_variable();
            
            Node* assign_op = NEXT(varDec);
            if (assign_op && strcmp(NODE_NAME(assign_op), "ASSIGNOP") == 0) {
                Node* init_exp = NEXT(assign_op);
                if (init_exp) {
                    Operand init_val = new_temp();
                    IRList init_ir = translate_Exp(init_exp, init_val);
                    irlist_concat(&list, init_ir);
                    irlist_append(&list, ir_assign(var_op, init_val));
                }
            }
            
            add_var_binding(name, var_op, current_type, 0);
        }
    }

    return list;
}

static IRList translate_StructFieldAccess(Node* struct_id_node, const char* field_name, Operand place) {
    IRList list = irlist_create();
    
    if (!struct_id_node || !field_name || !place) {
        return list;
    }
    
    const char* struct_name = NODE_TEXT(struct_id_node);
    
    // 查找变量绑定（而不是直接获取操作数）
    VarBinding* binding = var_table;
    Operand struct_var = NULL;
    Type struct_type = NULL;
    int is_param = 0;
    
    while (binding) {
        if (strcmp(binding->name, struct_name) == 0) {
            struct_var = binding->op;
            struct_type = binding->type;
            is_param = binding->is_param;
            break;
        }
        binding = binding->next;
    }
    
    if (!struct_var) {
        // printf("错误：找不到结构体变量 %s\n", struct_name);
        return list;
    }
    
    if (!struct_type) {
        // printf("错误：找不到结构体类型 %s\n", struct_name);
        return list;
    }
    
    // 计算字段偏移
    int offset = get_struct_field_offset(struct_type, field_name);
    if (offset < 0) {
        // printf("错误：找不到字段 %s\n", field_name);
        return list;
    }
    
    // printf("访问结构体字段 %s.%s, 偏移=%d, 是否为参数=%d\n", 
        //    struct_name, field_name, offset, is_param);
    
    // 获取结构体的地址
    Operand struct_addr = new_temp();
    
    if (is_param) {
        // 结构体参数：已经是地址，直接使用（假设传递的是地址）
        // printf("结构体参数 %s，直接作为地址使用\n", struct_name);
        
        // 对于参数，我们假设它已经是地址
        // 如果有专门的地址操作数，使用它；否则直接使用变量
        if (binding->addr_op) {
            irlist_append(&list, ir_assign(struct_addr, binding->addr_op));
        } else {
            // 创建一个地址操作数并缓存
            Operand addr_op = (Operand)malloc(sizeof(*addr_op));
            addr_op->kind = OP_VARIABLE;
            addr_op->u.var_no = struct_var->u.var_no;
            addr_op->addr_of = struct_var;
            binding->addr_op = addr_op;
            irlist_append(&list, ir_assign(struct_addr, addr_op));
        }
    } else {
        // 局部结构体变量：需要取地址
        // printf("局部结构体变量 %s，需要取地址\n", struct_name);
        Operand addr_op = (Operand)malloc(sizeof(*addr_op));
        addr_op->kind = OP_ADDRESS;
        addr_op->u.var_no = struct_var->u.var_no;
        addr_op->addr_of = struct_var;
        irlist_append(&list, ir_assign(struct_addr, addr_op));
    }
    
    if (offset == 0) {
        // 第一个字段，直接从地址加载
        irlist_append(&list, ir_load(place, struct_addr));
    } else {
        // 计算字段地址并加载
        Operand offset_op = op_constant(offset);
        Operand field_addr = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, field_addr, struct_addr, offset_op));
        irlist_append(&list, ir_load(place, field_addr));
    }
    
    return list;
}

static IRList translate_StructFieldAssign(Node* struct_node, const char* field_name, Node* value_node) {
    IRList list = irlist_create();
    
    if (!struct_node || !field_name || !value_node) {
        return list;
    }
    
    // 计算要赋的值
    Operand value = new_temp();
    IRList value_ir = translate_Exp(value_node, value);
    irlist_concat(&list, value_ir);
    
    // 获取结构体变量名
    const char* struct_name = NULL;
    
    if (strcmp(struct_node->name, "ID") == 0) {
        struct_name = NODE_TEXT(struct_node);
    } else if (strcmp(struct_node->name, "Exp") == 0) {
        // 如果是表达式，查找其中的ID节点
        Node* id_node = find_id_node(struct_node);
        if (id_node) {
            struct_name = NODE_TEXT(id_node);
        }
    }
    
    if (!struct_name) {
        // printf("错误：无法获取结构体变量名\n");
        return list;
    }
    
    // printf("结构体字段赋值: %s.%s\n", struct_name, field_name);
    
    // 查找变量绑定
    VarBinding* binding = var_table;
    Operand struct_var = NULL;
    Type struct_type = NULL;
    int is_param = 0;
    
    while (binding) {
        if (strcmp(binding->name, struct_name) == 0) {
            struct_var = binding->op;
            struct_type = binding->type;
            is_param = binding->is_param;
            break;
        }
        binding = binding->next;
    }
    
    if (!struct_var) {
        // printf("错误：找不到结构体变量 %s\n", struct_name);
        return list;
    }
    
    if (!struct_type) {
        // printf("错误：找不到结构体类型 %s\n", struct_name);
        return list;
    }
    
    // 计算字段偏移
    int offset = get_struct_field_offset(struct_type, field_name);
    if (offset < 0) {
        // printf("错误：找不到字段 %s\n", field_name);
        return list;
    }
    
    // printf("字段 %s.%s 的偏移: %d, 是否为参数=%d\n", 
        //    struct_name, field_name, offset, is_param);
    
    // 获取结构体的地址
    Operand struct_addr = new_temp();
    
    if (is_param) {
        // 结构体参数：已经是地址，直接使用
        // printf("结构体参数 %s，直接作为地址使用\n", struct_name);
        
        if (binding->addr_op) {
            irlist_append(&list, ir_assign(struct_addr, binding->addr_op));
        } else {
            Operand addr_op = (Operand)malloc(sizeof(*addr_op));
            addr_op->kind = OP_VARIABLE;
            addr_op->u.var_no = struct_var->u.var_no;
            addr_op->addr_of = struct_var;
            binding->addr_op = addr_op;
            irlist_append(&list, ir_assign(struct_addr, addr_op));
        }
    } else {
        // 局部结构体变量：需要取地址
        // printf("局部结构体变量 %s，需要取地址\n", struct_name);
        Operand addr_op = (Operand)malloc(sizeof(*addr_op));
        addr_op->kind = OP_ADDRESS;
        addr_op->u.var_no = struct_var->u.var_no;
        addr_op->addr_of = struct_var;
        irlist_append(&list, ir_assign(struct_addr, addr_op));
    }
    
    if (offset == 0) {
        // 存储到第一个字段
        irlist_append(&list, ir_store(struct_addr, value));
    } else {
        // 计算字段地址并存储
        Operand offset_op = op_constant(offset);
        Operand field_addr = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, field_addr, struct_addr, offset_op));
        irlist_append(&list, ir_store(field_addr, value));
    }
    
    return list;
}

static IRList handle_struct_variable(const char* name, Type type) {
    IRList list = irlist_create();
    
    if (!name || !type) {
        // printf("错误：handle_struct_variable 参数为空\n");
        return list;
    }
    
    if (type->kind != T_STRUCT) {
        // printf("错误：%s 不是结构体类型\n", name);
        return list;
    }
    
    Operand var_op = new_variable();
    int size = get_type_size(type);
    
    // printf("处理结构体变量: %s，大小: %d 字节\n", name, size);
    
    // 为结构体变量分配空间
    if (size > 0) {
        irlist_append(&list, ir_dec(var_op, size));
        // printf("为结构体变量 %s 分配 %d 字节空间 -> v%d\n", name, size, var_op->u.var_no);
    }
    
    // 绑定变量
    Type type_copy = type_deepcopy(type);
    add_var_binding(name, var_op, type_copy, 0);
    
    // 获取结构体地址用于初始化
    Operand struct_addr = new_temp();
    Operand addr_op = (Operand)malloc(sizeof(*addr_op));
    addr_op->kind = OP_ADDRESS;
    addr_op->u.var_no = var_op->u.var_no;
    addr_op->addr_of = var_op;
    irlist_append(&list, ir_assign(struct_addr, addr_op));
    
    // 根据结构体的实际定义初始化每个字段
    FieldList field = type->u.structure;
    int current_offset = 0;
    
    while (field) {
        // printf("初始化字段: %s (偏移: %d)\n", field->name, current_offset);
        
        Operand zero = op_constant(0);
        
        if (current_offset == 0) {
            // 第一个字段，可以直接使用结构体地址
            irlist_append(&list, ir_store(struct_addr, zero));
        } else {
            // 计算字段地址
            Operand offset_op = op_constant(current_offset);
            Operand field_addr = new_temp();
            irlist_append(&list, ir_binop(IR_ADD, field_addr, struct_addr, offset_op));
            irlist_append(&list, ir_store(field_addr, zero));
        }
        
        // 移动到下一个字段
        int field_size = get_type_size(field->type);
        current_offset += field_size;
        field = field->tail;
    }
    
    return list;
}
/*=====================*
 *    表达式翻译       *
 *=====================*/

// 辅助函数声明
static int is_struct_array_field_access(Node* exp);
static IRList handle_struct_array_field_assign(Node* left_exp, Node* right_exp, Operand place);
static IRList handle_struct_array_field_access(Node* exp, Operand place);
IRList translate_Exp(Node* n, Operand place) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* c1 = CHILD(n);
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;
    Node* c4 = c3 ? NEXT(c3) : NULL;
    Node* c5 = c4 ? NEXT(c4) : NULL;

    // INT常量
    if (c1 && strcmp(NODE_NAME(c1), "INT") == 0 && !c2) {
        if (place) {
            int value = atoi(NODE_TEXT(c1));
            Operand c = op_constant(value);
            irlist_append(&list, ir_assign(place, c));
        }
    }
    // ID变量
    else if (c1 && strcmp(NODE_NAME(c1), "ID") == 0 && !c2) {
        if (place) {
            const char* name = NODE_TEXT(c1);
        
            Operand var_op = get_var_operand(name);
        
            if (!var_op) {
                // printf("错误：变量 %s 未找到\n", name);
                return list;
            }
        
            Type var_type = get_var_type(name);
        
            if (var_type && var_type->kind == T_STRUCT) {
                // 结构体变量，返回地址
                Operand addr = (Operand)malloc(sizeof(*addr));
                addr->kind = OP_ADDRESS;
                addr->u.var_no = var_op->u.var_no;
                addr->addr_of = var_op;
                irlist_append(&list, ir_assign(place, addr));
            }
            else if (var_type && var_type->kind == T_ARRAY) {
                // 数组变量，返回基地址
                // printf("访问数组变量 %s 的基地址\n", name);
                Operand addr = (Operand)malloc(sizeof(*addr));
                addr->kind = OP_VARIABLE;
                addr->u.var_no = var_op->u.var_no;
                addr->addr_of = var_op;
                irlist_append(&list, ir_assign(place, addr));
            } else {
                // 普通变量，返回值
                irlist_append(&list, ir_assign(place, var_op));
            }
        }
    }
    else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
         c2 && strcmp(NODE_NAME(c2), "ASSIGNOP") == 0 &&
         c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {
    
        // printf("=== 处理赋值表达式 ===\n");
        
        // 检查是否是 goods[cnt].price 这种结构体数组元素的字段赋值
        if (is_struct_array_field_access(c1)) {
            // printf("检测到结构体数组字段赋值\n");
            IRList assign_ir = handle_struct_array_field_assign(c1, c3, place);
            irlist_concat(&list, assign_ir);
            return list;
        }
        
        // 检查是否是普通结构体字段赋值
        Node* left_exp = c1;
        Node* right_exp = c3;
        
        // 分析左子树的结构
        if (left_exp && strcmp(left_exp->name, "Exp") == 0) {
            Node* inner_c1 = CHILD(left_exp);
            Node* inner_c2 = inner_c1 ? NEXT(inner_c1) : NULL;
            Node* inner_c3 = inner_c2 ? NEXT(inner_c2) : NULL;
            
            if (inner_c1 && inner_c2 && inner_c3 &&
                strcmp(inner_c1->name, "Exp") == 0 &&
                strcmp(inner_c2->name, "DOT") == 0 &&
                strcmp(inner_c3->name, "ID") == 0) {
                
                // 这是 rect.width 形式的普通结构体字段赋值
                Node* struct_id_node = CHILD(inner_c1);
                Node* field_id = inner_c3;
                
                if (struct_id_node && strcmp(struct_id_node->name, "ID") == 0) {
                    const char* struct_name = NODE_TEXT(struct_id_node);
                    const char* field_name = NODE_TEXT(field_id);
                    
                    // printf("发现结构体字段赋值: %s.%s\n", struct_name, field_name);
                    
                    // 调用结构体字段赋值函数
                    IRList assign_ir = translate_StructFieldAssign(struct_id_node, field_name, right_exp);
                    irlist_concat(&list, assign_ir);
                    
                    // 如果需要返回值，读取字段值
                    if (place) {
                        Operand result = new_temp();
                        IRList access_ir = translate_StructFieldAccess(struct_id_node, field_name, result);
                        irlist_concat(&list, access_ir);
                        irlist_append(&list, ir_assign(place, result));
                    }
                    
                    return list;
                }
            }else if(inner_c1 && inner_c2 && inner_c3 &&
                strcmp(inner_c1->name, "Exp") == 0 &&
                strcmp(inner_c2->name, "LB") == 0 &&
                strcmp(inner_c3->name, "Exp") == 0){
                    // printf("数组元素赋值\n");
    
    // 检查是否有 RB 节点
                    Node* rb = inner_c3 ? NEXT(inner_c3) : NULL;
                    if (!rb || strcmp(rb->name, "RB") != 0) {
                        // printf("错误：数组访问缺少 RB\n");
                        return list;
                    }
    
                    // 获取数组名
                    const char* array_name = NULL;
                    Node* array_id_node = find_id_node(inner_c1);
                    if (array_id_node && strcmp(array_id_node->name, "ID") == 0) {
                        array_name = NODE_TEXT(array_id_node);
                    }
    
                    if (!array_name) {
                        // printf("错误：无法获取数组名\n");
                        return list;
                    }
    
                    // printf("数组元素赋值: %s[索引] = ...\n", array_name);
    
    // 1. 计算左值地址（a[i]的地址）
                    Operand elem_addr = new_temp();
    
    // 计算数组基地址
                    Operand array_var = get_var_operand(array_name);
                    if (!array_var) {
                        // printf("错误：找不到数组变量 %s\n", array_name);
                        return list;
                    }
    
    // 创建数组地址操作数
                    Operand array_addr = new_temp();
                    Operand addr_op = (Operand)malloc(sizeof(*addr_op));
                    addr_op->kind = OP_VARIABLE;
                    addr_op->u.var_no = array_var->u.var_no;
                    addr_op->addr_of = array_var;
                    irlist_append(&list, ir_assign(array_addr, addr_op));
    
    // 计算索引
                    Operand index = new_temp();
                    IRList index_ir = translate_Exp(inner_c3, index);
                    irlist_concat(&list, index_ir);
    
    // 计算元素地址：base + index * 4
                    Operand offset = new_temp();
                    Operand elem_size = op_constant(4);  // 假设int类型
                    irlist_append(&list, ir_binop(IR_MUL, offset, index, elem_size));
    
                    irlist_append(&list, ir_binop(IR_ADD, elem_addr, array_addr, offset));
    
    // 2. 计算右值
                    Operand right_val = new_temp();
                    IRList right_ir = translate_Exp(right_exp, right_val);
                    irlist_concat(&list, right_ir);
    
    // 3. 存储：*elem_addr = right_val
                    irlist_append(&list, ir_store(elem_addr, right_val));
    
    // 4. 如果需要返回值，读取值
                    if (place) {
                        irlist_append(&list, ir_load(place, elem_addr));
                    }
    
                    return list;
                }
        }


        // 检查是否是普通变量赋值
        Node* left_id = find_id_node(c1);
        if (left_id && strcmp(left_id->name, "ID") == 0) {
            const char* var_name = NODE_TEXT(left_id);
            
            // printf("普通变量赋值: %s\n", var_name);
            Type id_type = get_var_type(var_name);
            if(id_type && id_type->kind == T_ARRAY){
                // printf("数组变量赋值（作为指针）: %s\n", var_name);
        
        // 1. 计算右值（应该是另一个数组的地址）
                Operand right_val = new_temp();
                IRList right_ir = translate_Exp(c3, right_val);
                irlist_concat(&list, right_ir);

        // 3. 获取左值变量
                Operand left_var = get_var_operand(var_name);
                if (left_var) {
            // 直接赋值地址
                    irlist_append(&list, ir_assign(left_var, right_val));
            
            // 更新地址操作数缓存
                    VarBinding* binding = find_var_binding(var_name);
                    if (binding) {
                        binding->addr_op = right_val;  // 缓存这个地址
                    }
                }
        
        // 4. 如果需要返回值
                if (place) {
                    irlist_append(&list, ir_assign(place, left_var));
                }
        
                return list;
            }
            else{
                Operand right_val = new_temp();
                IRList right_ir = translate_Exp(c3, right_val);
                irlist_concat(&list, right_ir);
        
                Operand left_var = get_var_operand(var_name);
                if (left_var) {
                    irlist_append(&list, ir_assign(left_var, right_val));
                }
        
                if (place) {
                    irlist_append(&list, ir_assign(place, left_var));
                }
        
                return list;
            }
        }
    
        // printf("未知的赋值类型\n");
        return list;
    }
    else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
             c2 && (strcmp(NODE_NAME(c2), "PLUS")  == 0 ||
                    strcmp(NODE_NAME(c2), "MINUS") == 0 ||
                    strcmp(NODE_NAME(c2), "STAR")  == 0 ||
                    strcmp(NODE_NAME(c2), "DIV")   == 0) &&
             c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {

        Operand t1 = new_temp();
        Operand t2 = new_temp();

        IRList e1 = translate_Exp(c1, t1);
        IRList e2 = translate_Exp(c3, t2);
        
        irlist_concat(&list, e1);
        irlist_concat(&list, e2);

        if (place) {
            IRKind opk = IR_ADD;
            if (strcmp(NODE_NAME(c2), "PLUS") == 0)      opk = IR_ADD;
            else if (strcmp(NODE_NAME(c2), "MINUS") == 0) opk = IR_SUB;
            else if (strcmp(NODE_NAME(c2), "STAR") == 0)  opk = IR_MUL;
            else if (strcmp(NODE_NAME(c2), "DIV") == 0)   opk = IR_DIV;

            irlist_append(&list, ir_binop(opk, place, t1, t2));
        }
    }
    else if(c1 && strcmp(NODE_NAME(c1), "MINUS") == 0 && 
            c2 && strcmp(NODE_NAME(c2), "Exp") == 0) {
        Operand t1 = new_temp();
        IRList e1 = translate_Exp(c2, t1);
        irlist_concat(&list, e1);
        if (place) {
            Operand zero = op_constant(0);
            irlist_append(&list, ir_binop(IR_SUB, place, zero, t1));
        }
    }
    else if (c1 && strcmp(NODE_NAME(c1), "LP") == 0 && c2 && strcmp(NODE_NAME(c2), "Exp") == 0) {
        IRList e1 = translate_Exp(c2, place);
        irlist_concat(&list, e1);
    }
    else if((c2 && (strcmp(NODE_NAME(c2), "AND") == 0 ||
                    strcmp(NODE_NAME(c2), "OR")  == 0))
            ||(c1 && strcmp(NODE_NAME(c1), "NOT") == 0) ){
        Operand label1 = new_label();
        Operand label2 = new_label();

        if (place) {
            Operand zero = op_constant(0);
            irlist_append(&list, ir_assign(place, zero));
        }
        IRList cond = translate_Cond(n, label1, label2);
        irlist_concat(&list, cond);

        if (place) {
            irlist_append(&list, ir_label(label1));
            Operand one = op_constant(1);
            irlist_append(&list, ir_assign(place, one));
            irlist_append(&list, ir_label(label2));
        } 
        else{
            irlist_append(&list, ir_label(label1));
            irlist_append(&list, ir_label(label2));
        }
    }
    else if(c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
            c2 && strcmp(NODE_NAME(c2), "RELOP") == 0 &&
            c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {
    
        Operand label_true = new_label();
        Operand label_false = new_label();
        Operand result = place ? place : new_temp();
    
        if (place) {
            Operand zero = op_constant(0);
            irlist_append(&list, ir_assign(result, zero));
        }
    
        IRList cond = translate_Cond(n, label_true, label_false);
        irlist_concat(&list, cond);
    
        if (place) {
            irlist_append(&list, ir_label(label_true));
            Operand one = op_constant(1);
            irlist_append(&list, ir_assign(result, one));
            irlist_append(&list, ir_label(label_false));
        } 
        else {
            irlist_append(&list, ir_label(label_true));
            irlist_append(&list, ir_label(label_false));
        }
    }
    else if (c1 && strcmp(NODE_NAME(c1), "ID") == 0 &&
             c2 && strcmp(NODE_NAME(c2), "LP") == 0) {

        const char* func_name = NODE_TEXT(c1);
        Node* third = NEXT(c2);

        if (third && strcmp(NODE_NAME(third), "RP") == 0) {
            if (strcmp(func_name, "read") == 0) {
                if (place) {
                    irlist_append(&list, ir_read(place));
                } else {
                    Operand t = new_temp();
                    irlist_append(&list, ir_read(t));
                }
            } else {
                Operand f = op_function((char*)func_name);
                if (place == NULL) {
                    Operand t = new_temp();
                    irlist_append(&list, ir_call(t, f));
                } else {
                    irlist_append(&list, ir_call(place, f));
                }
            }
        } else if (third && strcmp(NODE_NAME(third), "Args") == 0) {
            Operand arg_list[128];
            int arg_cnt = 0;
            IRList args_ir = translate_Args(third, arg_list, &arg_cnt);
            irlist_concat(&list, args_ir);

            if (strcmp(func_name, "write") == 0) {
                if (arg_cnt >= 1) {
                    irlist_append(&list, ir_write(arg_list[0]));
                }
                if (place) {
                    Operand zero = op_constant(0);
                    irlist_append(&list, ir_assign(place, zero));
                }
            }
            else if (strcmp(func_name, "add") == 0) {
                if (arg_cnt >= 1) {
                    irlist_append(&list, ir_arg(arg_list[0]));
                }
                
                Operand f = op_function((char*)func_name);
                Operand result = place ? place : new_temp();
                irlist_append(&list, ir_call(result, f));
            } else {
                for (int i = arg_cnt - 1; i >= 0; --i) {
                    irlist_append(&list, ir_arg(arg_list[i]));
                }
                Operand f = op_function((char*)func_name);
                if (place == NULL) {
                    Operand t = new_temp();
                    irlist_append(&list, ir_call(t, f));
                } else {
                    irlist_append(&list, ir_call(place, f));
                }
            }
        }
    }
    else if(c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
        c2 && strcmp(NODE_NAME(c2), "DOT") == 0 &&
        c3 && strcmp(NODE_NAME(c3), "ID") == 0) {
    
        // printf("处理 Exp DOT ID 结构\n");
    
        const char* field_name = NODE_TEXT(c3);
    
        // 检查是否是 goods[cnt].price 这种结构体数组元素的字段访问
        if (is_struct_array_field_access(c1)) {
            // printf("检测到结构体数组字段访问: [].%s\n", field_name);
            IRList access_ir = handle_struct_array_field_access(c1, place);
            irlist_concat(&list, access_ir);
            return list;
        }
    
        // 普通结构体字段访问
        Node* inner_exp = c1;  // 内层Exp节点
        
        // 获取结构体变量名
        Node* struct_id_node = NULL;
        
        // 尝试直接获取ID节点
        if (strcmp(inner_exp->name, "Exp") == 0) {
            struct_id_node = CHILD(inner_exp);
        }
        
        // 如果第一个子节点不是ID，递归查找
        if (!struct_id_node || strcmp(struct_id_node->name, "ID") != 0) {
            struct_id_node = find_id_node(inner_exp);
        }
    
        if (struct_id_node && strcmp(struct_id_node->name, "ID") == 0) {
            const char* var_name = NODE_TEXT(struct_id_node);
            // printf("访问结构体字段: %s.%s\n", var_name, field_name);
        
            if (place) {
                IRList access_ir = translate_StructFieldAccess(struct_id_node, field_name, place);
                irlist_concat(&list, access_ir);
            } else {
                // 如果没有place，仍然需要处理（比如在参数中）
                Operand temp = new_temp();
                IRList access_ir = translate_StructFieldAccess(struct_id_node, field_name, temp);
                irlist_concat(&list, access_ir);
            }
        } else {
            // printf("错误：无法找到结构体变量名\n");
        }
    
        return list;
    }
    else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
         c2 && strcmp(NODE_NAME(c2), "LB") == 0 &&
         c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {
    
        // printf("处理数组访问\n");
        if (is_struct_field_access(c1)) {
            // printf("这是结构体字段的数组访问\n");
        
            // 处理结构体字段的数组访问
            return handle_struct_field_array_access(n, place);
        }

        // printf("这不是结构体字段的数组访问\n");
    
        // 获取数组基地址
        Operand array_base = new_temp();
        IRList base_ir = translate_Exp(c1, array_base);
        irlist_concat(&list, base_ir);
    
        // 获取索引
        Operand index = new_temp();
        IRList index_ir = translate_Exp(c3, index);
        irlist_concat(&list, index_ir);
    
        // 获取数组变量名
        const char* array_name = NULL;
        Node* id_node = find_id_node(c1);
        if (id_node && strcmp(id_node->name, "ID") == 0) {
            array_name = NODE_TEXT(id_node);
            // printf("数组名: %s\n", array_name);
        }
    
        Type array_type = get_var_type(array_name);
        // printf("数组类型检查开始\n");
        // print_type_info(array_type, "array_type");
        // printf("is_multi_dimension_array = %d\n", is_multi_dimension_array(array_type));
        if (array_type && is_multi_dimension_array(array_type)) {
            // printf("错误：不支持多维数组 %s 的访问\n", array_name);
            return list;
        }

        // 获取元素大小
        int elem_size = 4; // 默认
        if (array_type && array_type->kind == T_ARRAY) {
            elem_size = get_type_size(array_type->u.array.elem);
            // printf("数组 %s 元素大小: %d\n", array_name, elem_size);
        }
    
        // 计算偏移：offset = index * elem_size
        Operand elem_size_op = op_constant(elem_size);
        Operand offset = new_temp();
        irlist_append(&list, ir_binop(IR_MUL, offset, index, elem_size_op));
    
        // 计算元素地址：elem_addr = array_base + offset
        Operand elem_addr = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, elem_addr, array_base, offset));
    
        // 检查是否后面跟着 .field（结构体字段访问）
        Node* next = c2 ? NEXT(c2) : NULL;
        next = next ? NEXT(next) : NULL; // 跳过RB
    
        if (next && strcmp(next->name, "DOT") == 0) {
            // 是 goods[cnt].price 这种结构
            // printf("数组元素后跟着字段访问\n");
            // elem_addr 已经是结构体的地址
            // 后面的DOT处理会使用这个地址
            if (place) {
                irlist_append(&list, ir_assign(place, elem_addr));
            }
        } else {
            // 普通的数组访问：a[i]
            if (place) {
                // 如果是右值，加载值
                irlist_append(&list, ir_load(place, elem_addr));
            } else {
               
                // 如果是左值（在赋值左边），需要地址
                // 这种情况会在上层处理
            }
        }
    
        return list;
    }
    
    return list;
}

/*=====================*
 *   辅助函数          *
 *=====================*/
static VarBinding* find_var_binding(const char* name) {
    VarBinding* p = var_table;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

static Node* find_id_node(Node* exp);

// 检查是否是结构体数组元素的字段访问（如 goods[cnt].price）
static int is_struct_array_field_access(Node* exp) {
    if (!exp || strcmp(exp->name, "Exp") != 0) return 0;
    
    // printf("检查是否是结构体数组字段访问\n");
    
    // 第一种情况：Exp DOT ID 结构
    Node* c1 = CHILD(exp);
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;

    if (c1 && c2 && c3 &&
        strcmp(c1->name, "Exp") == 0 &&
        strcmp(c2->name, "DOT") == 0 &&
        strcmp(c3->name, "ID") == 0) {
        
        // 检查 c1 是否是数组访问：Exp LB Exp RB
        Node* inner_c1 = CHILD(c1);
        Node* inner_c2 = inner_c1 ? NEXT(inner_c1) : NULL;
        Node* inner_c3 = inner_c2 ? NEXT(inner_c2) : NULL;
        Node* inner_c4 = inner_c3 ? NEXT(inner_c3) : NULL;
        
        if (inner_c1 && inner_c2 && inner_c3 && inner_c4 &&
            strcmp(inner_c1->name, "Exp") == 0 &&
            strcmp(inner_c2->name, "LB") == 0 &&
            strcmp(inner_c3->name, "Exp") == 0 &&
            strcmp(inner_c4->name, "RB") == 0) {
            // printf("是结构体数组字段访问（Exp DOT ID 结构）\n");
            return 1;
        }
    }
    
    // 第二种情况：直接是数组访问结构，后面可能跟着DOT
    // 检查是否是 Exp LB Exp RB 结构
    if (c1 && c2 && c3) {
        Node* c4 = c3 ? NEXT(c3) : NULL;

        if (strcmp(c1->name, "Exp") == 0 &&
            strcmp(c2->name, "LB") == 0 &&
            strcmp(c3->name, "Exp") == 0 &&
            c4 && strcmp(c4->name, "RB") == 0) {
            
            // 检查后面是否有 DOT ID
            Node* c5 = c4 ? NEXT(exp) : NULL;
            Node* c6 = c5 ? NEXT(c5) : NULL;
            
            if (c5 && c6 &&
                strcmp(c5->name, "DOT") == 0 &&
                strcmp(c6->name, "ID") == 0) {
                // printf("是结构体数组字段访问（数组访问+DOT结构）\n");
                return 1;
            }
        }
    }
    
    // printf("不是结构体数组字段访问\n");
    return 0;
}

// 处理结构体数组元素的字段赋值
static IRList handle_struct_array_field_assign(Node* left_exp, Node* right_exp, Operand place) {
    IRList list = irlist_create();
    int flag = 0;
    
    // printf("处理结构体数组字段赋值\n");
    // debug_print_ast_structure(left_exp, 1);
    
    // 先尝试解析左侧表达式的结构
    // 应该是: Exp DOT ID
    Node* array_access = NULL;
    Node* dot_op = NULL;
    Node* field_id = NULL;
    
    // 解析 Exp DOT ID 结构
    if (strcmp(left_exp->name, "Exp") == 0) {
        Node* c1 = CHILD(left_exp);
        Node* c2 = c1 ? NEXT(c1) : NULL;
        Node* c3 = c2 ? NEXT(c2) : NULL;
        
        if (c1 && c2 && c3 &&
            strcmp(c1->name, "Exp") == 0 &&
            strcmp(c2->name, "DOT") == 0 &&
            strcmp(c3->name, "ID") == 0) {
            
            array_access = c1;  // 数组访问部分
            dot_op = c2;
            field_id = c3;
        }
    }
    
    if (!array_access || !dot_op || !field_id) {
        // printf("错误：无效的结构体数组字段访问格式\n");
        return list;
    }
    
    // 解析数组访问部分: Exp LB Exp RB
    const char* array_name = NULL;
    Node* index_exp = NULL;
    
    if (strcmp(array_access->name, "Exp") == 0) {
        Node* arr_c1 = CHILD(array_access);
        Node* arr_c2 = arr_c1 ? NEXT(arr_c1) : NULL;
        Node* arr_c3 = arr_c2 ? NEXT(arr_c2) : NULL;
        Node* arr_c4 = arr_c3 ? NEXT(arr_c3) : NULL;
        
        if (arr_c1 && arr_c2 && arr_c3 && arr_c4 &&
            strcmp(arr_c1->name, "Exp") == 0 &&
            strcmp(arr_c2->name, "LB") == 0 &&
            strcmp(arr_c3->name, "Exp") == 0 &&
            strcmp(arr_c4->name, "RB") == 0) {
            
            // 获取数组名
            Node* parent = find_id_parent_node(arr_c1);
            Node* array_id_node = NULL;
            if(NEXT(parent) && strcmp(NODE_NAME(NEXT(parent)), "DOT") == 0){
                flag = 1;
            }else{
                array_id_node = CHILD(parent);
            }
            
            if (array_id_node && strcmp(array_id_node->name, "ID") == 0) {
                array_name = NODE_TEXT(array_id_node);
            }

            index_exp = arr_c3;  // 索引表达式
        }
    }
    
    if(flag == 0){
        if (!array_name || !index_exp) {
            // printf("错误：无法解析数组访问结构\n");
            return list;
        }
        
        const char* field_name = NODE_TEXT(field_id);
        
        // printf("结构体数组字段赋值: %s[].%s\n", array_name, field_name);
        
        // 计算右值
        Operand right_val = new_temp();
        IRList right_ir = translate_Exp(right_exp, right_val);
        irlist_concat(&list, right_ir);
        
        // 获取数组基地址
        Operand array_var = get_var_operand(array_name);
        if (!array_var) {
            // printf("错误：找不到数组变量 %s\n", array_name);
            return list;
        }
        
        // 获取数组变量的地址
        Operand array_addr = (Operand)malloc(sizeof(*array_addr));
        array_addr->kind = OP_ADDRESS;
        array_addr->u.var_no = array_var->u.var_no;
        array_addr->addr_of = array_var;
        
        Operand array_base = new_temp();
        irlist_append(&list, ir_assign(array_base, array_addr));
        
        // 计算索引
        Operand index = new_temp();
        IRList index_ir = translate_Exp(index_exp, index);
        irlist_concat(&list, index_ir);
        
        // 获取数组类型以确定元素大小
        Type array_type = get_var_type(array_name);
        int elem_size = 12;  // Car结构体默认大小：3个int = 12字节
        int field_offset = 0;
        
        if (array_type && array_type->kind == T_ARRAY) {
            Type elem_type = array_type->u.array.elem;
            elem_size = get_type_size(elem_type);
            // printf("数组元素大小: %d\n", elem_size);
            
            // 计算字段偏移
            if (elem_type->kind == T_STRUCT) {
                field_offset = get_struct_field_offset(elem_type, field_name);
                // printf("字段 %s 在结构体中的偏移: %d\n", field_name, field_offset);
            }
        }
        
        // 计算元素地址：elem_addr = array_base + index * elem_size
        Operand elem_size_op = op_constant(elem_size);
        Operand array_offset = new_temp();
        irlist_append(&list, ir_binop(IR_MUL, array_offset, index, elem_size_op));
        
        Operand elem_base = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, elem_base, array_base, array_offset));
        
        // 计算字段地址：field_addr = elem_base + field_offset
        Operand field_addr = new_temp();
        Operand offset_op = op_constant(field_offset);
        irlist_append(&list, ir_binop(IR_ADD, field_addr, elem_base, offset_op));
        
        // 存储值到字段
        irlist_append(&list, ir_store(field_addr, right_val));
        
        // 如果需要返回值，读取字段值
        if (place) {
            irlist_append(&list, ir_load(place, field_addr));
        }
    }else{
        Node* parent = find_id_parent_node(CHILD(array_access));
        const char* struct_name = NODE_TEXT(CHILD(parent));
        VarBinding* binding = find_var_binding(struct_name);
        if (!binding) {
            // printf("错误：找不到结构体变量绑定 %s\n", struct_name);
            return list;
        }
        
        Operand struct_var = binding->op;
        Type struct_type = binding->type;
        int is_param = binding->is_param;  // 获取是否为参数

        if (!struct_var) {
            // printf("错误：找不到结构体变量 %s\n", struct_name);
            return list;
        }
        
        if (!struct_type || struct_type->kind != T_STRUCT) {
            // printf("错误：%s 不是结构体类型\n", struct_name);
            return list;
        }
        
        const char* array_field_name = NODE_TEXT(NEXT(NEXT(parent))); // "cars"
        const char* element_field_name = NODE_TEXT(field_id);    // "loc_x"

        // printf("处理: %s.%s[].%s\n", struct_name, array_field_name, element_field_name);
        
        // 1. 计算 cars 字段在 Street 结构体中的偏移
        int array_field_offset = get_struct_field_offset(struct_type, array_field_name);
        if (array_field_offset < 0) {
            // printf("错误：找不到字段 %s\n", array_field_name);
            return list;
        }
        
        // printf("字段 %s 在结构体中的偏移: %d\n", array_field_name, array_field_offset);
        
        // 2. 获取数组字段的类型
        Type array_field_type = NULL;
        FieldList field_list = struct_type->u.structure;
        while (field_list) {
            if (field_list->name && strcmp(field_list->name, array_field_name) == 0) {
                array_field_type = field_list->type;
                break;
            }
            field_list = field_list->tail;
        }
        
        if (!array_field_type || array_field_type->kind != T_ARRAY) {
            // printf("错误：%s.%s 不是数组类型\n", struct_name, array_field_name);
            return list;
        }
        
        // 3. 获取数组元素类型（应该是 Car 结构体）
        Type elem_type = array_field_type->u.array.elem;
        if (!elem_type || elem_type->kind != T_STRUCT) {
            // printf("错误：数组元素不是结构体类型\n");
            return list;
        }
        
        // 4. 计算元素中字段的偏移
        int element_field_offset = get_struct_field_offset(elem_type, element_field_name);
        if (element_field_offset < 0) {
            // printf("错误：找不到字段 %s\n", element_field_name);
            return list;
        }
        
        // printf("字段 %s 在 Car 中的偏移: %d\n", element_field_name, element_field_offset);
        
        // 5. 计算右值
        Operand right_val = new_temp();
        IRList right_ir = translate_Exp(right_exp, right_val);
        irlist_concat(&list, right_ir);
        
        // 6. 获取结构体地址
        Operand struct_addr = new_temp();
        if (is_param) {
            // 参数：v1 保存地址值，直接使用
            // printf("结构体参数 %s，变量保存地址值\n", struct_name);
            
            if (binding->addr_op) {
                // 使用缓存的地址临时变量
                irlist_append(&list, ir_assign(struct_addr, binding->addr_op));
            } else {
                // v1 保存地址值，直接赋值给临时变量
                irlist_append(&list, ir_assign(struct_addr, struct_var));
                // 缓存这个临时变量
                binding->addr_op = struct_addr;
            }
        } else {
            // 局部变量：需要取地址
            // printf("局部结构体变量 %s，需要取地址\n", struct_name);
            
            Operand addr_op = (Operand)malloc(sizeof(*addr_op));
            addr_op->kind = OP_VARIABLE;
            addr_op->u.var_no = struct_var->u.var_no;
            addr_op->addr_of = struct_var;
            irlist_append(&list, ir_assign(struct_addr, addr_op));
        }
        
        // 7. 计算数组基地址：struct_addr + array_field_offset
        Operand array_field_offset_op = op_constant(array_field_offset);
        Operand array_base = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, array_base, struct_addr, array_field_offset_op));
        
        // 8. 计算索引
        Operand index = new_temp();
        IRList index_ir = translate_Exp(index_exp, index);
        irlist_concat(&list, index_ir);
        
        // 9. 计算元素大小（Car 结构体大小）
        int elem_size = get_type_size(elem_type);
        // printf("Car结构体大小: %d\n", elem_size);
        
        // 10. 计算元素地址：array_base + index * elem_size
        Operand elem_size_op = op_constant(elem_size);
        Operand array_offset = new_temp();
        irlist_append(&list, ir_binop(IR_MUL, array_offset, index, elem_size_op));
        
        Operand elem_base = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, elem_base, array_base, array_offset));
        
        // 11. 计算字段地址：elem_base + element_field_offset
        Operand field_offset_op = op_constant(element_field_offset);
        Operand field_addr = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, field_addr, elem_base, field_offset_op));
        
        // 12. 存储值到字段
        irlist_append(&list, ir_store(field_addr, right_val));
        
        // 13. 如果需要返回值，读取字段值
        if (place) {
            irlist_append(&list, ir_load(place, field_addr));
        }
    }
    
    return list;
}

// 处理结构体数组元素的字段访问
static IRList handle_struct_array_field_access(Node* exp, Operand place) {
    IRList list = irlist_create();
    int flag = 0;  // 0: 普通数组，1: 结构体字段数组
    
    // printf("处理结构体数组字段访问\n");
    // debug_print_ast_structure(exp, 0);
    
    Node* array_access = NULL;
    Node* dot_op = NULL;
    Node* field_id = NULL;
    
    // 检查是哪种结构
    Node* c1 = exp;
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;
    
    if (c1 && c2 && c3 &&
        strcmp(c1->name, "Exp") == 0 &&
        strcmp(c2->name, "DOT") == 0 &&
        strcmp(c3->name, "ID") == 0) {
        // 结构：Exp DOT ID
        array_access = c1;
        dot_op = c2;
        field_id = c3;
    }
    
    if (!array_access || !dot_op || !field_id) {
        // printf("错误：无法解析结构体数组字段访问结构\n");
        return list;
    }
    
    // 解析数组访问部分
    const char* array_name = NULL;
    const char* struct_name = NULL;
    const char* array_field_name = NULL;
    Node* index_exp = NULL;
    
    if (strcmp(array_access->name, "Exp") == 0) {
        Node* arr_c1 = CHILD(array_access);
        Node* arr_c2 = arr_c1 ? NEXT(arr_c1) : NULL;
        Node* arr_c3 = arr_c2 ? NEXT(arr_c2) : NULL;
        Node* arr_c4 = arr_c3 ? NEXT(arr_c3) : NULL;
        
        if (arr_c1 && arr_c2 && arr_c3 && arr_c4 &&
            strcmp(arr_c1->name, "Exp") == 0 &&
            strcmp(arr_c2->name, "LB") == 0 &&
            strcmp(arr_c3->name, "Exp") == 0 &&
            strcmp(arr_c4->name, "RB") == 0) {
            
            // 检查是否是结构体字段的数组访问
            Node* struct_field = arr_c1;
            
            // 尝试解析结构体字段访问
            Node* id_parent = find_id_parent_node(struct_field);
            if (id_parent) {
                Node* id_node = CHILD(id_parent);
                Node* next_node = NEXT(id_parent);
                
                if (next_node && strcmp(NODE_NAME(next_node), "DOT") == 0) {
                    // 这是结构体字段访问：struct_name.array_field_name
                    flag = 1;
                    struct_name = NODE_TEXT(id_node);
                    
                    // 获取数组字段名
                    Node* array_field_node = NEXT(next_node);
                    if (array_field_node && strcmp(array_field_node->name, "ID") == 0) {
                        array_field_name = NODE_TEXT(array_field_node);
                    }
                } else {
                    // 直接数组访问
                    flag = 0;
                    if (id_node && strcmp(id_node->name, "ID") == 0) {
                        array_name = NODE_TEXT(id_node);
                    }
                }
            }
            
            index_exp = arr_c3;  // 索引表达式
        }
    }
    
    if (!index_exp) {
        // printf("错误：无法获取索引表达式\n");
        return list;
    }
    
    const char* element_field_name = NODE_TEXT(field_id);
    
    if (flag == 1) {
        // 处理结构体字段数组访问：myStreet.cars[0].loc_x
        if (!struct_name || !array_field_name) {
            // printf("错误：无法解析结构体数组字段访问\n");
            return list;
        }
        
        // printf("结构体数组字段访问: %s.%s[].%s\n", struct_name, array_field_name, element_field_name);
        
        VarBinding* binding = find_var_binding(struct_name);
        if (!binding) {
            // printf("错误：找不到结构体变量绑定 %s\n", struct_name);
            return list;
        }
        
        Operand struct_var = binding->op;
        Type struct_type = binding->type;
        int is_param = binding->is_param;
        
        // 3. 计算数组字段在结构体中的偏移
        int array_field_offset = get_struct_field_offset(struct_type, array_field_name);
        if (array_field_offset < 0) {
            // printf("错误：找不到字段 %s\n", array_field_name);
            return list;
        }
        
        // printf("字段 %s 在结构体中的偏移: %d\n", array_field_name, array_field_offset);
        
        // 4. 获取数组字段的类型
        Type array_field_type = NULL;
        FieldList field_list = struct_type->u.structure;
        while (field_list) {
            if (field_list->name && strcmp(field_list->name, array_field_name) == 0) {
                array_field_type = field_list->type;
                break;
            }
            field_list = field_list->tail;
        }
        
        if (!array_field_type || array_field_type->kind != T_ARRAY) {
            // printf("错误：%s.%s 不是数组类型\n", struct_name, array_field_name);
            return list;
        }
        
        // 5. 获取数组元素类型
        Type elem_type = array_field_type->u.array.elem;
        if (!elem_type || elem_type->kind != T_STRUCT) {
            // printf("错误：数组元素不是结构体类型\n");
            return list;
        }
        
        // 6. 计算元素中字段的偏移
        int element_field_offset = get_struct_field_offset(elem_type, element_field_name);
        if (element_field_offset < 0) {
            // printf("错误：找不到字段 %s\n", element_field_name);
            return list;
        }
        
        // printf("字段 %s 在元素中的偏移: %d\n", element_field_name, element_field_offset);
        
        // 7. 获取结构体地址
        Operand struct_addr = new_temp();
        if (is_param) {
            // 参数：v1 保存地址值，直接使用
            // printf("结构体参数 %s，变量保存地址值\n", struct_name);
            
            if (binding->addr_op) {
                irlist_append(&list, ir_assign(struct_addr, binding->addr_op));
            } else {
                irlist_append(&list, ir_assign(struct_addr, struct_var));
                binding->addr_op = struct_addr;
            }
        } else {
            // 局部变量：需要取地址
            // printf("局部结构体变量 %s，需要取地址\n", struct_name);
            
            Operand addr_op = (Operand)malloc(sizeof(*addr_op));
            addr_op->kind = OP_ADDRESS;
            addr_op->u.var_no = struct_var->u.var_no;
            addr_op->addr_of = struct_var;
            irlist_append(&list, ir_assign(struct_addr, addr_op));
        }
        
        // 8. 计算数组基地址：struct_addr + array_field_offset
        Operand array_field_offset_op = op_constant(array_field_offset);
        Operand array_base = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, array_base, struct_addr, array_field_offset_op));
        
        // 9. 计算索引
        Operand index = new_temp();
        IRList index_ir = translate_Exp(index_exp, index);
        irlist_concat(&list, index_ir);
        
        // 10. 计算元素大小
        int elem_size = get_type_size(elem_type);
        
        // 11. 计算元素地址：array_base + index * elem_size
        Operand elem_size_op = op_constant(elem_size);
        Operand array_offset = new_temp();
        irlist_append(&list, ir_binop(IR_MUL, array_offset, index, elem_size_op));
        
        Operand elem_base = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, elem_base, array_base, array_offset));
        
        // 12. 计算字段地址并访问
        Operand field_offset_op = op_constant(element_field_offset);
        Operand field_addr = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, field_addr, elem_base, field_offset_op));
        
        if (place) {
            irlist_append(&list, ir_load(place, field_addr));
        }
        
    } else {
        // 处理普通数组访问：array[0].field
        if (!array_name) {
            // printf("错误：无法获取数组名\n");
            return list;
        }
        
        // printf("普通数组字段访问: %s[].%s\n", array_name, element_field_name);
        
        // 获取数组变量
        Operand array_var = get_var_operand(array_name);
        if (!array_var) {
            // printf("错误：找不到数组变量 %s\n", array_name);
            return list;
        }
        
        // 获取数组基地址
        Operand array_addr = new_temp();
        Operand addr_op = (Operand)malloc(sizeof(*addr_op));
        addr_op->kind = OP_ADDRESS;
        addr_op->u.var_no = array_var->u.var_no;
        addr_op->addr_of = array_var;
        irlist_append(&list, ir_assign(array_addr, addr_op));
        
        // 计算索引
        Operand index = new_temp();
        IRList index_ir = translate_Exp(index_exp, index);
        irlist_concat(&list, index_ir);
        
        // 获取数组类型
        Type array_type = get_var_type(array_name);
        int elem_size = 12;  // 默认Car大小
        int field_offset = 0;
        
        if (array_type && array_type->kind == T_ARRAY) {
            Type elem_type = array_type->u.array.elem;
            elem_size = get_type_size(elem_type);
            
            // 计算字段偏移
            if (elem_type->kind == T_STRUCT) {
                field_offset = get_struct_field_offset(elem_type, element_field_name);
            }
        }
        
        // 计算元素地址
        Operand elem_size_op = op_constant(elem_size);
        Operand array_offset = new_temp();
        irlist_append(&list, ir_binop(IR_MUL, array_offset, index, elem_size_op));
        
        Operand elem_base = new_temp();
        irlist_append(&list, ir_binop(IR_ADD, elem_base, array_addr, array_offset));
        
        // 计算字段地址并访问
        if (field_offset == 0) {
            if (place) {
                irlist_append(&list, ir_load(place, elem_base));
            }
        } else {
            Operand offset_op = op_constant(field_offset);
            Operand field_addr = new_temp();
            irlist_append(&list, ir_binop(IR_ADD, field_addr, elem_base, offset_op));
            
            if (place) {
                irlist_append(&list, ir_load(place, field_addr));
            }
        }
    }
    
    return list;
}

// 查找ID节点
static Node* find_id_node(Node* exp) {
    if (!exp) return NULL;
    
    if (strcmp(exp->name, "ID") == 0) {
        return exp;
    }
    
    Node* child = CHILD(exp);
    while (child) {
        Node* result = find_id_node(child);
        if (result) return result;
        child = NEXT(child);
    }
    
    return NULL;
}

static Node* find_id_parent_node(Node* exp) {
    if (!exp) return NULL;
    
    if (strcmp(exp->child->name, "ID") == 0) {
        return exp;
    }
    
    Node* child = CHILD(exp);
    while (child) {
        Node* result = find_id_parent_node(child);
        if (result) return result;
        child = NEXT(child);
    }
    
    return NULL;
}

/*=====================*
 *   条件表达式翻译    *
 *=====================*/

static IRList translate_Cond(Node* n, Operand label_true, Operand label_false) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* c1 = CHILD(n);
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;

    if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
        c2 && strcmp(NODE_NAME(c2), "RELOP") == 0 &&
        c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {

        RelopKind rk = relop_from_node(c2);

        Operand t1 = new_temp();
        Operand t2 = new_temp();
        IRList e1 = translate_Exp(c1, t1);
        IRList e2 = translate_Exp(c3, t2);
        irlist_concat(&list, e1);
        irlist_concat(&list, e2);

        irlist_append(&list, ir_if(t1, rk, t2, label_true));
        irlist_append(&list, ir_goto(label_false));
        return list;
    }
    else if (c1 && strcmp(NODE_NAME(c1), "NOT") == 0) {
        IRList inner = translate_Cond(c2, label_false, label_true);
        irlist_concat(&list, inner);
    }
    else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
             c2 && strcmp(NODE_NAME(c2), "AND") == 0 &&
             c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {

        Operand label1 = new_label();
        IRList cnd1 = translate_Cond(c1, label1, label_false);
        irlist_concat(&list, cnd1);
        irlist_append(&list, ir_label(label1));
        IRList cnd2 = translate_Cond(c3, label_true, label_false);
        irlist_concat(&list, cnd2);
    }
    else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
             c2 && strcmp(NODE_NAME(c2), "OR") == 0 &&
             c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {

        Operand label1 = new_label();
        IRList cnd1 = translate_Cond(c1, label_true, label1);
        irlist_concat(&list, cnd1);
        irlist_append(&list, ir_label(label1));
        IRList cnd2 = translate_Cond(c3, label_true, label_false);
        irlist_concat(&list, cnd2);
    }
    
    else {
        Operand t1 = new_temp();
        IRList e1 = translate_Exp(n, t1);
        irlist_concat(&list, e1);
        Operand zero = op_constant(0);
        irlist_append(&list, ir_if(t1, RELOP_NE, zero, label_true));
        irlist_append(&list, ir_goto(label_false));
    }

    return list;
}

static IRList handle_struct_field_array_access(Node* exp, Operand place) {
    IRList list = irlist_create();
    
    // exp 结构：Exp LB Exp RB
    // 例如：street2.cars[0]
    
    // printf("处理结构体字段数组访问\n");
    // debug_print_ast_structure(exp, 1);
    
    // 获取 Exp (street2.cars)
    Node* struct_field_exp = CHILD(exp);  // street2.cars
    Node* lb = NEXT(struct_field_exp);    // LB
    Node* index_exp = NEXT(lb);           // Exp (索引)
    Node* rb = NEXT(index_exp);           // RB
    
    // printf("struct_field_exp 类型: %s\n", struct_field_exp->name);
    
    // 解析结构体字段访问：street2.cars
    const char* struct_name = NULL;
    const char* field_name = NULL;
    
    if (strcmp(struct_field_exp->name, "Exp") == 0) {
        // 可能是 Exp DOT ID 结构
        Node* c1 = CHILD(struct_field_exp);
        Node* c2 = c1 ? NEXT(c1) : NULL;
        Node* c3 = c2 ? NEXT(c2) : NULL;
        
        if (c1 && c2 && c3 &&
            strcmp(c1->name, "Exp") == 0 &&
            strcmp(c2->name, "DOT") == 0 &&
            strcmp(c3->name, "ID") == 0) {
            
            // 获取结构体名
            Node* struct_id_parent_node = find_id_parent_node(c1);
            Node* struct_id_node = NULL;

            if(NEXT(struct_id_parent_node) && strcmp(NODE_NAME(struct_id_parent_node), "DOT") == 0){
                struct_id_node = NEXT(NEXT(struct_id_parent_node));
            }else{
                struct_id_node = CHILD(struct_id_parent_node);
            }

            if (struct_id_node && strcmp(struct_id_node->name, "ID") == 0) {
                struct_name = NODE_TEXT(struct_id_node);
            }
            
            // 获取字段名
            field_name = NODE_TEXT(c3);
        } else if (c1 && c2 && c3 &&
                   strcmp(c1->name, "ID") == 0 &&
                   strcmp(c2->name, "DOT") == 0 &&
                   strcmp(c3->name, "ID") == 0) {
            
            // 直接是 ID DOT ID 结构
            struct_name = NODE_TEXT(c1);
            field_name = NODE_TEXT(c3);
        }
    }
    
    if (!struct_name || !field_name) {
        // printf("错误：无法解析结构体字段访问: struct_name=%s, field_name=%s\n", 
            //    struct_name ? struct_name : "NULL", field_name ? field_name : "NULL");
        
        // 尝试其他方式解析
        Node* temp = struct_field_exp;
        while (temp) {
            if (strcmp(temp->name, "ID") == 0) {
                if (!struct_name) {
                    struct_name = NODE_TEXT(temp);
                } else if (!field_name) {
                    field_name = NODE_TEXT(temp);
                }
            }
            temp = CHILD(temp);
        }
        
        if (!struct_name || !field_name) {
            return list;
        }
    }
    
    // printf("结构体字段数组访问: %s.%s[]\n", struct_name, field_name);
    
    VarBinding* binding = find_var_binding(struct_name);
    if (!binding) {
        // printf("错误：找不到结构体变量绑定 %s\n", struct_name);
        return list;
    }
    
    Operand struct_var = binding->op;
    Type struct_type = binding->type;
    int is_param = binding->is_param;  // 关键：获取是否为参数

    if (!struct_var) {
        // printf("错误：找不到结构体变量 %s\n", struct_name);
        return list;
    }
    
    if (!struct_type || struct_type->kind != T_STRUCT) {
        // printf("错误：%s 不是结构体类型\n", struct_name);
        return list;
    }
    
    // 计算字段在结构体中的偏移
    int field_offset = get_struct_field_offset(struct_type, field_name);
    if (field_offset < 0) {
        // printf("错误：找不到字段 %s\n", field_name);
        return list;
    }
    
    // printf("字段 %s 在结构体中的偏移: %d\n", field_name, field_offset);
    
    // 获取结构体地址
    Operand struct_base = new_temp();

    if (is_param) {
        // 参数：变量保存地址值，直接使用
        // printf("结构体参数 %s，变量保存地址值 v%d\n", 
            //    struct_name, struct_var->u.var_no);
        
        if (binding->addr_op) {
            // 使用缓存的地址临时变量
            irlist_append(&list, ir_assign(struct_base, binding->addr_op));
        } else {
            // 直接赋值：struct_base := struct_var
            irlist_append(&list, ir_assign(struct_base, struct_var));
            // 缓存这个临时变量
            binding->addr_op = struct_base;
        }
    } else {
        // 局部变量：需要取地址
        // printf("局部结构体变量 %s，需要取地址 &v%d\n", 
            //    struct_name, struct_var->u.var_no);
        
        // 创建地址操作数
        Operand addr_op = (Operand)malloc(sizeof(*addr_op));
        addr_op->kind = OP_ADDRESS;
        addr_op->u.var_no = struct_var->u.var_no;
        addr_op->addr_of = struct_var;
        
        irlist_append(&list, ir_assign(struct_base, addr_op));
    }
    
    // 计算数组基地址：struct_base + field_offset
    Operand offset_op = op_constant(field_offset);
    Operand array_base = new_temp();
    irlist_append(&list, ir_binop(IR_ADD, array_base, struct_base, offset_op));
    
    // 计算索引
    Operand index = new_temp();
    IRList index_ir = translate_Exp(index_exp, index);
    irlist_concat(&list, index_ir);
    
    // 获取数组元素类型和大小
    Type field_type = NULL;
    FieldList field_list = struct_type->u.structure;
    while (field_list) {
        if (field_list->name && strcmp(field_list->name, field_name) == 0) {
            field_type = field_list->type;
            break;
        }
        field_list = field_list->tail;
    }
    
    if (!field_type || field_type->kind != T_ARRAY) {
        // printf("错误：%s.%s 不是数组类型\n", struct_name, field_name);
        return list;
    }
    
    Type elem_type = field_type->u.array.elem;
    int elem_size = get_type_size(elem_type);
    // printf("数组元素类型大小: %d\n", elem_size);
    
    // 计算元素地址：array_base + index * elem_size
    Operand elem_size_op = op_constant(elem_size);
    Operand elem_offset = new_temp();
    irlist_append(&list, ir_binop(IR_MUL, elem_offset, index, elem_size_op));
    
    Operand elem_addr = new_temp();
    irlist_append(&list, ir_binop(IR_ADD, elem_addr, array_base, elem_offset));
    
    // 返回元素地址
    if (place) {
        if (elem_type->kind == T_STRUCT) {
            // 结构体数组元素，返回地址
            irlist_append(&list, ir_assign(place, elem_addr));
        } else {
            // 基本类型数组元素，加载值
            irlist_append(&list, ir_load(place, elem_addr));
        }
    }
    
    return list;
}

static int is_struct_field_access(Node* exp) {
    if (!exp || strcmp(exp->name, "Exp") != 0) return 0;
    
    // 检查是否是 Exp DOT ID 结构
    Node* c1 = CHILD(exp);
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;
    
    if (c1 && c2 && c3 &&
        strcmp(c1->name, "Exp") == 0 &&
        strcmp(c2->name, "DOT") == 0 &&
        strcmp(c3->name, "ID") == 0) {
        return 1;
    }
    
    // 或者第一个子节点是 ID DOT ID
    if (c1 && c2 && c3 &&
        strcmp(c1->name, "ID") == 0 &&
        strcmp(c2->name, "DOT") == 0 &&
        strcmp(c3->name, "ID") == 0) {
        return 1;
    }
    
    return 0;
}

/*=====================*
 *   Args 翻译         *
 *=====================*/

 static IRList translate_Args(Node* n, Operand* arg_list, int* arg_cnt) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* exp = CHILD(n);
    Node* comma_or_end = NEXT(exp);
    
    // 简单检查：如果参数是简单的ID变量，检查是否为数组
    if (exp && exp->child && strcmp(exp->child->name, "ID") == 0) {
        const char* var_name = NODE_TEXT(exp->child);
        
        // 从var_table查找类型
        VarBinding* binding = var_table;
        while (binding) {
            if (strcmp(binding->name, var_name) == 0) {
                // 检查是否为数组类型
                Type type = binding->type;
                if (type && type->kind == T_ARRAY) {
                    // printf("错误：变量 '%s' 是数组，不能作为函数参数\n", var_name);
                    // printf("请使用指针或传递数组元素\n");
                    has_fatal_error = 1;
                }
                break;
            }
            binding = binding->next;
        }
    }
    
    // 翻译表达式
    Operand t = new_temp();
    IRList e = translate_Exp(exp, t);
    irlist_concat(&list, e);

    arg_list[*arg_cnt] = t;
    (*arg_cnt)++;

    if (comma_or_end && strcmp(NODE_NAME(comma_or_end), "COMMA") == 0) {
        Node* args2 = NEXT(comma_or_end);
        IRList rest = translate_Args(args2, arg_list, arg_cnt);
        irlist_concat(&list, rest);
    }

    return list;
}