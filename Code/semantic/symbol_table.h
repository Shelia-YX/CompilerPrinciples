#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdbool.h>
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 符号种类 */
typedef enum {
    SYM_VAR = 0, // 变量/常量
    SYM_FUNC = 1, // 函数
    SYM_STRUCT_TAG = 2 // 结构体标签（struct Tag {...} 的 Tag）
} SymKind;

/* 函数签名 */
typedef struct FuncSig {
    Type ret; // 返回类型
    int argc; // 参数个数
    Type *argv; // 参数类型数组（长度为 argc）
    int declared_only; // 1 表示仅声明，0 表示已定义
} FuncSig;

/* 符号条目 */
typedef struct Symbol {
    char *name; // 符号名
    SymKind kind; // 符号种类
    Type type; // 变量/结构体的类型；函数名时可为 NULL
    FuncSig *func; // 函数信息；非函数时为 NULL
    int depth; // 作用域深度（0 为全局，逐层 +1）
    int is_current_scope; // 新增：标记是否是当前作用域的定义
    // 哈希桶链 & 作用域链（头插法）
    struct Symbol *hash_next;
    struct Symbol *scope_next;
} Symbol;

/*==================== 生命周期 ====================*/
void symtab_init(void);
void symtab_enter_scope(void);
void symtab_leave_scope(void);
int symtab_current_depth(void);

/*==================== 插入/查找 ===================*/
/**
* 插入符号：
* - 若当前作用域已存在同名符号，返回 0（不插入）。
* - 否则插入并返回 1。
*/
int symtab_insert(Symbol *s);

/** 最近可见定义（按最近定义优先） */
Symbol *symtab_lookup(const char *name);

/** 仅在当前作用域查找（用于判重） */
Symbol *symtab_lookup_in_current_scope(const char *name);

/*==================== 构造辅助 ====================*/
Symbol *sym_make_var(const char *name, Type t);
Symbol *sym_make_struct_tag(const char *name, Type t);
Symbol *sym_make_func_decl(const char *name, FuncSig *f); // 声明：declared_only=1
Symbol *sym_make_func_def(const char *name, FuncSig *f); // 定义：declared_only=0

FuncSig *funcsig_make(Type ret, int argc, Type *argv, int declared_only);
void funcsig_free(FuncSig *f);

/*==================== 其他工具 ====================*/
void free_symbol(Symbol *s);
unsigned hash_pjw(const char *name);

void symtab_print_all(void);
void symtab_print_current_scope(void);

#ifdef __cplusplus
}
#endif

#endif // SYMBOL_TABLE_H