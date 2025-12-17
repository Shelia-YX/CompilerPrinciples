#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include "./IR/ir.h"
#include "codegen.h"

/* -----------------------------
 *  工具函数
 * ----------------------------- */

// 输出一行汇编
static FILE* OUT = NULL;

static void emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(OUT, fmt, ap);
    fprintf(OUT, "\n");
    va_end(ap);
}

// 输出文件开头通用内容
static void emit_prelude(void) {
    emit(".data");
    emit("_prompt: .asciiz \"Enter an integer:\"");
    emit("_ret: .asciiz \"\\n\"");
    emit(".globl main");
    emit(".text");

    emit("read:");
    emit("  li $v0, 4");
    emit("  la $a0, _prompt");
    emit("  syscall");
    emit("  li $v0, 5");
    emit("  syscall");
    emit("  jr $ra");

    emit("write:");
    emit("  li $v0, 1");
    emit("  syscall");
    emit("  li $v0, 4");
    emit("  la $a0, _ret");
    emit("  syscall");
    emit("  move $v0, $0");
    emit("  jr $ra");
}

static int label_id(Operand op) {
    assert(op && op->kind == OP_LABEL);
    return op->u.var_no; // 你现在 label 用 var_no
}

static const char* relop_to_branch(RelopKind r) {
    switch (r) {
        case RELOP_EQ: return "beq";
        case RELOP_NE: return "bne";
        case RELOP_LT: return "blt";
        case RELOP_GT: return "bgt";
        case RELOP_LE: return "ble";
        case RELOP_GE: return "bge";
        default: assert(0 && "unknown relop");
    }
    return "beq";
}

/* -----------------------------
 *  Operand -> offset 映射（朴素实现）
 * ----------------------------- */

typedef struct {
    int key;
    int offset; // bytes
} OffsetEntry;

typedef struct {
    OffsetEntry* data;
    int size;
    int cap;
} OffsetMap;

static void omap_init(OffsetMap* m) {
    m->size = 0;
    m->cap = 128;
    m->data = (OffsetEntry*)malloc(sizeof(OffsetEntry) * m->cap);
}

static void omap_free(OffsetMap* m) {
    free(m->data);
    m->data = NULL;
    m->size = m->cap = 0;
}

static int operand_key(Operand op) {
    if (!op) return -1;
    switch (op->kind) {
        case OP_VARIABLE: return 100000 + op->u.var_no;
        case OP_TEMP:     return 200000 + op->u.var_no;
        // 常数/标签/函数名不需要 offset
        default:          return -1;
    }
}

static int omap_get(OffsetMap* m, int key, int* found) {
    for (int i = 0; i < m->size; i++) {
        if (m->data[i].key == key) {
            *found = 1;
            return m->data[i].offset;
        }
    }
    *found = 0;
    return 0;
}

static void omap_put(OffsetMap* m, int key, int offset) {
    // 不重复插入
    int f = 0;
    (void)omap_get(m, key, &f);
    if (f) return;

    if (m->size == m->cap) {
        m->cap *= 2;
        m->data = (OffsetEntry*)realloc(m->data, sizeof(OffsetEntry) * m->cap);
    }
    m->data[m->size].key = key;
    m->data[m->size].offset = offset;
    m->size++;
}

/* -----------------------------
 *  函数级上下文：offset表 + frame_size + ARG缓冲
 * ----------------------------- */

typedef struct {
    OffsetMap off;
    int frame_size;      // bytes
    // ARG 缓冲（你例子一个参数就够，这里做通用一点）
    Operand args[32];
    int arg_cnt;
    int param_idx;
} FuncCtx;

static void fctx_init(FuncCtx* ctx) {
    omap_init(&ctx->off);
    ctx->frame_size = 0;
    ctx->arg_cnt = 0;
    ctx->param_idx = 0;
}

static void fctx_free(FuncCtx* ctx) {
    omap_free(&ctx->off);
}

/* -----------------------------
 *  分配 offset：变量/临时变量占4字节；DEC占size字节
 *  规则：遇到某个变量/临时变量第一次出现 -> 分配
 * ----------------------------- */

static void ensure_slot(FuncCtx* ctx, Operand op, int bytes) {
    int key = operand_key(op);
    if (key < 0) return;
    int found = 0;
    (void)omap_get(&ctx->off, key, &found);
    if (found) return;

    // 为该变量分配 offset（从0开始递增）
    int off = ctx->frame_size;
    omap_put(&ctx->off, key, off);
    ctx->frame_size += bytes;
    ctx->frame_size = (ctx->frame_size + 3) / 4 * 4;
}

// 变量/临时默认4字节的快速调用
static void ensure_i32(FuncCtx* ctx, Operand op) {
    ensure_slot(ctx, op, 4);
}

