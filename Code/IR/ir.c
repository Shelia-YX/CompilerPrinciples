#include "ir.h"
#include "../semantic/type.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* 内存分配工具 */
static void* xmalloc(size_t size) {
    void* p = malloc(size);
    if (!p) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    return p;
}

/*=========================*
 *  Operand 构造函数实现   *
 *=========================*/

Operand op_variable(int no) {
    Operand op = (Operand)xmalloc(sizeof(*op));
    op->kind = OP_VARIABLE;
    op->u.var_no = no;
    return op;
}

Operand op_temp(int no) {
    Operand op = (Operand)xmalloc(sizeof(*op));
    op->kind = OP_TEMP;
    op->u.var_no = no;
    return op;
}

Operand op_constant(int value) {
    Operand op = (Operand)xmalloc(sizeof(*op));
    op->kind = OP_CONSTANT;
    op->u.value = value;
    return op;
}

Operand op_label(int no) {
    Operand op = (Operand)xmalloc(sizeof(*op));
    op->kind = OP_LABEL;
    op->u.var_no = no;       /* 打印时会转成 label<no> */
    return op;
}

Operand op_function(char* name) {
    Operand op = (Operand)xmalloc(sizeof(*op));
    op->kind = OP_FUNCTION;
    op->u.name = xstrdup(name);   /* 拷贝一份函数名 */
    return op;
}

// ir.c - 修复op_address函数
Operand op_address(Operand var) {
    /* 正确实现地址操作数 */
    Operand op = (Operand)xmalloc(sizeof(*op));
    op->kind = OP_ADDRESS;
    // 保存原始操作数的引用
    op->u.var_no = var->u.var_no;  // 保存变量编号
    op->addr_of = var;  // 添加这个字段来引用原始操作数
    return op;
}

/*=========================*
 *   IRNode 构造函数实现   *
 *=========================*/

IRNode ir_label(Operand label) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_LABEL;
    node->u.label.op = label;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_function(Operand func) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_FUNCTION;
    node->u.function.func = func;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_assign(Operand left, Operand right) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_ASSIGN;
    node->u.assign.left  = left;
    node->u.assign.right = right;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_binop(IRKind kind, Operand result, Operand op1, Operand op2) {
    /* 只允许 ADD/SUB/MUL/DIV */
    assert(kind == IR_ADD || kind == IR_SUB ||
           kind == IR_MUL || kind == IR_DIV);

    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = kind;
    node->u.binop.result = result;
    node->u.binop.op1    = op1;
    node->u.binop.op2    = op2;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_addr(Operand result, Operand var) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_ADDR;
    node->u.addr.result = result;
    node->u.addr.addr   = var;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_load(Operand result, Operand ptr) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_LOAD;
    node->u.load.result = result;
    node->u.load.ptr    = ptr;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_store(Operand ptr, Operand value) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_STORE;
    node->u.store.ptr   = ptr;
    node->u.store.value = value;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_goto(Operand target) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_GOTO;
    node->u.jump.target = target;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_if(Operand op1, RelopKind relop, Operand op2, Operand target) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_IF;
    node->u.cond_jump.op1    = op1;
    node->u.cond_jump.op2    = op2;
    node->u.cond_jump.target = target;
    node->u.cond_jump.relop  = relop;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_return(Operand ret_val) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_RETURN;
    node->u.ret.ret_val = ret_val;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_dec(Operand var, int size) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_DEC;
    node->u.dec.var  = var;
    node->u.dec.size = size;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_arg(Operand arg) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_ARG;
    node->u.arg.arg = arg;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_call(Operand result, Operand func) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_CALL;
    node->u.call.result = result;
    node->u.call.func   = func;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_param(Operand param) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_PARAM;
    node->u.param.param = param;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_read(Operand var) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_READ;
    node->u.io.var = var;
    node->prev = node->next = NULL;
    return node;
}

IRNode ir_write(Operand var) {
    IRNode node = (IRNode)xmalloc(sizeof(*node));
    node->kind = IR_WRITE;
    node->u.io.var = var;
    node->prev = node->next = NULL;
    return node;
}

/*=========================*
 *      链表操作实现       *
 *=========================*/

IRList irlist_create(void) {
    IRList list;
    list.head = list.tail = NULL;
    return list;
}

void irlist_append(IRList* list, IRNode node) {
    if (!node) return;
    if (!list->head) {
        list->head = list->tail = node;
    } else {
        list->tail->next = node;
        node->prev = list->tail;
        list->tail = node;
    }
}

void irlist_concat(IRList* list1, IRList list2) {
    if (!list2.head) return;

    if (!list1->head) {
        *list1 = list2;
    } else {
        list1->tail->next = list2.head;
        list2.head->prev  = list1->tail;
        list1->tail       = list2.tail;
    }
}

/*=========================*
 *      IR 打印与释放      *
 *=========================*/

