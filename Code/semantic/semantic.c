#include "semantic.h"
#include "symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

/*********************************
 *  AST 访问宏（基于 node.h）    *
 *********************************/

#define NODE_KIND(n)   ((n)->name)
#define NODE_LINE(n)   ((n)->lineno)
#define NODE_CHILD(n)  ((n)->child)
#define NODE_NEXT(n)   ((n)->nxt)
#define TEXT(n)        ((n)->value)     // 词素文本（ID/TYPE/INT/FLOAT 等）

static Node* child_at(Node* n, int k){
    Node* c = NODE_CHILD(n);
    for(int i=0; c && i<k; ++i) c = NODE_NEXT(c);
    return c;
}

static const char* kind(Node* n){ return n ? NODE_KIND(n) : "<null>"; }
static int line(Node* n){ return n ? NODE_LINE(n) : 0; }

static long parse_int(Node* n){
    if(!n || !TEXT(n)) return 0;
    char *end=NULL; long v = strtol(TEXT(n), &end, 0); (void)end; return v;
}

/*********************************
 *  错误与全局状态               *
 *********************************/

static int g_errcnt = 0;
static Type g_current_func_ret = NULL; // 当前函数返回类型（用于 return 检查）

static void report(int etype, int lineno, const char* fmt, ...){
    ++g_errcnt;
    fprintf(stderr, "Error type %d at Line %d: ", etype, lineno);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

int semantic_error_count(void){ return g_errcnt; }

/*********************************
 *  前向声明（visit_*）          *
 *********************************/

static void visit_Program(Node* n);
static void visit_ExtDefList(Node* n);
static void visit_ExtDef(Node* n);
static Type visit_Specifier(Node* n);
static Type visit_StructSpecifier(Node* n);
static void visit_FunDec(Node* n, Type ret, int is_def);
static FieldList build_ParamList_from_VarList(Node* n);
static void visit_CompSt(Node* n);
static void visit_DefList(Node* n, int is_struct_field);
static void visit_Def(Node* n, int is_struct_field);
static void visit_DecList(Node* n, Type base, int is_struct_field);
static void visit_Dec(Node* n, Type base, int is_struct_field);
static Type visit_VarDec(Node* n, Type base, char** out_name);
static void visit_StmtList(Node* n);
static void visit_Stmt(Node* n);
static Type visit_Exp(Node* n, int* is_lvalue);
static FieldList build_FieldList_from_DefList(Node* deflist);
static Type find_struct_field(Type st, const char* name);

/*********************************
 *  入口                         *
 *********************************/

void semantic_init(void){
    symtab_init();
    g_errcnt = 0;
    g_current_func_ret = NULL;
}

void semantic_analyze(Node* root){
    if(!root) return;
    if(strcmp(kind(root), "Program") == 0) visit_Program(root);
    else visit_Program(root); // 容错：直接按 Program 入口
}

/*********************************
 *  语法单元：Program / ExtDef*  *
 *********************************/

static void visit_Program(Node* n){
    // Program -> ExtDefList
    Node* extdeflist = child_at(n, 0);
    visit_ExtDefList(extdeflist);
}

static void visit_ExtDefList(Node* n){
    // ExtDefList -> ExtDef ExtDefList | (empty)
    for(Node* p=n; p; p = child_at(p, 1)){
        Node* extdef = child_at(p, 0);
        if(extdef) visit_ExtDef(extdef);
        else break;
    }
}

static void visit_ExtDef(Node* n){
    // 常见三种：
    // 1) Specifier ExtDecList SEMI   (全局变量定义)
    // 2) Specifier FunDec CompSt     (函数定义)
    // 3) Specifier FunDec SEMI       (函数声明)
    // 或者：结构体类型定义：Specifier SEMI
    Node* spec = child_at(n, 0);
    Type t = visit_Specifier(spec);
    Node* second = child_at(n, 1);
    if(!second){ return; }

    if(strcmp(kind(second), "ExtDecList") == 0){
        // 变量定义序列：ExtDecList -> VarDec ( , VarDec )*
        Node* p = second;
        while(p){
            Node* vardec = child_at(p, 0);
            char* name = NULL;
            Type vtype = visit_VarDec(vardec, t, &name);
            if(name){
                if(symtab_lookup_in_current_scope(name)){
                    report(3, line(vardec), "Redefined variable '%s'", name);
                }else{
                    Symbol* s = sym_make_var(name, type_deepcopy(vtype));
                    if(!symtab_insert(s)){
                        report(3, line(vardec), "Redefined variable '%s'", name);
                    }
                }
            }
            // 逗号后还有？ ExtDecList -> VarDec COMMA ExtDecList
            Node* comma = child_at(p, 1);
            if(comma && child_at(p,2)) p = child_at(p,2); else break;
        }
    }
    else if(strcmp(kind(second), "FunDec") == 0){
        Node* third = child_at(n, 2);
        int is_def = (third && strcmp(kind(third), "CompSt")==0);
        visit_FunDec(second, t, is_def);
        if(is_def){ visit_CompSt(third); }
    }
    else if(strcmp(kind(second), "SEMI") == 0){
        // 结构体类型定义：Specifier SEMI
        (void)0;
    } 
    else {
        (void)0;
    }
}

/*********************************
 *  Specifier / StructSpecifier  *
 *********************************/

static Type visit_Specifier(Node* n){
    if (!n) {
        report(17, 0, "Null specifier node");
        return type_make_basic(TY_INT);
    }
    Node* c = child_at(n, 0);
    if (!c) {
        report(17, line(n), "Empty specifier");
        return type_make_basic(TY_INT);
    }
    
    // Specifier -> TYPE | StructSpecifier
    if (strcmp(kind(c), "TYPE") == 0) {
        const char* ty = TEXT(c);
        if (ty) {
            if (strcmp(ty, "int") == 0) {
                return type_make_basic(TY_INT);
            } else if (strcmp(ty, "float") == 0) {
                return type_make_basic(TY_FLOAT);
            }
        }
        // 未知类型，默认返回 int
        report(17, line(n), "Unknown type '%s'", ty ? ty : "NULL");
        return type_make_basic(TY_INT);
    } 
    else if (strcmp(kind(c), "StructSpecifier") == 0) {
        return visit_StructSpecifier(c);
    }
    else {
        // 既不是 TYPE 也不是 StructSpecifier，这是语法错误
        report(17, line(n), "Invalid specifier: expected TYPE or StructSpecifier, got %s", kind(c));
        return type_make_basic(TY_INT);
    }
}

static FieldList build_FieldList_from_DefList(Node* deflist){
    // 用于 StructSpecifier 内部把域列表收集成 FieldList
    FieldList head = NULL;
    
    // 用于检查字段名重复的集合
    typedef struct FieldSet {
        char *name;
        struct FieldSet *next;
    } FieldSet;
    FieldSet *field_set = NULL;
    
    for(Node* p=deflist; p; p=child_at(p,1)){
        Node* def = child_at(p,0); if(!def) break;
        // Def -> Specifier DecList SEMI
        Type base = visit_Specifier(child_at(def,0));
        Node* declist = child_at(def,1);
        
        for(Node* q=declist; q; ){
            Node* dec = child_at(q,0);
            // Dec -> VarDec | VarDec ASSIGNOP Exp  （结构体域里不允许初始化）
            char* name = NULL;
            Type ftype = visit_VarDec(child_at(dec,0), base, &name);
            
            if(child_at(dec,1)){
                report(15, line(dec), "Illegal initialization of struct field '%s'", name?name:"<field>");
            }
            
            if(name){
                // 检查字段名是否重复
                int is_duplicate = 0;
                FieldSet *fs = field_set;
                while (fs) {
                    if (fs->name && strcmp(fs->name, name) == 0) {
                        is_duplicate = 1;
                        break;
                    }
                    fs = fs->next;
                }
                
                if (is_duplicate) {
                    report(15, line(dec), "Redefined field \"%s\"", name);
                    free(name);
                    type_free(ftype);
                } else {
                    // 添加到字段集合中
                    FieldSet *new_fs = (FieldSet*)malloc(sizeof(FieldSet));
                    new_fs->name = xstrdup(name);
                    new_fs->next = field_set;
                    field_set = new_fs;
                    
                    // 添加到字段链表（头插）
                    head = field_make(name, ftype, head);
                }
            }
            
            Node* comma = child_at(q,1);
            if(comma && child_at(q,2)) q = child_at(q,2); else break;
        }
    }
    
    // 释放字段集合内存
    while (field_set) {
        FieldSet *next = field_set->next;
        free(field_set->name);
        free(field_set);
        field_set = next;
    }
    
    // 反转链表，保持文法出现顺序
    FieldList rev=NULL; 
    while(head){ 
        FieldList nx=head->tail; 
        head->tail=rev; 
        rev=head; 
        head=nx; 
    }
    return rev;
}

static Type visit_StructSpecifier(Node* n){
    // 形式1：StructSpecifier -> STRUCT Tag
    // 形式2：StructSpecifier -> STRUCT OptTag LC DefList RC
    Node* c1 = child_at(n,1);
    if(strcmp(kind(c1), "Tag")==0 || (strcmp(kind(c1), "ID")==0 && !child_at(n,2))){
        // STRUCT Tag 或直接 STRUCT ID
        const char* tag = (strcmp(kind(c1),"ID")==0) ? TEXT(c1) : TEXT(child_at(c1,0));
        Symbol* s = symtab_lookup(tag);
        if(!s || s->kind != SYM_STRUCT_TAG){
            report(17, line(c1), "Undefined struct '%s'", tag);
            return type_make_struct(NULL);
        }
        return s->type; // 共享引用
    }else{
        // STRUCT OptTag LC DefList RC
        const char* tag = NULL;
        if(strcmp(kind(c1), "OptTag")==0){
            Node* id = child_at(c1,0);
            if(id && strcmp(kind(id),"ID")==0) tag = TEXT(id);
        }
        Node* deflist = child_at(n,3);
        FieldList fields = build_FieldList_from_DefList(deflist);
        Type st = type_make_struct(fields);
        if(tag){
            if(symtab_lookup_in_current_scope(tag)){
                report(16, line(c1), "Duplicated name '%s'", tag);
            }else{
                Symbol* tagSym = sym_make_struct_tag(tag, st);
                if(!symtab_insert(tagSym)){
                    report(16, line(c1), "Duplicated name '%s'", tag);
                }
            }
        }
        return st;
    }
}

/*********************************
 *  VarDec / 构造数组层级        *
 *********************************/

static Type visit_VarDec(Node* n, Type base, char** out_name){
    
    Node* c0 = child_at(n,0);
    
    if(strcmp(kind(c0), "ID")==0){
        const char* id_name = TEXT(c0);
        if(out_name && id_name)    *out_name = xstrdup(id_name);
        return base;
    }
    else{
        // VarDec LB INT RB
        char* inner_name = NULL;  // 改为有意义的变量名
        Type elem = visit_VarDec(c0, base, &inner_name); 
        
        Node* intnode = child_at(n,2);
        int sz = intnode ? (int)parse_int(intnode) : 0;
        if(sz<=0) report(15, line(intnode?intnode:n), "Illegal array size");
        
        // 重要：将内部名称传递到外层
        if(out_name && inner_name)    *out_name = inner_name;  // 直接传递所有权，不需要复制
        else if (inner_name)    free(inner_name);  // 如果外层不需要名称，释放内存
        
        return type_make_array(elem, sz);
    }
}

/*********************************
 *  函数：FunDec / 形参列表      *
 *********************************/

static FieldList build_ParamList_from_VarList(Node* varlist){
    FieldList head = NULL, tail = NULL;
    for(Node* p = varlist; p; ){
        Node* paramdec = child_at(p, 0);
        if (!paramdec) break;
        Type base = visit_Specifier(child_at(paramdec, 0));
        if (!base) {
            base = type_make_basic(TY_INT); // 默认类型
        }
        
        char* name = NULL;
        Type param_type = visit_VarDec(child_at(paramdec, 1), base, &name);
        
        // 确保名称有效
        if (!name) {
            name = xstrdup("<unnamed>");
        }
        
        FieldList f = field_make(name, type_deepcopy(param_type), NULL);
        free(name); // field_make 已经复制了名称
        
        if(!head) {
            head = tail = f;
        } else {
            tail->tail = f;
            tail = f;
        }
        
        Node* comma = child_at(p, 1);
        if(comma && child_at(p, 2)) {
            p = child_at(p, 2);
        } else {
            break;
        }
    }
    return head;
}

/* ================= 修改版 visit_FunDec ================= */
static FieldList g_pending_params = NULL; // ★新增：暂存形参列表，供 CompSt 使用

static void fieldlist_free_all(FieldList f){
    while(f){
        FieldList nx = f->tail;
        if(f->name) free(f->name);
        if(f->type) type_free(f->type);
        free(f);
        f = nx;
    }
}

/* ---------- FunDec -> ID LP VarList RP | ID LP RP ---------- */
static void visit_FunDec(Node* n, Type ret, int is_def){
    // 先清理之前的 pending params
    if (g_pending_params) {
        fieldlist_free_all(g_pending_params);
        g_pending_params = NULL;
    }
    
    Node* id = child_at(n, 0);
    if (!id) {
        report(19, line(n), "Function has no name");
        return;
    }
    
    const char* fname = TEXT(id);
    if (!fname) {
        report(19, line(n), "Function name is NULL");
        return;
    }

    FieldList params = NULL;
    Node* c2 = child_at(n, 2);
    if (c2 && strcmp(kind(c2), "VarList") == 0) {
        params = build_ParamList_from_VarList(c2);
    }

    // 将 FieldList 转成 Type* 数组
    int argc = 0;
    for(FieldList f = params; f; f = f->tail) ++argc;
    
    Type* argv = NULL;
    if (argc > 0) {
        argv = (Type*)malloc(sizeof(Type) * argc);
        if (!argv) {
            report(19, line(n), "Memory allocation failed");
            fieldlist_free_all(params);
            return;
        }
        
        int i = 0;
        for(FieldList f = params; f; f = f->tail) {
            argv[i++] = f->type ? type_deepcopy(f->type) : NULL;
        }
    }

    FuncSig* fs = funcsig_make(type_deepcopy(ret), argc, argv, is_def ? 0 : 1);
    
    // 清理 argv 数组（funcsig_make 已经接管了类型的所有权）
    if (argv) free(argv);

    Symbol* old = symtab_lookup(fname);
    if(old){
        if(old->kind != SYM_FUNC){
            report(19, line(n), "'%s' is not a function", fname);
            funcsig_free(fs);
            fieldlist_free_all(params);
        } else {
            // 检查是否是真正的重定义（相同的签名）
            int is_redefinition = 1;
            
            if(old->func->argc != fs->argc){
                is_redefinition = 0;
            } 
            else if(!type_equal(old->func->ret, fs->ret, false)){
                is_redefinition = 0;
            } 
            else {
                for(int k = 0; k < fs->argc; ++k){
                    if(!type_equal(old->func->argv[k], fs->argv[k], false)){
                        is_redefinition = 0;
                        break;
                    }
                }
            }
            
            if(is_redefinition){
                // 相同签名的重定义：错误类型4
                if(!old->func->declared_only && is_def){
                    report(4, line(n), "Redefined function '%s'", fname);
                }
                if(old->func->declared_only && is_def){
                    old->func->declared_only = 0;
                }
            } else {
                // 不同签名的冲突：根据你的要求，可以选择报告错误类型4或19
                // 如果要报告错误类型4：
                report(4, line(n), "Redefined function '%s'", fname);
                // 或者如果要报告错误类型19（更准确）：
                // report(19, line(n), "Conflicting function declaration/definition for '%s'", fname);
            }
            
            funcsig_free(fs);
            fieldlist_free_all(params);
        }
    } else {
        // 新函数
        Symbol* fn = is_def ? sym_make_func_def(fname, fs) : sym_make_func_decl(fname, fs);
        if(!symtab_insert(fn)){
            report(19, line(n), "Duplicated function '%s'", fname);
            funcsig_free(fs);
            free(fn);
            fieldlist_free_all(params); // 插入失败也要释放
        } else {
            // 插入成功，如果是定义，保存参数供 CompSt 使用
            if(is_def){
                g_current_func_ret = ret;
                g_pending_params = params;
            } else {
                fieldlist_free_all(params); // 声明立即释放
            }
        }
    }
    
    // 防御性置空
    if (!is_def) {
        g_pending_params = NULL;
    }
}


/*********************************
 *  复合语句块 / 局部定义        *
 *********************************/

static void visit_CompSt(Node* n){
    symtab_enter_scope();
    
    if (g_pending_params) {
        for (FieldList f = g_pending_params; f; f = f->tail) {
            // 参数在当前作用域，允许与外部变量重名
            Symbol* vs = sym_make_var(f->name, type_deepcopy(f->type));
            if(!symtab_insert(vs)){
                report(3, line(n), "Redefined parameter '%s'", f->name);
            }
        }
        fieldlist_free_all(g_pending_params);
        g_pending_params = NULL;
    }

    // 处理局部变量定义
    for (Node* child = NODE_CHILD(n); child; child = NODE_NEXT(child)) {
        if (strcmp(kind(child), "DefList") == 0) {
            visit_DefList(child, /*is_struct_field=*/0);
        } else if (strcmp(kind(child), "StmtList") == 0) {
            visit_StmtList(child);
        }
    }

    symtab_leave_scope();
}

static void visit_StmtList(Node* n){
    for(Node* p=n; p; p=child_at(p,1)){
        Node* stmt = child_at(p,0); if(!stmt) break;
        visit_Stmt(stmt);
    }
}

static void visit_Stmt(Node* n){
    Node* c0 = child_at(n,0);
    if(!c0) return;
    
    if(strcmp(kind(c0), "Exp")==0){
        int lv=0; (void)visit_Exp(c0, &lv);
    }
    else if(strcmp(kind(c0), "CompSt")==0){
        visit_CompSt(c0); // 已经包含作用域管理
    }
    else if(strcmp(kind(c0), "RETURN")==0){
        Type t = visit_Exp(child_at(n,1), NULL);
        if(g_current_func_ret && !type_equal(g_current_func_ret, t, false)){
            report(8, line(n), "Type mismatched for return");
        }
    }
    else if(strcmp(kind(c0), "IF")==0){
        (void)visit_Exp(child_at(n,2), NULL);
        
        // 为 then 分支创建作用域
        symtab_enter_scope();
        visit_Stmt(child_at(n,4));
        symtab_leave_scope();
        
        Node* elsekw = child_at(n,5);
        if(elsekw && strcmp(kind(elsekw),"ELSE")==0){
            // 为 else 分支创建作用域
            symtab_enter_scope();
            visit_Stmt(child_at(n,6));
            symtab_leave_scope();
        }
    }
    else if(strcmp(kind(c0), "WHILE")==0){
        (void)visit_Exp(child_at(n,2), NULL);
        
        // 为循环体创建作用域
        symtab_enter_scope();
        visit_Stmt(child_at(n,4));
        symtab_leave_scope();
    }
}

static void visit_DefList(Node* n, int is_struct_field){
    if (!n) return;  // 添加空指针检查
    
    for(Node* p=n; p; p = child_at(p, 1)){
        Node* def = child_at(p, 0); 
        if(!def) break;
        visit_Def(def, is_struct_field);
    }
}

static void visit_Def(Node* n, int is_struct_field){
    // Def -> Specifier DecList SEMI
    Node* spec_node = child_at(n, 0);
    Type base = visit_Specifier(spec_node);
    Node* declist_node = child_at(n, 1);
    visit_DecList(declist_node, base, is_struct_field);
}

static void visit_DecList(Node* n, Type base, int is_struct_field){
    for(Node* p=n; p; ){
        Node* dec = child_at(p, 0);
        if (!dec)    break;
        visit_Dec(child_at(p,0), base, is_struct_field);
        Node* comma = child_at(p,1);
        if(comma && child_at(p,2)) p = child_at(p,2); 
        else break;
    }
}

static void visit_Dec(Node* n, Type base, int is_struct_field){
    Node* vardec = child_at(n,0);
    char* name=NULL; 
    Type t = visit_VarDec(vardec, base, &name);
    
    if(name){
        // 只检查当前作用域是否有重名（允许覆盖外层作用域的定义）
        Symbol* existing = symtab_lookup_in_current_scope(name);
        if(existing){
            report(3, line(vardec), "Redefined variable '%s'", name);
            type_free(t);
        }
        else{
            Symbol* s = sym_make_var(name, t);
            if(!symtab_insert(s)){
                report(3, line(vardec), "Redefined variable '%s'", name);
                free_symbol(s);
            }
        }
        
        // 处理初始化
        if(child_at(n,1)){
            Type rt = visit_Exp(child_at(n,2), NULL);
            if(!type_equal(t, rt, false)){
                report(5, line(n), "Type mismatched for assignment (initializer)");
            }
        }
        
        free(name);
    } else {
        type_free(t);
    }
}

/*********************************
 *  表达式                        *
 *********************************/

static int is_relop(Node* n){ return strcmp(kind(n),"RELOP")==0; }

static Type visit_Exp(Node* n, int* is_lvalue){
    if(is_lvalue) *is_lvalue = 0;
    Node* c0 = child_at(n,0);
    Node* c1 = child_at(n,1);
    if(!c0) return NULL;

    // ID
    if(strcmp(kind(c0), "ID")==0 && !c1){
        const char* name = TEXT(c0);
        Symbol* s = symtab_lookup(name); // 这会自动找到最近作用域的定义
        
        if(!s){
            report(1, line(n), "Undefined variable '%s'", name); 
            if (is_lvalue) *is_lvalue = 1;
            return type_make_basic(TY_INT); 
        }
        
        // 检查是否是函数名被误用为变量
        if(s->kind == SYM_FUNC){ 
            report(1, line(n), "'%s' is a function, not a variable", name); 
            return type_make_basic(TY_INT);
        }
        
        if(is_lvalue) *is_lvalue = 1;
        return s->type;
    }

    // INT/FLOAT literal
    if(strcmp(kind(c0),"INT")==0 && !c1) return type_make_basic(TY_INT);
    if(strcmp(kind(c0),"FLOAT")==0 && !c1) return type_make_basic(TY_FLOAT);

    // ( Exp )
    if(strcmp(kind(c0),"LP")==0){
        return visit_Exp(child_at(n,1), is_lvalue);
    }

    // 一元 - / !
    if(strcmp(kind(c0),"MINUS")==0){
        Type t = visit_Exp(child_at(n,1), NULL);
        if(!type_is_numeric(t)) report(7, line(n), "Type mismatched for operands");
        return t;
    }
    if(strcmp(kind(c0),"NOT")==0){
        Type t = visit_Exp(child_at(n,1), NULL);
        if(!type_is_int(t)) report(7, line(n), "Type mismatched for operands");
        return t;
    }

    // 函数调用  ID ( Args? )
    if(strcmp(kind(c0),"ID")==0 && strcmp(kind(c1),"LP")==0){
        const char* fname = TEXT(c0);
        Symbol* s = symtab_lookup(fname);
        
        if(!s){
            report(2, line(n), "Undefined function '%s'", fname);
            return type_make_basic(TY_INT);
        }
        
        // 检查符号类型：如果是变量而不是函数，报告错误类型11
        if(s->kind != SYM_FUNC){
            report(11, line(n), "'%s' is not a function", fname);
            return type_make_basic(TY_INT);
        }
        
        int given = 0;
        if(strcmp(kind(child_at(n,2)),"Args")==0){
            int idx = 0; Node* args = child_at(n,2);
            for(Node* p=args; p; ){
                Type at = visit_Exp(child_at(p,0), NULL);
                if(idx >= s->func->argc){ report(9, line(n), "Too many arguments to '%s'", fname); break; }
                if(!type_equal(at, s->func->argv[idx], false)){
                    report(9, line(n), "Argument %d type mismatch for '%s'", idx+1, fname);
                }
                ++idx;
                Node* comma = child_at(p,1);
                if(comma && child_at(p,2)) p = child_at(p,2); else { p=NULL; }
            }
            given = idx;
        }
        if(given != s->func->argc){
            report(9, line(n), "Number of arguments mismatch for '%s'", fname);
        }
        return s->func->ret;
    }

    Node* c2 = child_at(n,2);

    // 赋值
    if(c1 && strcmp(kind(c1),"ASSIGNOP")==0){
        int lv=0; 
        Type lt = visit_Exp(c0, &lv); 
        Type rt = visit_Exp(c2, NULL);
    
        if(!lv)    report(6, line(n), "The left-hand side of an assignment must be a variable");
        else if(!type_equal(lt, rt, false))    report(5, line(n), "Type mismatched for assignment");
        return lt;
    }

    // 数组访问 A[B]
    if(c1 && strcmp(kind(c1),"LB")==0){
        Type a = visit_Exp(c0, NULL); 
        Type idx = visit_Exp(c2, NULL);

        if(!type_is_array(a)){
            report(10, line(n), "Not an array");
            return type_make_basic(TY_INT);
        }

        // 检查数组索引类型：如果是浮点数，报告错误类型12
        if(idx && type_is_float(idx)){
            report(12, line(c2), "\"%s\" is not an integer", TEXT(c2) ? TEXT(c2) : "index");
            // 在报告错误后，我们仍然应该设置左值标志，因为数组访问本身是左值
            if(is_lvalue)    *is_lvalue = 1;
            return type_array_elem(a);
        }
        if(!type_is_int(idx))    report(12, line(n), "Array index is not an integer");
    
        if(is_lvalue)    *is_lvalue = 1;
    return type_array_elem(a);
}

    // 结构体成员 A.B
    if(c1 && strcmp(kind(c1),"DOT")==0){
        Type a = visit_Exp(c0, NULL);
        if(!type_is_struct(a)){
            report(13, line(n), "Illegal use of '.'");
            // 返回错误标记类型
            return NULL;
        }
        Node* id = c2; 
        const char* mname = TEXT(id);
        int error_line = line(id);
        if (error_line <= 0) error_line = line(n);
    
        Type mt = find_struct_field(a, mname);
        if(!mt){ 
            report(14, error_line, "Non-existent field \"%s\"", mname); 
            // 返回 NULL 表示错误，让调用者处理
            return NULL;
        }
    
        if(is_lvalue) *is_lvalue = 1;
        return mt;
    }

    // 逻辑/关系/算术
    if(c1 && (strcmp(kind(c1),"AND")==0 || strcmp(kind(c1),"OR")==0)){
        Type a = visit_Exp(c0, NULL), b = visit_Exp(c2, NULL);
        if(!type_is_int(a) || !type_is_int(b)) report(7, line(n), "Type mismatched for operands");
        return type_make_basic(TY_INT);
    }
    if(c1 && (is_relop(c1))){
        Type a = visit_Exp(c0, NULL);
        Type b = visit_Exp(c2, NULL);
        // 如果任一操作数为 NULL（表示有错误），跳过类型检查
        if (!a || !b)     return type_make_basic(TY_INT);
        if(!type_is_numeric(a) || !type_is_numeric(b) || !type_equal(a,b,false))    report(7, line(n), "Type mismatched for operands");
        return type_make_basic(TY_INT);
    }
    if(c1 && (strcmp(kind(c1),"PLUS")==0 || strcmp(kind(c1),"MINUS")==0 ||
              strcmp(kind(c1),"STAR")==0 || strcmp(kind(c1),"DIV")==0)){
        Type a = visit_Exp(c0, NULL), b = visit_Exp(c2, NULL);
        if(!type_is_numeric(a) || !type_is_numeric(b) || !type_equal(a,b,false))
            report(7, line(n), "Type mismatched for operands");
        return a;
    }

    // 兜底
    return type_make_basic(TY_INT);
}

/*********************************
 *  结构体成员查找                *
 *********************************/

static Type find_struct_field(Type st, const char* name){
    if(!type_is_struct(st)) return NULL;
    for(FieldList f = st->u.structure; f; f=f->tail){
        if(f->name && strcmp(f->name,name)==0) return f->type;
    }
    return NULL;
}