// 获取 offset（必须已分配）
static int get_off(FuncCtx* ctx, Operand op) {
    int key = operand_key(op);
    assert(key >= 0);
    int found = 0;
    int off = omap_get(&ctx->off, key, &found);
    assert(found && "offset not allocated for operand");
    return off + 8;
}

/* -----------------------------
 *  load/store 辅助：把 operand 的值放到某个寄存器
 *  - 常数：li reg, imm
 *  - 变量/临时：lw reg, off(op)($sp)
 * ----------------------------- */
static void load_to_reg(FuncCtx* ctx, Operand op, const char* reg) {
    if (!op) return;
    if (op->kind == OP_CONSTANT) {
        emit("  li %s, %d", reg, op->u.value);
    } 
    else if (op->kind == OP_VARIABLE || op->kind == OP_TEMP) {
        int off = get_off(ctx, op);
        if (off % 4 != 0) {
            fprintf(stderr,"[ALIGN ERROR][LOAD] %d offset=%d (not multiple of 4)\n", op->kind, off);
            exit(1);
        }
        emit("  lw %s, %d($fp)", reg, off);
    }
    else if (op->kind == OP_ADDRESS) {
        Operand base = op->addr_of;
        if (!base){// 兜底 
            // base = op_variable(op->u.var_no); 
            fprintf(stderr, "[ADDR ERROR] OP_ADDRESS without base\n");
            exit(1);
        }
        int off = get_off(ctx, base);
        if (off % 4 != 0) {
            fprintf(stderr,"[ALIGN ERROR][ADDR] base_kind=%d off=%d\n",base->kind, off);
            exit(1);
        }
        emit("  addi %s, $fp, %d", reg, off);
    }
    else {
        // label/function/address 本身不应该出现在“求值”位置（你的 IR 里不常见）
        assert(0 && "unsupported operand kind in load_to_reg");
    }
}

static void store_from_reg(FuncCtx* ctx, Operand dst, const char* reg) {
    assert(dst && (dst->kind == OP_VARIABLE || dst->kind == OP_TEMP));
    int off = get_off(ctx, dst);
    if (off % 4 != 0) {
        fprintf(stderr,"[ALIGN ERROR][STORE] %d offset=%d (not multiple of 4)\n",dst->kind, off);
        exit(1);
    }
    emit("  sw %s, %d($fp)", reg, off);
}

/* -----------------------------
 *  翻译IR（朴素寄存器分配）
 * ----------------------------- */