/* 辅助：打印 Operand */
static void print_operand(FILE* out, Operand op) {
    if (!op) return;
    switch (op->kind) {
    case OP_VARIABLE:
        fprintf(out, "v%d", op->u.var_no);
        break;
    case OP_TEMP:
        fprintf(out, "t%d", op->u.var_no);
        break;
    case OP_CONSTANT:
        fprintf(out, "#%d", op->u.value);
        break;
    case OP_LABEL:
        fprintf(out, "label%d", op->u.var_no);
        break;
    case OP_FUNCTION:
        fprintf(out, "%s", op->u.name);
        break;
    case OP_ADDRESS:
        // 对于地址操作数，打印 &vX
        fprintf(out, "&v%d", op->u.var_no);
        break;
    default:
        fprintf(out, "??");
        break;
    }
}

/* 辅助：打印 Relop */
static void print_relop(FILE* out, RelopKind relop) {
    switch (relop) {
    case RELOP_EQ: fprintf(out, "=="); break;
    case RELOP_NE: fprintf(out, "!="); break;
    case RELOP_LT: fprintf(out, "<");  break;
    case RELOP_GT: fprintf(out, ">");  break;
    case RELOP_LE: fprintf(out, "<="); break;
    case RELOP_GE: fprintf(out, ">="); break;
    default:       fprintf(out, "??"); break;
    }
}

/* 打印整张 IR 表到文件 */
void irlist_print(FILE* out, IRList list) {
    for (IRNode p = list.head; p != NULL; p = p->next) {
        switch (p->kind) {
        case IR_LABEL:
            fprintf(out, "LABEL ");
            print_operand(out, p->u.label.op);
            fprintf(out, " :\n");
            break;

        case IR_FUNCTION:
            fprintf(out, "FUNCTION ");
            print_operand(out, p->u.function.func);
            fprintf(out, " :\n");
            break;

        case IR_ASSIGN:
            print_operand(out, p->u.assign.left);
            fprintf(out, " := ");
            print_operand(out, p->u.assign.right);
            fprintf(out, "\n");
            break;

        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV: {
            print_operand(out, p->u.binop.result);
            fprintf(out, " := ");
            print_operand(out, p->u.binop.op1);
            const char* op = NULL;
            if (p->kind == IR_ADD)      op = " + ";
            else if (p->kind == IR_SUB) op = " - ";
            else if (p->kind == IR_MUL) op = " * ";
            else if (p->kind == IR_DIV) op = " / ";
            fprintf(out, "%s", op);
            print_operand(out, p->u.binop.op2);
            fprintf(out, "\n");
            break;
        }

        case IR_ADDR:
            print_operand(out, p->u.addr.result);
            fprintf(out, " := &");
            print_operand(out, p->u.addr.addr);
            fprintf(out, "\n");
            break;

        case IR_LOAD:
            print_operand(out, p->u.load.result);
            fprintf(out, " := *");
            print_operand(out, p->u.load.ptr);
            fprintf(out, "\n");
            break;

        case IR_STORE:
            fprintf(out, "*");
            print_operand(out, p->u.store.ptr);
            fprintf(out, " := ");
            print_operand(out, p->u.store.value);
            fprintf(out, "\n");
            break;

        case IR_GOTO:
            fprintf(out, "GOTO ");
            print_operand(out, p->u.jump.target);
            fprintf(out, "\n");
            break;

        case IR_IF:
            fprintf(out, "IF ");
            print_operand(out, p->u.cond_jump.op1);
            fprintf(out, " ");
            print_relop(out, p->u.cond_jump.relop);
            fprintf(out, " ");
            print_operand(out, p->u.cond_jump.op2);
            fprintf(out, " GOTO ");
            print_operand(out, p->u.cond_jump.target);
            fprintf(out, "\n");
            break;

        case IR_RETURN:
            fprintf(out, "RETURN ");
            print_operand(out, p->u.ret.ret_val);
            fprintf(out, "\n");
            break;

        case IR_DEC:
            fprintf(out, "DEC ");
            print_operand(out, p->u.dec.var);
            fprintf(out, " %d\n", p->u.dec.size);
            break;

        case IR_ARG:
            fprintf(out, "ARG ");
            print_operand(out, p->u.arg.arg);
            fprintf(out, "\n");
            break;

        case IR_CALL:
            print_operand(out, p->u.call.result);
            fprintf(out, " := CALL ");
            print_operand(out, p->u.call.func);
            fprintf(out, "\n");
            break;

        case IR_PARAM:
            fprintf(out, "PARAM ");
            print_operand(out, p->u.param.param);
            fprintf(out, "\n");
            break;

        case IR_READ:
            fprintf(out, "READ ");
            print_operand(out, p->u.io.var);
            fprintf(out, "\n");
            break;

        case IR_WRITE:
            fprintf(out, "WRITE ");
            print_operand(out, p->u.io.var);
            fprintf(out, "\n");
            break;

        default:
            /* 不应该出现的类型 */
            break;
        }
    }
}

/* 释放整个 IRList
 * 注意：这里为了避免 double free，不释放 Operand 内存，
 * 只释放 IRNode 本身。程序结束时由 OS 回收。
 */
void irlist_free(IRList list) {
    IRNode p = list.head;
    while (p) {
        IRNode next = p->next;
        free(p);
        p = next;
    }
}
