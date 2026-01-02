#include <stdio.h>
#include "read.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int_suffix(const char* s) {
    // 从 label12 / v3 / t7 里抽出数字部分
    while (*s && !isdigit((unsigned char)*s)) s++;
    return (*s) ? atoi(s) : 0;
}

// ------------------ relop 解析 ------------------

static RelopKind parse_relop(const char* r) {
    if (strcmp(r, "==") == 0) return RELOP_EQ;
    if (strcmp(r, "!=") == 0) return RELOP_NE;
    if (strcmp(r, "<")  == 0) return RELOP_LT;
    if (strcmp(r, ">")  == 0) return RELOP_GT;
    if (strcmp(r, "<=") == 0) return RELOP_LE;
    if (strcmp(r, ">=") == 0) return RELOP_GE;
    // 默认给一个值，最好你在调试时直接报错
    return RELOP_EQ;
}

// ------------------ operand 解析 ------------------

static Operand parse_operand_tok(const char* tok) {
    // 注意：tok 可能是 "&v6" "*t7" "#5" "v1" "t3" "label2" "main"
    if (tok[0] == '#') {
        return op_constant(atoi(tok + 1));
    }
    if (tok[0] == '&') {
        Operand base = parse_operand_tok(tok + 1);
        return op_address(base);
    }
    if (tok[0] == '*') {
        // 你的 OperandKind 没有 OP_DEREF，这里把 "*t7" 当成 ptr 操作数（t7）
        return parse_operand_tok(tok + 1);
    }
    if (tok[0] == 'v' && isdigit((unsigned char)tok[1])) {
        return op_variable(atoi(tok + 1));
    }
    if (tok[0] == 't' && isdigit((unsigned char)tok[1])) {
        return op_temp(atoi(tok + 1));
    }
    if (strncmp(tok, "label", 5) == 0) {
        return op_label(parse_int_suffix(tok));
    }
    // 其他情况：函数名
    // 注意：op_function(char* name) 通常会在内部 strdup；
    // 如果你内部没 strdup，那你就需要在这里 strdup 一下。
    return op_function((char*)tok);
}


// ------------------ 一行 IR 解析 ------------------

static IRNode parse_ir_line(const char* s) {
    char a[128], b[128], c[128], d[128];

    // FUNCTION f :
    if (sscanf(s, "FUNCTION %127s :", a) == 1) {
        return ir_function(op_function(a));
    }

    // LABEL label1 :
    if (sscanf(s, "LABEL %127s :", a) == 1) {
        return ir_label(parse_operand_tok(a));   // label1 -> op_label(1)
    }

    // GOTO label1
    if (sscanf(s, "GOTO %127s", a) == 1) {
        return ir_goto(parse_operand_tok(a));
    }

    // RETURN x
    if (sscanf(s, "RETURN %127s", a) == 1) {
        return ir_return(parse_operand_tok(a));
    }

    // READ x
    if (sscanf(s, "READ %127s", a) == 1) {
        return ir_read(parse_operand_tok(a));
    }

    // WRITE x
    if (sscanf(s, "WRITE %127s", a) == 1) {
        return ir_write(parse_operand_tok(a));
    }

    // DEC v6 40
    if (sscanf(s, "DEC %127s %127s", a, b) == 2) {
        return ir_dec(parse_operand_tok(a), atoi(b));
    }

    // ARG x
    if (sscanf(s, "ARG %127s", a) == 1) {
        return ir_arg(parse_operand_tok(a));
    }

    // PARAM x
    if (sscanf(s, "PARAM %127s", a) == 1) {
        return ir_param(parse_operand_tok(a));
    }

    // IF x relop y GOTO label
    // 例：IF v8 < v1 GOTO label2
    if (sscanf(s, "IF %127s %127s %127s GOTO %127s", a, b, c, d) == 4) {
        Operand op1 = parse_operand_tok(a);
        Operand op2 = parse_operand_tok(c);
        Operand tgt = parse_operand_tok(d);   // label
        return ir_if(op1, parse_relop(b), op2, tgt);
    }

    // *x := y    (STORE)
    // 例：*t7 := v1
    if (sscanf(s, "*%127s := %127s", a, b) == 2) {
        Operand ptr = parse_operand_tok(a);
        Operand val = parse_operand_tok(b);
        return ir_store(ptr, val);
    }

    // x := CALL f
    if (sscanf(s, "%127s := CALL %127s", a, b) == 2) {
        Operand res  = parse_operand_tok(a);
        Operand func = op_function(b);
        return ir_call(res, func);
    }

    // x := y op z   (ADD/SUB/MUL/DIV)
    // 例：t3 := v1 + #2
    if (sscanf(s, "%127s := %127s %127s %127s", a, b, c, d) == 4) {
        Operand dst = parse_operand_tok(a);
        Operand op1 = parse_operand_tok(b);
        Operand op2 = parse_operand_tok(d);

        IRKind k;
        if      (strcmp(c, "+") == 0) k = IR_ADD;
        else if (strcmp(c, "-") == 0) k = IR_SUB;
        else if (strcmp(c, "*") == 0) k = IR_MUL;
        else if (strcmp(c, "/") == 0) k = IR_DIV;
        else return NULL;

        return ir_binop(k, dst, op1, op2);
    }

    // x := &y
    if (sscanf(s, "%127s := &%127s", a, b) == 2) {
        Operand res = parse_operand_tok(a);
        Operand var = parse_operand_tok(b);
        return ir_addr(res, var);
    }

    // x := *y   (LOAD)
    if (sscanf(s, "%127s := *%127s", a, b) == 2) {
        Operand res = parse_operand_tok(a);
        Operand ptr = parse_operand_tok(b);
        return ir_load(res, ptr);
    }


    // x := y   (ASSIGN)
    if (sscanf(s, "%127s := %127s", a, b) == 2) {
        return ir_assign(parse_operand_tok(a), parse_operand_tok(b));
    }

    // 解析失败：你可以选择 NULL 或者直接报错退出
    return NULL;
}


IRList ir_parse_file(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        perror("ir_parse_file fopen");
        return irlist_create();
    }

    IRList list = irlist_create();
    char buf[1024];

    while (fgets(buf, sizeof(buf), fp)) {
        IRNode node = parse_ir_line(buf);
        if (node) irlist_append(&list, node);
        // else: 你也可以打印 warning 方便调试
        // fprintf(stderr, "WARN: cannot parse: %s\n", s);
    }

    fclose(fp);
    return list;
}