static void gen_ir(FuncCtx* ctx, IRNode n) {
    switch (n->kind) {
        case IR_LABEL:
            // 你这里 label operand 可能用 name 或编号，自行统一输出名
            // 如果你的 label 是 OP_LABEL 且用 var_no 编号：
            emit("label%d:", n->u.label.op->u.var_no);
            break;

        case IR_FUNCTION:
            // 在外层处理
            break;

        case IR_PARAM: {
            Operand v = n->u.param.param;
            int i = ctx->param_idx++;
            if (i < 4) {
                const char* areg[] = {"$a0", "$a1", "$a2", "$a3"};
                emit("  sw %s, %d($fp)", areg[i], get_off(ctx, v));
            } 
            else {
                int src = (ctx->frame_size + 8) + 16 + (i - 4) * 4;
                emit("  lw $t0, %d($fp)", src);
                emit("  sw $t0, %d($fp)", get_off(ctx, v));
            }
            break;
        }

        case IR_DEC:
            // DEC 只是“分配空间”，栈帧已经在函数入口一次性 addi 了，这里不输出指令
            break;

        case IR_ASSIGN: {
            Operand x = n->u.assign.left;
            Operand y = n->u.assign.right;
            load_to_reg(ctx, y, "$t0");
            store_from_reg(ctx, x, "$t0");
            break;
        }

        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: {
            Operand x = n->u.binop.result;
            Operand y = n->u.binop.op1;
            Operand z = n->u.binop.op2;

            load_to_reg(ctx, y, "$t0");

            // 常数加法可以用 addi；但为了稳，你也可以统一先li到$t1再add
            if (z->kind == OP_CONSTANT && n->kind == IR_ADD) {
                emit("  addi $t2, $t0, %d", z->u.value);
            } 
            else {
                load_to_reg(ctx, z, "$t1");
                if (n->kind == IR_ADD) emit("  add $t2, $t0, $t1");
                if (n->kind == IR_SUB) emit("  sub $t2, $t0, $t1");
                if (n->kind == IR_MUL) emit("  mul $t2, $t0, $t1");
                if (n->kind == IR_DIV) {
                    // 简化：用 div + mflo
                    emit("  div $t0, $t1");
                    emit("  mflo $t2");
                }
            }
            store_from_reg(ctx, x, "$t2");
            break;
        }

        case IR_ADDR: {
            // x := &y   （你结构里叫 result, addr）
            Operand x = n->u.addr.result;
            Operand y = n->u.addr.addr;
            // y 必须是变量/临时（或DEC出来的v）
            int off_y = get_off(ctx, y);
            emit("  addi $t0, $fp, %d", off_y);
            store_from_reg(ctx, x, "$t0");
            break;
        }

        case IR_LOAD: {
            // x := *p
            Operand x = n->u.load.result;
            Operand p = n->u.load.ptr;
            // $t0 = p (addr)
            load_to_reg(ctx, p, "$t0");
            // $t1 = *p
            emit("  lw $t1, 0($t0)");
            store_from_reg(ctx, x, "$t1");
            break;
        }

        case IR_STORE: {
            // *p := v
            Operand p = n->u.store.ptr;
            Operand v = n->u.store.value;
            load_to_reg(ctx, p, "$t0");     // $t0 = addr
            load_to_reg(ctx, v, "$t1");     // $t1 = value
            emit("  sw $t1, 0($t0)");
            break;
        }

        case IR_ARG: {
            // 收集参数，遇到 CALL 时一次性处理
            ctx->args[ctx->arg_cnt++] = n->u.arg.arg;
            break;
        }

        case IR_CALL: {
            const char* areg[] = {"$a0", "$a1", "$a2", "$a3"};

            int argc = ctx->arg_cnt;
            int extra = (argc > 4) ? (argc - 4) : 0;
            // 分配 argument area：16字节(给a0~a3 shadow) + 额外参数
            int arg_area = 16 + extra * 4;
            emit("  addi $sp, $sp, -%d", arg_area);

            for (int i = 0; i < argc; i++) {
                Operand temp = ctx->args[ctx->arg_cnt - 1 - i];
                load_to_reg(ctx, temp, "$t0");
                if(i < 4){
                    emit("  move %s, $t0", areg[i]);
                }
                else{
                    emit("  sw $t0, %d($sp)", 16 + (i - 4) * 4);
                }
            }

            ctx->arg_cnt = 0;

            // 3) jal func
            // func operand: OP_FUNCTION name
            emit("  jal %s", n->u.call.func->u.name);

            // caller 清理参数区
            emit("  addi $sp, $sp, %d", arg_area);

            // 5) 保存返回值到 result
            if (n->u.call.result != NULL) {
                store_from_reg(ctx, n->u.call.result, "$v0");
            }
            break;
        }

        case IR_WRITE: {
            // WRITE x：把x放$a0，然后jal write
            load_to_reg(ctx, n->u.io.var, "$a0");

            emit("  jal write");
            break;
        }

        case IR_READ: {
            Operand x = n->u.io.var;          // READ x
            emit("  jal read");
            store_from_reg(ctx, x, "$v0");    // sw $v0, off(x)($sp)
            break;
        }

        case IR_RETURN: {
            // RETURN x
            load_to_reg(ctx, n->u.ret.ret_val, "$t0");
            emit("  move $v0, $t0");
            // 函数尾部的栈恢复在 gen_function 里统一做（但这里可能有多 return）
            // 方案：这里直接做 epilogue（更稳）
            emit("  move $sp, $fp");
            emit("  lw $fp, 0($sp)");
            emit("  lw $ra, 4($sp)");
            emit("  addi $sp, $sp, %d", ctx->frame_size + 8);
            emit("  jr $ra");

            break;
        }

        case IR_GOTO: {
            Operand t = n->u.jump.target;
            emit("  j label%d", label_id(t));
            break;
        }

        case IR_IF: {
            Operand x = n->u.cond_jump.op1;
            Operand y = n->u.cond_jump.op2;
            Operand t = n->u.cond_jump.target;
            RelopKind r = n->u.cond_jump.relop;
            // 把 x,y 的值装到寄存器
            load_to_reg(ctx, x, "$t0");
            load_to_reg(ctx, y, "$t1");
            // 生成对应分支
            const char* br = relop_to_branch(r);
            emit("  %s $t0, $t1, label%d", br, label_id(t));
            break;
        }

        default:
            assert(0 && "IR kind not supported yet");
            break;
    }
}

/* -----------------------------
 *  扫描一个函数块，建立offset表并计算frame_size
 *  输入：从 func_node 开始（它是 IR_FUNCTION），扫描到下一个 IR_FUNCTION 或 NULL
 * ----------------------------- */

