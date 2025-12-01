#ifndef IR_H
#define IR_H

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 操作数类型 */
typedef enum {
    OP_VARIABLE,    // 变量 v1, v2, ...
    OP_TEMP,        // 临时变量 t1, t2, ...  
    OP_CONSTANT,    // 常数 #5, #0, ...
    OP_LABEL,       // 标签 label1, label2, ...
    OP_FUNCTION,    // 函数名 main, fact, ...
    OP_ADDRESS      // 地址 &x
} OperandKind;

/* 操作数结构 */
typedef struct Operand_ {
    OperandKind kind;
    union {
        int var_no;     // 变量/临时变量编号
        int value;      // 常数值
        char* name;     // 函数名/标签名
    } u;
} *Operand;

/* 中间代码类型 */
typedef enum {
    IR_LABEL,       // LABEL x:
    IR_FUNCTION,    // FUNCTION f:
    IR_ASSIGN,      // x := y
    IR_ADD,         // x := y + z
    IR_SUB,         // x := y - z  
    IR_MUL,         // x := y * z
    IR_DIV,         // x := y / z
    IR_ADDR,        // x := &y
    IR_LOAD,        // x := *y
    IR_STORE,       // *x := y
    IR_GOTO,        // GOTO x
    IR_IF,          // IF x [relop] y GOTO z
    IR_RETURN,      // RETURN x
    IR_DEC,         // DEC x [size]
    IR_ARG,         // ARG x
    IR_CALL,        // x := CALL f
    IR_PARAM,       // PARAM x
    IR_READ,        // READ x
    IR_WRITE        // WRITE x
} IRKind;

/* 关系操作符 */
typedef enum {
    RELOP_EQ,       // ==
    RELOP_NE,       // !=  
    RELOP_LT,       // <
    RELOP_GT,       // >
    RELOP_LE,       // <=
    RELOP_GE        // >=
} RelopKind;

/* 中间代码节点 */
typedef struct IRNode_ {
    IRKind kind;
    union {
        struct { Operand op; } label;          // LABEL x
        struct { Operand func; } function;     // FUNCTION f
        struct { Operand left, right; } assign; // x := y
        struct { Operand result, op1, op2; } binop; // x := y + z
        struct { Operand result, addr; } addr; // x := &y
        struct { Operand result, ptr; } load;  // x := *y  
        struct { Operand ptr, value; } store;  // *x := y
        struct { Operand target; } jump;       // GOTO x
        struct { Operand op1, op2, target; RelopKind relop; } cond_jump; // IF x op y GOTO z
        struct { Operand ret_val; } ret;       // RETURN x
        struct { Operand var; int size; } dec; // DEC x size
        struct { Operand arg; } arg;           // ARG x
        struct { Operand result, func; } call; // x := CALL f
        struct { Operand param; } param;       // PARAM x
        struct { Operand var; } io;            // READ x / WRITE x
    } u;
    struct IRNode_ *prev, *next;
} *IRNode;

/* 中间代码链表 */
typedef struct {
    IRNode head, tail;
} IRList;

/* 操作数构造函数 */
Operand op_variable(int no);
Operand op_temp(int no);
Operand op_constant(int value);
Operand op_label(int no);
Operand op_function(char* name);
Operand op_address(Operand var);

/* 中间代码构造函数 */
IRNode ir_label(Operand label);
IRNode ir_function(Operand func);
IRNode ir_assign(Operand left, Operand right);
IRNode ir_binop(IRKind kind, Operand result, Operand op1, Operand op2);
IRNode ir_addr(Operand result, Operand var);
IRNode ir_load(Operand result, Operand ptr);
IRNode ir_store(Operand ptr, Operand value);
IRNode ir_goto(Operand target);
IRNode ir_if(Operand op1, RelopKind relop, Operand op2, Operand target);
IRNode ir_return(Operand ret_val);
IRNode ir_dec(Operand var, int size);
IRNode ir_arg(Operand arg);
IRNode ir_call(Operand result, Operand func);
IRNode ir_param(Operand param);
IRNode ir_read(Operand var);
IRNode ir_write(Operand var);

/* 链表操作 */
IRList irlist_create(void);
void irlist_append(IRList* list, IRNode node);
void irlist_concat(IRList* list1, IRList list2);
void irlist_print(FILE* out, IRList list);
void irlist_free(IRList list);

#ifdef __cplusplus
}
#endif

#endif // IR_H