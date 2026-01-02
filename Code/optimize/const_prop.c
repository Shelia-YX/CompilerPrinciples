// const_prop.c
#include "../IR/ir.h"
#include "read.h"
#include <stdlib.h>
#include <string.h>

/*
 * 一个非常轻量的“常量表”：
 * 只记录：变量/临时变量 -> 常量(#k)
 * 为了简单与稳妥：不做跨基本块传播
 */

typedef struct ConstEntry_ {
    OperandKind kind; // OP_VARIABLE or OP_TEMP
    int id;           // var_no
    int value;        // constant value
    struct ConstEntry_* next;
} ConstEntry;

static ConstEntry* g_const_map = NULL;

static void constmap_clear(void) {
    ConstEntry* p = g_const_map;
    while (p) {
        ConstEntry* nxt = p->next;
        free(p);
        p = nxt;
    }
    g_const_map = NULL;
}

static int operand_is_trackable(Operand op) {
    return op && (op->kind == OP_VARIABLE || op->kind == OP_TEMP);
}

static ConstEntry* constmap_find(Operand op) {
    if (!operand_is_trackable(op)) return NULL;
    for (ConstEntry* p = g_const_map; p; p = p->next) {
        if (p->kind == op->kind && p->id == op->u.var_no) return p;
    }
    return NULL;
}

static void constmap_kill(Operand op) {
    if (!operand_is_trackable(op)) return;
    ConstEntry **pp = &g_const_map;
    while (*pp) {
        ConstEntry* cur = *pp;
        if (cur->kind == op->kind && cur->id == op->u.var_no) {
            *pp = cur->next;
            free(cur);
            return;
        }
        pp = &((*pp)->next);
    }
}

static void constmap_set(Operand op, int value) {
    if (!operand_is_trackable(op)) return;
    ConstEntry* e = constmap_find(op);
    if (!e) {
        e = (ConstEntry*)malloc(sizeof(ConstEntry));
        e->kind = op->kind;
        e->id   = op->u.var_no;
        e->next = g_const_map;
        g_const_map = e;
    }
    e->value = value;
}

static int operand_is_constant(Operand op, int* out_val) {
    if (!op) return 0;
    if (op->kind == OP_CONSTANT) {
        if (out_val) *out_val = op->u.value;
        return 1;
    }
    ConstEntry* e = constmap_find(op);
    if (e) {
        if (out_val) *out_val = e->value;
        return 1;
    }
    return 0;
}

// 如果 op 在常量表里，返回一个新常量 operand；否则返回原 op
static Operand replace_with_const_if_known(Operand op) {
    int v;
    if (operand_is_constant(op, &v)) {
        return op_constant(v);
    }
    return op;
}

static int eval_binop(IRKind kind, int a, int b, int* out) {
    switch (kind) {
        case IR_ADD: *out = a + b; return 1;
        case IR_SUB: *out = a - b; return 1;
        case IR_MUL: *out = a * b; return 1;
        case IR_DIV:
            if (b == 0) return 0; // 避免编译期除0，保守不折叠
            *out = a / b; return 1;
        default:
            return 0;
    }
}

/*
 * 对一个 IRList 做“局部常量传播 + 常量折叠”
 * - 仅保证在基本块内传播（遇到控制流边界就清空表）
 */
void const_propagation_local(IRList* list) {
    if (!list || !list->head) return;

    constmap_clear();

    for (IRNode p = list->head; p; p = p->next) {
        switch (p->kind) {

            // ---------- 控制流边界：保守清空 ----------
            case IR_FUNCTION:
            case IR_LABEL:
            case IR_GOTO:
            case IR_IF:
            case IR_RETURN:
                constmap_clear();
                break;

            // ---------- READ x ：x 变为未知 ----------
            case IR_READ:
                constmap_kill(p->u.io.var);
                break;

            // ---------- x := y ----------
            case IR_ASSIGN: {
                Operand x = p->u.assign.left;
                Operand y = p->u.assign.right;

                // 右侧若已知常量，则替换为常量并记录
                int val;
                if (operand_is_constant(y, &val)) {
                    p->u.assign.right = op_constant(val);
                    constmap_set(x, val);
                } else {
                    // y 不是常量：尝试用常量表替换一次（例如 y 是 v1 但 v1=常量）
                    Operand y2 = replace_with_const_if_known(y);
                    p->u.assign.right = y2;

                    if (y2->kind == OP_CONSTANT) {
                        constmap_set(x, y2->u.value);
                    } else {
                        // x := 非常量，x 变为未知
                        constmap_kill(x);
                    }
                }
                break;
            }

            // ---------- x := y op z ----------
            case IR_ADD:
            case IR_SUB:
            case IR_MUL:
            case IR_DIV: {
                Operand x  = p->u.binop.result;
                Operand y  = p->u.binop.op1;
                Operand z  = p->u.binop.op2;

                // 尝试把 y、z 替换为常量
                Operand y2 = replace_with_const_if_known(y);
                Operand z2 = replace_with_const_if_known(z);
                p->u.binop.op1 = y2;
                p->u.binop.op2 = z2;

                int a, b, out;
                if (y2->kind == OP_CONSTANT && z2->kind == OP_CONSTANT &&
                    eval_binop(p->kind, y2->u.value, z2->u.value, &out)) {

                    // 折叠：把这条 binop 直接改写成 ASSIGN
                    p->kind = IR_ASSIGN;
                    p->u.assign.left  = x;
                    p->u.assign.right = op_constant(out);

                    constmap_set(x, out);
                } else {
                    // 结果不是常量，kill 掉 x
                    constmap_kill(x);
                }
                break;
            }

            // ---------- x := &y / x := *y / *x := y / x := CALL f ----------
            // 这些涉及地址/内存/调用，先保守：kill 结果，必要时清空表
            case IR_ADDR:
                constmap_kill(p->u.addr.result);
                break;

            case IR_LOAD:
                constmap_kill(p->u.load.result);
                break;

            case IR_STORE:
                // 写内存可能影响别名：保守清空
                constmap_clear();
                break;

            case IR_CALL:
                // 调用可能改全局/别名：保守清空；结果也未知
                constmap_clear();
                constmap_kill(p->u.call.result);
                break;

            // ---------- 其他：先不动 ----------
            default:
                break;
        }
    }

    constmap_clear();
}
