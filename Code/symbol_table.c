#include "symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*--------------------- 配置 ---------------------*/
#define HASH_BITS 14
#define HASH_SIZE (1u << HASH_BITS) // 16384
#define HASH_MASK (HASH_SIZE - 1u)


/*--------------------- 作用域帧 ---------------------*/
typedef struct ScopeFrame {
    int depth; // 本帧深度
    Symbol *head; // 本层插入的符号链（通过 scope_next 串起来）
    struct ScopeFrame *prev; // 上一层
} ScopeFrame;


/*--------------------- 全局状态 ---------------------*/
static Symbol *g_hash[HASH_SIZE];
static ScopeFrame *g_top = NULL; // 作用域栈顶

/*--------------------- 内部工具 ---------------------*/
static void *xmalloc(size_t n){
    void *p = malloc(n);
    if(!p){ fprintf(stderr, "[symtab] OOM\n"); exit(1);}
    return p;
}

/* 哈希：PJW（参照教材示例，常数决定表大小） */
unsigned hash_pjw(const char *name){
    unsigned int val = 0, i;
    for (; *name; ++name){
        val = (val << 2) + (unsigned char)(*name);
        if ((i = val & ~HASH_MASK)) val = (val ^ (i >> (HASH_BITS - 2))) & HASH_MASK;
    }
    return val & HASH_MASK;
}

void free_symbol(Symbol *s){
    if(!s) return;
    if(s->name) free(s->name);
    if(s->func) funcsig_free(s->func);
    if(s->type) type_free(s->type);
    free(s);
}


/* 从哈希桶删除一个具体节点（假定一定存在） */
static void bucket_remove(Symbol **bucket_head, Symbol *target){
    Symbol *prev = NULL, *cur = *bucket_head;
    while(cur){
        if(cur == target){
            if(prev) prev->hash_next = cur->hash_next;
            else *bucket_head = cur->hash_next;
            return;
        }
        prev = cur; cur = cur->hash_next;
    }
}

/*--------------------- 作用域管理 ---------------------*/
void symtab_init(void){
    memset(g_hash, 0, sizeof(g_hash));
    while(g_top){ // 清空旧的作用域栈
        ScopeFrame *tmp = g_top->prev;
        // 不应残留符号；若残留，逐个释放
        Symbol *s = g_top->head;
        while(s){ Symbol *n = s->scope_next; free_symbol(s); s = n; }
        free(g_top); g_top = tmp;
    }
    // 建立全局作用域 depth=0
    g_top = (ScopeFrame*)xmalloc(sizeof(ScopeFrame));
    g_top->depth = 0; g_top->head = NULL; g_top->prev = NULL;
}


void symtab_enter_scope(void){
    ScopeFrame *nf = (ScopeFrame*)xmalloc(sizeof(ScopeFrame));
    nf->depth = g_top ? g_top->depth + 1 : 0;
    nf->head = NULL;
    nf->prev = g_top;
    g_top = nf;
}

void symtab_leave_scope(void){
    if(!g_top) return;
    // 逐个弹出本层符号：从哈希桶移除并释放
    Symbol *s = g_top->head;
    while(s){
        Symbol *next = s->scope_next;
        if(s->depth > 0){
            unsigned idx = hash_pjw(s->name);
            bucket_remove(&g_hash[idx], s);
            free_symbol(s);
        }
        s = next;
    }
    // 弹出帧
    ScopeFrame *old = g_top; g_top = g_top->prev; free(old);
}

int symtab_current_depth(void){ return g_top ? g_top->depth : 0;}

/*--------------------- 构造辅助 ---------------------*/
static Symbol *sym_new(const char *name, SymKind k){
    Symbol *s = (Symbol*)xmalloc(sizeof(Symbol));
    s->name = xstrdup(name);
    s->kind = k;
    s->type = NULL;
    s->func = NULL;
    s->depth = symtab_current_depth();
    s->hash_next = NULL;
    s->scope_next = NULL;
    return s;
}

Symbol *sym_make_var(const char *name, Type t){
    Symbol *s = sym_new(name, SYM_VAR);
    s->type = type_deepcopy(t); // 建议传入前已决定所有权策略：若独占，请传入 type_deepcopy 结果
    return s;
}


Symbol *sym_make_struct_tag(const char *name, Type t){
    Symbol *s = sym_new(name, SYM_STRUCT_TAG);
    s->type = type_deepcopy(t); // 结构体标签的类型通常是一个 T_STRUCT（包含域链）
    return s;
}

