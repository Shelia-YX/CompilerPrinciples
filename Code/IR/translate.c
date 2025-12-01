#include "translate.h"
#include "temp.h"
#include "ir.h"
#include "../node.h"
#include "../semantic/type.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* 说明：
 * 为了不强依赖你的 symbol_table.h，这里用一个简单的链表把变量名映射到 vX。
 * 这样翻译阶段就能正常产生 IR。
 * 如果你想用自己的符号表，可以把 get_var_operand() 换成“查符号表”的实现。
 */

typedef struct VarBinding_ {
    char*   name;
    Operand op;
    struct VarBinding_* next;
} VarBinding;

static VarBinding* var_table = NULL;

static Operand get_var_operand(const char* name) {
    VarBinding* p = var_table;
    while (p) {
        if (strcmp(p->name, name) == 0) {
            return p->op;
        }
        p = p->next;
    }
    // 没有就新建一个
    Operand v = new_variable();
    VarBinding* nb = (VarBinding*)malloc(sizeof(VarBinding));
    nb->name = xstrdup(name);
    nb->op   = v;
    nb->next = var_table;
    var_table = nb;
    return v;
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

static void print_node_tree(Node* node, int depth);
static void print_operand(FILE* out, Operand op);

/*=====================*
 *   Relop 辅助函数    *
 *=====================*/

static RelopKind relop_from_node(Node* relop_node) {
    /* RELOP 结点的 value 现在包含了运算符字符串 */
    const char* t = NODE_TEXT(relop_node);
    
    // 调试输出
    printf("relop_from_node: node name='%s', value='%s'\n", relop_node->name, t ? t : "NULL");
    
    if (!t) {
        printf("WARNING: RELOP node has NULL value!\n");
        return RELOP_EQ;
    }
    
    printf("  Comparing '%s' to operators...\n", t);
    
    if (strcmp(t, "==") == 0) return RELOP_EQ;
    if (strcmp(t, "!=") == 0) return RELOP_NE;
    if (strcmp(t, "<")  == 0) return RELOP_LT;
    if (strcmp(t, ">")  == 0) return RELOP_GT;
    if (strcmp(t, "<=") == 0) return RELOP_LE;
    if (strcmp(t, ">=") == 0) return RELOP_GE;
    
    printf("WARNING: Unknown relop '%s'\n", t);
    return RELOP_EQ;
}

// static RelopKind relop_from_node(Node* relop_node) {
//     /* 假设 RELOP 结点的 value 是 "==", "!=", "<", ">", "<=", ">=" */
//     const char* t = NODE_TEXT(relop_node);
//     if (!t) return RELOP_EQ;
//     if (strcmp(t, "==") == 0) return RELOP_EQ;
//     if (strcmp(t, "!=") == 0) return RELOP_NE;
//     if (strcmp(t, "<")  == 0) return RELOP_LT;
//     if (strcmp(t, ">")  == 0) return RELOP_GT;
//     if (strcmp(t, "<=") == 0) return RELOP_LE;
//     if (strcmp(t, ">=") == 0) return RELOP_GE;
//     return RELOP_EQ;
// }



/*=====================*
 *   顶层入口函数      *
 *=====================*/

/* Program -> ExtDefList */
IRList translate_Program(Node* root) {
    IRList list = irlist_create();
    if (!root) return list;

    // 一般 root 是 "Program"，child 是 "ExtDefList"
    Node* extDefList = CHILD(root);
    IRList sub = translate_ExtDefList(extDefList);
    irlist_concat(&list, sub);
    return list;
}

/* ExtDefList -> ExtDef ExtDefList | empty */
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

/* ExtDef 主要关心: Specifier FunDec CompSt */
static IRList translate_ExtDef(Node* n) {
    printf("\n=== translate_ExtDef ===\n");
    printf("Node: %p, name=%s\n", (void*)n, n ? n->name : "NULL");
    
    IRList list = irlist_create();
    if (!n) return list;

    // 打印所有子节点
    Node* child = CHILD(n);
    int i = 0;
    while (child) {
        printf("Child %d: name=%s\n", i, child->name ? child->name : "NULL");
        child = child->nxt;
        i++;
    }
    
    Node* specifier = CHILD(n);
    Node* second    = NEXT(specifier);
    if (!second) return list;

    /* 只翻译函数定义: Specifier FunDec CompSt */
    if (strcmp(second->name, "FunDec") == 0) {
        printf("  Function definition found\n");
        Node* funDec = second;
        Node* compSt = NEXT(funDec);
        
        printf("  FunDec: %p, CompSt: %p\n", (void*)funDec, (void*)compSt);

        IRList f = translate_FunDec(funDec);
        IRList b = translate_CompSt(compSt);
        irlist_concat(&list, f);
        irlist_concat(&list, b);
    }
    /* 其它如全局变量声明可以忽略或扩展 */
    else {
        printf("  Not a function definition (second child: %s)\n", second->name);
    }
    
    printf("=== translate_ExtDef finished ===\n");
    return list;
}

/* FunDec -> ID LP VarList RP | ID LP RP */
static IRList translate_FunDec(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* id  = CHILD(n);         // ID
    Node* lp  = NEXT(id);         // LP
    Node* nxt = NEXT(lp);         // VarList 或 RP

    const char* func_name = NODE_TEXT(id);
    Operand func = op_function((char*)func_name);
    irlist_append(&list, ir_function(func));

    /* 处理形参列表，生成 PARAM 指令 */
    if (nxt && strcmp(NODE_NAME(nxt), "VarList") == 0) {
        IRList params = translate_VarList(nxt);
        irlist_concat(&list, params);
    }
    return list;
}

/* VarList -> ParamDec COMMA VarList | ParamDec */
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

/* ParamDec -> Specifier VarDec */
static IRList translate_ParamDec(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* spec   = CHILD(n);          // Specifier
    Node* varDec = NEXT(spec);        // VarDec

    /* 从 VarDec 中拿到变量名 (只处理普通 ID，不考虑数组/结构体) */
    Node* p = varDec;
    while (p && strcmp(NODE_NAME(p), "ID") != 0) {
        p = CHILD(p);  // 对 VarDec -> VarDec LB INT RB 这种，会继续往下走
    }
    if (p && strcmp(NODE_NAME(p), "ID") == 0) {
        const char* name = NODE_TEXT(p);
        Operand v = get_var_operand(name);
        irlist_append(&list, ir_param(v));
    }

    return list;
}

/*=====================*
 *   语句块 / 语句翻译  *
 *=====================*/

/* CompSt -> LC DefList StmtList RC */
static IRList translate_CompSt(Node* n) {
    printf("=== translate_CompSt ===\n");
    printf("Node: %p, name=%s\n", (void*)n, n->name ? n->name : "NULL");
    
    IRList list = irlist_create();
    if (!n) return list;

    // 安全地遍历子节点
    Node* child = CHILD(n);
    int child_num = 0;
    
    while (child) {
        printf("Child %d: %p, name=%s\n", 
               child_num, (void*)child, child->name ? child->name : "NULL");
        child = NEXT(child);
        child_num++;
    }
    
    // 重置到第一个子节点
    child = CHILD(n);
    
    // 第一个子节点应该是 LC
    if (!child || strcmp(child->name, "LC") != 0) {
        printf("ERROR: CompSt should start with LC\n");
        return list;
    }
    
    // 第二个子节点可能是 DefList 或 StmtList
    Node* second = NEXT(child);
    if (!second) {
        printf("ERROR: CompSt has no second child\n");
        return list;
    }
    
    printf("Second child: name=%s\n", second->name);
    
    // 处理 DefList（如果有）
    if (strcmp(second->name, "DefList") == 0) {
        printf("Processing DefList\n");
    
    // 打印DefList的完整结构
    printf("DefList structure:\n");
    print_node_tree(second, 1);  // 假设你有这个函数
    
        IRList def_ir = translate_DefList(second);
        irlist_concat(&list, def_ir);
        
        // 第三个子节点应该是 StmtList
        Node* third = NEXT(second);
        if (third && strcmp(third->name, "StmtList") == 0) {
            printf("Processing StmtList\n");
            IRList stmt_ir = translate_StmtList(third);
            irlist_concat(&list, stmt_ir);
        }
    } 
    // 或者直接是 StmtList（没有变量定义）
    else if (strcmp(second->name, "StmtList") == 0) {
        printf("Processing StmtList (no DefList)\n");
        IRList stmt_ir = translate_StmtList(second);
        irlist_concat(&list, stmt_ir);
    }
    // 或者还有其他情况...
    
    printf("=== translate_CompSt finished ===\n");
    return list;
}

/* StmtList -> Stmt StmtList | empty */
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

/* 各种 Stmt 的翻译 */
static IRList translate_Stmt(Node* n) {
    printf("\n=== translate_Stmt ===\n");
    printf("Node: %p, name=%s\n", (void*)n, n ? n->name : "NULL");
    
    IRList list = irlist_create();
    if (!n) return list;

    // 打印所有子节点用于调试
    Node* child = CHILD(n);
    int i = 0;
    while (child) {
        printf("Child %d: name=%s\n", i, child->name ? child->name : "NULL");
        child = NEXT(child);
        i++;
    }
    
    child = CHILD(n);  // 重置到第一个子节点
    if (!child) return list;

    /* 根据第一个子节点的类型决定如何处理 */
    
    /* Stmt -> CompSt */
    if (strcmp(child->name, "CompSt") == 0) {
        printf("  Stmt is CompSt\n");
        IRList b = translate_CompSt(child);  // 传递child，不是n！
        irlist_concat(&list, b);
    }
    /* Stmt -> RETURN Exp SEMI */
else if (strcmp(child->name, "RETURN") == 0) {
    printf("  Stmt is RETURN\n");
    Node* exp = NEXT(child);
    if (exp) {
        printf("  RETURN expression: %s\n", exp->name);
        
        // 打印表达式结构
        printf("  Expression structure:\n");
        print_node_tree(exp, 2);
        
        Operand t = new_temp();
        IRList e = translate_Exp(exp, t);
        irlist_concat(&list, e);
        irlist_append(&list, ir_return(t));
    }
}
    /* Stmt -> IF LP Exp RP Stmt [ELSE Stmt] */
else if (strcmp(child->name, "IF") == 0) {
    printf("  Stmt is IF\n");
    
    // 获取所有相关节点
    Node* lp = NEXT(child);        // Child 1: LP
    Node* exp = NEXT(lp);          // Child 2: Exp  
    Node* rp = NEXT(exp);          // Child 3: RP
    Node* stmt1 = NEXT(rp);        // Child 4: Stmt (then)
    Node* else_kw = NEXT(stmt1);   // Child 5: ELSE 或 NULL
    Node* stmt2 = else_kw ? NEXT(else_kw) : NULL;  // Child 6: Stmt (else) 或 NULL
    
    printf("    Condition: %p, Then: %p, Else: %p\n", 
           (void*)exp, (void*)stmt1, (void*)stmt2);
    
    // 详细打印ELSE分支信息
    if (else_kw) {
        printf("    Found ELSE keyword at %p\n", (void*)else_kw);
        if (stmt2) {
            printf("    ELSE branch statement: %p, name=%s\n", 
                   (void*)stmt2, stmt2->name);
            printf("    ELSE branch structure:\n");
            print_node_tree(stmt2, 3);
        } else {
            printf("    ERROR: ELSE keyword found but no statement after it!\n");
        }
    } else {
        printf("    No ELSE branch\n");
    }

    if (exp && stmt1) {
        if (else_kw && stmt2) {
            // IF-ELSE语句
            printf("    Processing IF-ELSE statement\n");
            Operand label_true = new_label();
            Operand label_false = new_label();
            Operand label_end = new_label();

            IRList cond = translate_Cond(exp, label_true, label_false);
            irlist_concat(&list, cond);

            // THEN 分支
            irlist_append(&list, ir_label(label_true));
            IRList then_ir = translate_Stmt(stmt1);
            irlist_concat(&list, then_ir);
            irlist_append(&list, ir_goto(label_end));

            // ELSE 分支
            irlist_append(&list, ir_label(label_false));
            IRList else_ir = translate_Stmt(stmt2);
            irlist_concat(&list, else_ir);
            
            // 结束标签
            irlist_append(&list, ir_label(label_end));
        } else {
            // IF语句（没有ELSE）
            printf("    IF statement (no ELSE)\n");
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
    /* Stmt -> Exp SEMI */
    else if (strcmp(child->name, "Exp") == 0) {
        printf("  Stmt is Exp SEMI\n");
        IRList e = translate_Exp(child, NULL);
        irlist_concat(&list, e);
    }
    else {
        printf("  Unknown Stmt type: %s\n", child->name);
    }
    
    printf("=== translate_Stmt finished ===\n");
    return list;
}

/*=====================*
 *   变量定义翻译      *
 *=====================*/

/* DefList -> Def DefList | empty */
/* DefList -> Def DefList | empty */
static IRList translate_DefList(Node* n) {
    printf("\n=== translate_DefList ===\n");
    printf("Node: %p, name=%s, line=%d\n", 
           (void*)n, n ? n->name : "NULL", n ? n->lineno : -1);
    
    IRList list = irlist_create();
    if (!n) {
        printf("NULL node, returning empty list\n");
        return list;
    }
    
    // 打印当前节点的完整信息
    printf("Current node details:\n");
    printf("  name: %s\n", n->name ? n->name : "NULL");
    printf("  value: %s\n", n->value ? n->value : "NULL");
    printf("  line: %d\n", n->lineno);
    printf("  child: %p\n", (void*)n->child);
    printf("  next: %p\n", (void*)n->nxt);
    
    // 检查子节点
    if (!n->child) {
        printf("No children, returning empty list\n");
        return list;
    }
    
    // 遍历所有子节点
    Node* child = n->child;
    int child_count = 0;
    while (child) {
        printf("Child %d: %p, name=%s\n", 
               child_count, (void*)child, child->name ? child->name : "NULL");
        child = child->nxt;
        child_count++;
    }
    
    // 第一个子节点应该是Def
    Node* def = n->child;
    if (!def || strcmp(def->name, "Def") != 0) {
        printf("ERROR: First child is not Def: %s\n", 
               def ? def->name : "NULL");
        return list;
    }
    
    printf("Processing Def: %p\n", (void*)def);
    IRList def_ir = translate_Def(def);
    irlist_concat(&list, def_ir);
    
    // 检查是否有第二个子节点（应该是另一个DefList）
    Node* next_def_list = def->nxt;
    if (next_def_list) {
        printf("Next node: %p, name=%s\n", 
               (void*)next_def_list, next_def_list->name ? next_def_list->name : "NULL");
        
        if (next_def_list->name && strcmp(next_def_list->name, "DefList") == 0) {
            printf("Recursive call to translate_DefList\n");
            IRList rest_ir = translate_DefList(next_def_list);
            irlist_concat(&list, rest_ir);
        } else {
            printf("WARNING: Next node is not DefList: %s\n", next_def_list->name);
        }
    } else {
        printf("No more DefList nodes\n");
    }
    
    printf("=== translate_DefList finished ===\n");
    return list;
}

/* Def -> Specifier DecList SEMI */
static IRList translate_Def(Node* n) {
    printf("translate_Def: node=%p\n", (void*)n);

    // 类型检查
    if (n && n->name && strcmp(n->name, "Def") != 0) {
        printf("ERROR: translate_Def called with non-Def node: %s\n", n->name);
        return irlist_create();  // 立即返回
    }
    
    IRList list = irlist_create();
    if (!n) {
        printf("ERROR: translate_Def called with NULL node\n");
        return list;
    }
    
    printf("  Node name: %s\n", n->name ? n->name : "NULL");

    Node* spec = CHILD(n);
    Node* decList = NEXT(spec);
    
    printf("  Specifier: %p, name=%s\n", 
           (void*)spec, spec ? spec->name : "NULL");
    printf("  DecList: %p, name=%s\n", 
           (void*)decList, decList ? decList->name : "NULL");

    if (!decList) {
        printf("ERROR: Def node has no DecList child\n");
        return list;
    }

    IRList d = translate_DecList(decList);
    irlist_concat(&list, d);
    return list;
}

/* DecList -> Dec | Dec COMMA DecList */
static IRList translate_DecList(Node* n) {
    printf("translate_DecList: node=%p\n", (void*)n);

    if (n && n->name && strcmp(n->name, "DecList") != 0) {
        printf("ERROR: translate_DecList called with non-DecList node: %s\n", n->name);
        return irlist_create();  // 立即返回
    }
    
    IRList list = irlist_create();
    if (!n) {
        printf("WARNING: translate_DecList called with NULL node\n");
        return list;
    }
    
    printf("  Node name: %s\n", n->name ? n->name : "NULL");
    printf("  Node line: %d\n", n->lineno);
    
    // 关键：在访问子节点前检查
    Node* dec = CHILD(n);
    if (!dec) {
        printf("ERROR: DecList node has no Dec child\n");
        return list;
    }
    
    printf("  Dec child: %p, name=%s\n", 
           (void*)dec, dec->name ? dec->name : "NULL");
    
    Node* comma_or_end = NEXT(dec);
    printf("  Next after dec: %p, name=%s\n", 
           (void*)comma_or_end, 
           comma_or_end ? comma_or_end->name : "NULL");

    IRList d1 = translate_Dec(dec);
    irlist_concat(&list, d1);

    if (comma_or_end && comma_or_end->name && 
        strcmp(comma_or_end->name, "COMMA") == 0) {
        Node* decList2 = NEXT(comma_or_end);
        printf("  Found COMMA, next DecList: %p\n", (void*)decList2);
        
        if (decList2) {
            IRList d2 = translate_DecList(decList2);
            irlist_concat(&list, d2);
        } else {
            printf("ERROR: COMMA后面没有DecList\n");
        }
    }
    
    printf("translate_DecList finished successfully\n");
    return list;
}

/* Dec -> VarDec | VarDec ASSIGNOP Exp
 * 这里只实现普通标量变量，不处理数组/结构体。数组的 DEC 可以在这里扩展。
 */
static IRList translate_Dec(Node* n) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* varDec = CHILD(n);
    Node* maybeAssign = NEXT(varDec);

    /* 从 VarDec 中拿到变量名 (下探到 ID) */
    Node* p = varDec;
    while (p && strcmp(NODE_NAME(p), "ID") != 0) {
        p = CHILD(p);
    }
    if (!p || strcmp(NODE_NAME(p), "ID") != 0) return list;
    const char* name = NODE_TEXT(p);
    Operand v = get_var_operand(name);

    /* TODO: 如果 varDec 是数组，这里应该计算大小并生成 DEC v size */

    /* 处理初始化: VarDec ASSIGNOP Exp */
    if (maybeAssign && strcmp(NODE_NAME(maybeAssign), "ASSIGNOP") == 0) {
        Node* exp = NEXT(maybeAssign);
        Operand t = new_temp();
        IRList e = translate_Exp(exp, t);
        irlist_concat(&list, e);
        irlist_append(&list, ir_assign(v, t));
    }

    return list;
}

/*=====================*
 *    表达式翻译       *
 *=====================*/

/* 公开接口：Exp 的翻译 */
IRList translate_Exp(Node* n, Operand place) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* c1 = CHILD(n);
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;

    /* 1. INT 常量 */
    if (c1 && strcmp(NODE_NAME(c1), "INT") == 0 && !c2) {
        if (place) {
            int value = atoi(NODE_TEXT(c1));
            Operand c = op_constant(value);
            irlist_append(&list, ir_assign(place, c));
        }
    }
    /* 2. ID（变量引用） */
    else if (c1 && strcmp(NODE_NAME(c1), "ID") == 0 && !c2) {
        if (place) {
            const char* name = NODE_TEXT(c1);
            Operand v = get_var_operand(name);
            irlist_append(&list, ir_assign(place, v));
        }
    }
    /* 3. Exp ASSIGNOP Exp */
    else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
             c2 && strcmp(NODE_NAME(c2), "ASSIGNOP") == 0 &&
             c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {

        /* 只处理左边是 ID 的简单赋值 */
        Node* left = CHILD(c1);
        if (left && strcmp(NODE_NAME(left), "ID") == 0) {
            const char* name = NODE_TEXT(left);
            Operand v = get_var_operand(name);
            Operand t = new_temp();

            IRList e2 = translate_Exp(c3, t);
            irlist_concat(&list, e2);
            irlist_append(&list, ir_assign(v, t));

            if (place) {
                irlist_append(&list, ir_assign(place, v));
            }
        }
        /* TODO: 左边是数组元素或结构体域的情况 */
    }
    /* 4. Exp PLUS/MINUS/STAR/DIV Exp */
else if (c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
         c2 && (strcmp(NODE_NAME(c2), "PLUS")  == 0 ||
                strcmp(NODE_NAME(c2), "MINUS") == 0 ||
                strcmp(NODE_NAME(c2), "STAR")  == 0 ||
                strcmp(NODE_NAME(c2), "DIV")   == 0) &&
         c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {

    printf("\n=== Processing binary operation ===\n");
    printf("Operator: %s\n", NODE_NAME(c2));
    
    // 打印左操作数结构
    printf("Left operand structure:\n");
    print_node_tree(c1, 1);
    
    // 打印右操作数结构
    printf("Right operand structure:\n");
    print_node_tree(c3, 1);
    
    Operand t1 = new_temp();
    Operand t2 = new_temp();

    printf("Translating left operand to t%d\n", t1->u.var_no);
    IRList e1 = translate_Exp(c1, t1);
    
    printf("Translating right operand to t%d\n", t2->u.var_no);
    IRList e2 = translate_Exp(c3, t2);
    
    irlist_concat(&list, e1);
    irlist_concat(&list, e2);

    if (place) {
        IRKind opk = IR_ADD;
        if (strcmp(NODE_NAME(c2), "PLUS") == 0)      opk = IR_ADD;
        else if (strcmp(NODE_NAME(c2), "MINUS") == 0) opk = IR_SUB;
        else if (strcmp(NODE_NAME(c2), "STAR") == 0)  opk = IR_MUL;
        else if (strcmp(NODE_NAME(c2), "DIV") == 0)   opk = IR_DIV;

        printf("Generating binary operation: ");
        print_operand(stdout, place);
        printf(" := ");
        print_operand(stdout, t1);
        printf(" %s ", NODE_NAME(c2));
        print_operand(stdout, t2);
        printf("\n");
        
        irlist_append(&list, ir_binop(opk, place, t1, t2));
    }
    
    printf("=== Binary operation finished ===\n");
}
    /* 5. MINUS Exp（一元负号） */
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
    /* 6. LP Exp RP */
    else if (c1 && strcmp(NODE_NAME(c1), "LP") == 0 && c2 && strcmp(NODE_NAME(c2), "Exp") == 0) {
        IRList e1 = translate_Exp(c2, place);
        irlist_concat(&list, e1);
    }
    /* 7. 逻辑表达式（AND/OR/NOT），用 translate_Cond + 0/1 */
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
        else{/* place 为 NULL 时，只生成 cond + 两个 label（无意义，但一般不会这样用） */
            irlist_append(&list, ir_label(label1));
            irlist_append(&list, ir_label(label2));
        }
    }
    /* 8. 关系表达式 Exp RELOP Exp */
    else if(c1 && strcmp(NODE_NAME(c1), "Exp") == 0 &&
            c2 && strcmp(NODE_NAME(c2), "RELOP") == 0 &&
            c3 && strcmp(NODE_NAME(c3), "Exp") == 0) {
    
        /* 这里应该只设置 place 为逻辑值（0/1） */
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
    /* 9. 函数调用：ID LP RP / ID LP Args RP */
    else if (c1 && strcmp(NODE_NAME(c1), "ID") == 0 &&
             c2 && strcmp(NODE_NAME(c2), "LP") == 0) {

        const char* func_name = NODE_TEXT(c1);
        Node* third = NEXT(c2);   // 可能是 Args 或 RP

        /* 特殊函数 read() */
        if (third && strcmp(NODE_NAME(third), "RP") == 0) {
            if (strcmp(func_name, "read") == 0) {
                /* READ place */
                if (place) {
                    irlist_append(&list, ir_read(place));
                } else {
                    Operand t = new_temp();
                    irlist_append(&list, ir_read(t));
                }
            } else {
                /* 一般无参函数调用 */
                Operand f = op_function((char*)func_name);
                if (place == NULL) {
                    Operand t = new_temp();
                    irlist_append(&list, ir_call(t, f));
                } else {
                    irlist_append(&list, ir_call(place, f));
                }
            }
        } else if (third && strcmp(NODE_NAME(third), "Args") == 0) {
            /* 有参函数调用 */
            Operand arg_list[128];
            int arg_cnt = 0;
            IRList args_ir = translate_Args(third, arg_list, &arg_cnt);
            irlist_concat(&list, args_ir);

            /* write(x) 特殊处理：WRITE x, 然后 place := #0 */
            if (strcmp(func_name, "write") == 0) {
                if (arg_cnt >= 1) {
                    irlist_append(&list, ir_write(arg_list[0]));
                }
                if (place) {
                    Operand zero = op_constant(0);
                    irlist_append(&list, ir_assign(place, zero));
                }
            } else {
                /* 普通函数：先按从右到左输出 ARG（按常见实现，可根据实验要求调整） */
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
    /* TODO: Exp LB Exp RB (数组元素), Exp DOT ID (结构体域) 等扩展 */
    else {
        /* 未处理的表达式形式，可以在调试时打印一下 */
        // fprintf(stderr, "translate_Exp: unhandled Exp form at line %d\n", n->lineno);
    }

    return list;
}

/*=====================*
 *   条件表达式翻译    *
 *=====================*/

static void print_node_tree(Node* node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    printf("name='%s', value='%s', line=%d\n", 
           node->name, 
           node->value ? node->value : "NULL",
           node->lineno);
    
    print_node_tree(node->child, depth + 1);
    print_node_tree(node->nxt, depth);
}

static void print_operand(FILE* out, Operand op) {
    if (!op) {
        fprintf(out, "NULL");
        return;
    }
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
        default:
            fprintf(out, "??");
            break;
    }
}

static IRList translate_Cond(Node* n, Operand label_true, Operand label_false) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* c1 = CHILD(n);
    Node* c2 = c1 ? NEXT(c1) : NULL;
    Node* c3 = c2 ? NEXT(c2) : NULL;

    /* 1. Exp1 RELOP Exp2 */
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
}

    /* 2. NOT Exp1 */
    else if (c1 && strcmp(NODE_NAME(c1), "NOT") == 0) {
        IRList inner = translate_Cond(c2, label_false, label_true);
        irlist_concat(&list, inner);
    }
    /* 3. Exp1 AND Exp2 */
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
    /* 4. Exp1 OR Exp2 */
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
    /* 5. 其他情况：当成 Exp != #0 */
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

/*=====================*
 *   Args 翻译         *
 *=====================*/

/* Args -> Exp | Exp COMMA Args
 * arg_list: 用来保存每个实参的 Operand
 * arg_cnt:  实际个数
 */
static IRList translate_Args(Node* n, Operand* arg_list, int* arg_cnt) {
    IRList list = irlist_create();
    if (!n) return list;

    Node* exp          = CHILD(n);
    Node* comma_or_end = NEXT(exp);

    Operand t = new_temp();
    IRList e = translate_Exp(exp, t);
    irlist_concat(&list, e);

    /* 先按从左到右把 t 存到数组里，之后在调用处决定 ARG 顺序 */
    arg_list[*arg_cnt] = t;
    (*arg_cnt)++;

    if (comma_or_end && strcmp(NODE_NAME(comma_or_end), "COMMA") == 0) {
        Node* args2 = NEXT(comma_or_end);
        IRList rest = translate_Args(args2, arg_list, arg_cnt);
        irlist_concat(&list, rest);
    }

    return list;
}
