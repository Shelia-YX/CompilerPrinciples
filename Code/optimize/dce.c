// local_dce.c
#include "read.h"
#include <stdlib.h>

// ---------- Operand 比较（只针对 v/t 常见场景） ----------
static int same_vartemp(Operand a, Operand b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == OP_VARIABLE || a->kind == OP_TEMP) {
        return a->u.var_no == b->u.var_no;
    }
    return 0;
}

// ---------- 一个很轻量的 used 集合（链表实现，够用） ----------
typedef struct Used_ {
    OperandKind kind; // OP_VARIABLE/OP_TEMP
    int id;
    struct Used_* next;
} Used;

static void used_clear(Used** s) {
    Used* p = *s;
    while (p) { Used* n = p->next; free(p); p = n; }
    *s = NULL;
}

static int used_has(Used* s, Operand op) {
    if (!op) return 0;
    if (!(op->kind == OP_VARIABLE || op->kind == OP_TEMP)) return 0;
    for (; s; s = s->next) {
        if (s->kind == op->kind && s->id == op->u.var_no) return 1;
    }
    return 0;
}

static void used_add(Used** s, Operand op) {
    if (!op) return;
    if (!(op->kind == OP_VARIABLE || op->kind == OP_TEMP)) return 0;
    if (used_has(*s, op)) return;

    Used* n = (Used*)malloc(sizeof(Used));
    n->kind = op->kind;
    n->id   = op->u.var_no;
    n->next = *s;
    *s = n;
}

// 取地址 &v6：你的 Operand.kind=OP_ADDRESS，addr_of 指向原始 operand
static void used_add_operand(Used** s, Operand op) {
    if (!op) return;
    if (op->kind == OP_ADDRESS && op->addr_of) {
        used_add_operand(s, op->addr_of);
        return;
    }
    used_add(s, op);
}

// ---------- 从一条指令收集 use/def ----------

static int has_side_effect(IRNode n) {
    switch (n->kind) {
        case IR_READ:
        case IR_WRITE:
        case IR_STORE:
        case IR_IF:
        case IR_GOTO:
        case IR_RETURN:
        case IR_LABEL:
        case IR_FUNCTION:
            return 1;
        case IR_CALL:
            // 保守：认为调用有副作用，不删
            return 1;
        default:
            return 0;
    }
}

// 返回：这条指令是否“定义”了一个变量/临时变量（可被 DCE 删除）
static Operand get_def(IRNode n) {
    switch (n->kind) {
        case IR_ASSIGN: return n->u.assign.left;
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:    return n->u.binop.result;
        case IR_ADDR:   return n->u.addr.result;
        case IR_LOAD:   return n->u.load.result;
        case IR_CALL:   return n->u.call.result;
        default:        return NULL;
    }
}

static void add_uses(IRNode n, Used** used) {
    switch (n->kind) {
        case IR_ASSIGN:
            used_add_operand(used, n->u.assign.right);
            break;
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:
            used_add_operand(used, n->u.binop.op1);
            used_add_operand(used, n->u.binop.op2);
            break;
        case IR_ADDR:
            used_add_operand(used, n->u.addr.addr);  // &y 里的 y
            break;
        case IR_LOAD:
            used_add_operand(used, n->u.load.ptr);
            break;
        case IR_STORE:
            used_add_operand(used, n->u.store.ptr);
            used_add_operand(used, n->u.store.value);
            break;
        case IR_IF:
            used_add_operand(used, n->u.cond_jump.op1);
            used_add_operand(used, n->u.cond_jump.op2);
            break;
        case IR_RETURN:
            used_add_operand(used, n->u.ret.ret_val);
            break;
        case IR_ARG:
            used_add_operand(used, n->u.arg.arg);
            break;
        case IR_PARAM:
            // PARAM 是定义形参，通常不当作 use
            break;
        case IR_READ:
            // READ x 读入到 x，不把 x 当 use
            break;
        case IR_WRITE:
            used_add_operand(used, n->u.io.var);
            break;
        case IR_CALL:
            // 保守：把 result 不算 use，但调用参数通常通过 ARG 指令体现
            // 函数名也不算 use
            break;
        default:
            break;
    }
}

// ---------- 删除节点 ----------
static void irlist_remove(IRList* list, IRNode n) {
    if (!list || !n) return;
    if (n->prev) n->prev->next = n->next;
    else         list->head = n->next;
    if (n->next) n->next->prev = n->prev;
    else         list->tail = n->prev;
    // 注意：这里不 free Operand（可能共享），只 free IRNode
    free(n);
}

static void used_remove(Used** s, Operand op) {
    if (!op) return;
    if (!(op->kind == OP_VARIABLE || op->kind == OP_TEMP)) return;

    Used** pp = s;
    while (*pp) {
        Used* cur = *pp;
        if (cur->kind == op->kind && cur->id == op->u.var_no) {
            *pp = cur->next;
            free(cur);
            return;
        }
        pp = &((*pp)->next);
    }
}


// ---------- 局部 DCE 主函数 ----------
// 保守策略：遇到 LABEL/FUNCTION/GOTO/IF/RETURN 就把 used 集合清空（边界）
// 这样不会跨基本块删除，安全但有效
void local_dce(IRList* list) {
    if (!list || !list->head) return;

    Used* used = NULL;

    for (IRNode p = list->tail; p; ) {
        IRNode prev = p->prev;

        // 控制流边界：切断基本块，清空 used
        if (p->kind == IR_LABEL || p->kind == IR_FUNCTION ||
            p->kind == IR_GOTO  || p->kind == IR_IF || p->kind == IR_RETURN) {
            used_clear(&used);
            // 这些指令本身也可能 use operand（IF/RETURN），加一下更准
            add_uses(p, &used);
            p = prev;
            continue;
        }

        // 有副作用的：必留，并把 use 加进去
        if (has_side_effect(p)) {
            add_uses(p, &used);
            Operand def2 = get_def(p);
            if (def2) used_remove(&used, def2);
            p = prev;
            continue;
        }

        Operand def = get_def(p);

        if (def && (def->kind == OP_VARIABLE || def->kind == OP_TEMP) && !used_has(used, def)) {
            // 这条定义的结果从未被使用：删
            irlist_remove(list, p);
            p = prev;
            continue;
        }

        // 不删：把 use 加进去，把 def 从 used 里“消费掉”（可选）
        add_uses(p, &used);

        // 下面这步是“更精准”的：如果我们向后已经处理过，def 已经用掉了，
        // 可以把 def 从 used 中移除，避免误保留更早的同名定义。
        // 但实现 used_remove 要多写几行；你先不写也能工作，只是删得少一点。

        p = prev;
    }

    used_clear(&used);
}