static IRNode scan_function_build_frame(FuncCtx* ctx, IRNode func_node) {
    assert(func_node && func_node->kind == IR_FUNCTION);

    // 先清空 frame 相关信息
    ctx->frame_size = 0;
    ctx->arg_cnt = 0;
    // 重新初始化 offset map（简单处理：释放再建）
    omap_free(&ctx->off);
    omap_init(&ctx->off);

    IRNode p = func_node->next;
    for (; p != NULL && p->kind != IR_FUNCTION; p = p->next) {
        switch (p->kind) {
            case IR_PARAM:
                // PARAM v1：v1 需要一个槽位（保存$a0等）
                ensure_i32(ctx, p->u.param.param);
                break;

            case IR_DEC:
                // DEC v3 8：v3 占 size 字节（结构体/数组）
                ensure_slot(ctx, p->u.dec.var, p->u.dec.size);
                break;

            case IR_ASSIGN:
                ensure_i32(ctx, p->u.assign.left);
                // right 如果是变量/临时也要分配
                if (p->u.assign.right->kind == OP_VARIABLE || p->u.assign.right->kind == OP_TEMP)
                    ensure_i32(ctx, p->u.assign.right);
                break;

            case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
                ensure_i32(ctx, p->u.binop.result);
                if (p->u.binop.op1->kind == OP_VARIABLE || p->u.binop.op1->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.binop.op1);
                if (p->u.binop.op2->kind == OP_VARIABLE || p->u.binop.op2->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.binop.op2);
                break;

            case IR_ADDR:
                ensure_i32(ctx, p->u.addr.result);
                // 被取地址的 var 必须有槽位（可能是DEC分配的）
                if (p->u.addr.addr->kind == OP_VARIABLE || p->u.addr.addr->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.addr.addr);
                break;

            case IR_LOAD:
                ensure_i32(ctx, p->u.load.result);
                if (p->u.load.ptr->kind == OP_VARIABLE || p->u.load.ptr->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.load.ptr);
                break;

            case IR_STORE:
                if (p->u.store.ptr->kind == OP_VARIABLE || p->u.store.ptr->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.store.ptr);
                if (p->u.store.value->kind == OP_VARIABLE || p->u.store.value->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.store.value);
                break;

            case IR_ARG:
                if (p->u.arg.arg->kind == OP_VARIABLE || p->u.arg.arg->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.arg.arg);
                break;

            case IR_CALL:
                ensure_i32(ctx, p->u.call.result);
                break;

            case IR_RETURN:
                if (p->u.ret.ret_val->kind == OP_VARIABLE || p->u.ret.ret_val->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.ret.ret_val);
                break;

            case IR_WRITE:
                if (p->u.io.var->kind == OP_VARIABLE || p->u.io.var->kind == OP_TEMP) 
                    ensure_i32(ctx, p->u.io.var);
                break;

            case IR_READ:
                ensure_i32(ctx, p->u.io.var);
                break;

            default:
                // 其他 IR（LABEL/GOTO/IF 等）先不影响 frame_size
                break;
        }
    }

    // 可选：对齐到8/16字节（QtSPIM通常不严格，但建议对齐）
    if (ctx->frame_size % 4 != 0) ctx->frame_size = (ctx->frame_size + 3) / 4 * 4;
    return p; // 返回“下一个函数的节点”（可能为NULL）
}

/* -----------------------------
 *  生成一个函数：扫描 frame -> 输出 prologue -> 翻译每条IR
 * ----------------------------- */
static IRNode gen_function(IRList ir, IRNode func_node) {
    FuncCtx ctx;
    fctx_init(&ctx);

    // 1) 扫描计算 frame_size & offsets
    IRNode next_func = scan_function_build_frame(&ctx, func_node);

    // 2) 输出函数标签
    // func operand: OP_FUNCTION name
    emit("%s:", func_node->u.function.func->u.name);

    // 3) Prologue：一次性分配栈帧
    // 额外留 8 字节保存 $fp/$ra
    int fs = ctx.frame_size;
    fs = (fs + 3) / 4 * 4;

    emit("  addi $sp, $sp, -%d", fs + 8);
    emit("  sw $fp, 0($sp)");
    emit("  sw $ra, 4($sp)");
    emit("  move $fp, $sp");


    // 4) 翻译函数体
    for (IRNode p = func_node->next; p != NULL && p->kind != IR_FUNCTION; p = p->next) {
        gen_ir(&ctx, p);
    }

    fctx_free(&ctx);
    return next_func;
}

/* -----------------------------
 *  主入口：输出 builtin + 遍历 IR_FUNCTION
 * ----------------------------- */
void mips_codegen(FILE* out, IRList ir) {
    OUT = out;
    emit_prelude();

    // 遍历 IR，遇到 FUNCTION 就生成
    for (IRNode p = ir.head; p != NULL; ) {
        if (p->kind == IR_FUNCTION) {
            p = gen_function(ir, p);
        } 
        else {
            p = p->next;
        }
    }
}