FuncSig *funcsig_make(Type ret, int argc, Type *argv, int declared_only){
    FuncSig *f = (FuncSig*)xmalloc(sizeof(FuncSig));
    f->ret = ret;
    f->argc = argc;
    if(argc > 0){
        f->argv = (Type*)xmalloc(sizeof(Type) * argc);
        for(int i=0;i<argc;++i) f->argv[i] = argv[i]; // 引用或深拷贝由上层策略决定
    }
    else{ f->argv = NULL;}
    f->declared_only = declared_only ? 1 : 0;
    return f;
}

void funcsig_free(FuncSig *f){
    if(!f) return;
    // 释放签名中类型：按“独占所有权”策略处理；若与符号或其他处共享，请改为不释放
    if(f->ret) {
        type_free(f->ret);
        f->ret = NULL;
    }
    if(f->argv){
        for(int i = 0; i < f->argc; ++i){
            //if(f->argv[i]) type_free(f->argv[i]);
            f->argv[i] = NULL;
        } 
        free(f->argv);
    }
    free(f);
}

Symbol *sym_make_func_decl(const char *name, FuncSig *fn){
    Symbol *s = sym_new(name, SYM_FUNC);
    s->func = fn; // fn->declared_only 应为 1
    return s;
}

Symbol *sym_make_func_def(const char *name, FuncSig *fn){
    Symbol *s = sym_new(name, SYM_FUNC);
    s->func = fn; // fn->declared_only 应为 0
    return s;
}

/*--------------------- 查找 ---------------------*/
Symbol *symtab_lookup(const char *name){
    if(!name) return NULL;
    unsigned idx = hash_pjw(name);
    for(Symbol *s = g_hash[idx]; s; s = s->hash_next){
        if(strcmp(s->name, name)==0) return s; // 最近定义优先（头插）
    }
    return NULL;
}

Symbol *symtab_lookup_in_current_scope(const char *name){
    if(!name || !g_top) return NULL;
    // 遍历当前作用域链表（比遍历整个桶更快定位“是否本层重名”）
    for(Symbol *s = g_top->head; s; s = s->scope_next){
        if(strcmp(s->name, name)==0) return s;
    }
    return NULL;
}

/*--------------------- 插入 ---------------------*/
int symtab_insert(Symbol *s){
    if(!s || !s->name){ return 0; }
    if(!g_top){ symtab_init(); }
    // 1) 本层重名检查
    if(symtab_lookup_in_current_scope(s->name)){ return 0;} // 同一作用域内不可重名 }
    // 2) 插入哈希桶（头插保证“最近定义优先”）
    unsigned idx = hash_pjw(s->name);
    s->hash_next = g_hash[idx];
    g_hash[idx] = s;
    // 3) 插入本层作用域链
    s->scope_next = g_top->head;
    g_top->head = s;
    // 4) 记录深度
    s->depth = g_top->depth;
    return 1;
}

void symtab_print_all(void) {
    printf("=== SYMBOL TABLE (ALL) ===\n");
    printf("Current depth: %d\n", symtab_current_depth());
    
    int total_symbols = 0;
    int bucket_counts[HASH_SIZE] = {0};
    
    // 打印哈希表
    for (unsigned i = 0; i < HASH_SIZE; i++) {
        if (g_hash[i]) {
            printf("Bucket[%u]: ", i);
            Symbol *s = g_hash[i];
            int count = 0;
            while (s) {
                if (count > 0) printf(" -> ");
                printf("%s(depth:%d)", s->name, s->depth);
                count++;
                total_symbols++;
                s = s->hash_next;
            }
            bucket_counts[i] = count;
            printf("\n");
        }
    }
    
    printf("Total symbols: %d\n", total_symbols);
    
    // 打印作用域栈
    printf("\n=== SCOPE STACK ===\n");
    ScopeFrame *frame = g_top;
    int frame_num = 0;
    while (frame) {
        printf("Frame %d (depth: %d): ", frame_num++, frame->depth);
        Symbol *s = frame->head;
        int count = 0;
        while (s) {
            if (count > 0) printf(", ");
            printf("%s", s->name);
            count++;
            s = s->scope_next;
        }
        printf(" (%d symbols)\n", count);
        frame = frame->prev;
    }
    printf("=== END SYMBOL TABLE ===\n\n");
}

void symtab_print_current_scope(void) {
    if (!g_top) {
        printf("No current scope\n");
        return;
    }
    
    printf("=== CURRENT SCOPE (depth: %d) ===\n", g_top->depth);
    Symbol *s = g_top->head;
    int count = 0;
    
    while (s) {
        printf("[%d] %s\n", count, s->name);
        count++;
        s = s->scope_next;
    }
    
    if (count == 0) {
        printf("(empty)\n");
    } else {
        printf("Total: %d symbols\n", count);
    }
    printf("=== END CURRENT SCOPE ===\n\n");
}