#ifndef TYPE_H
#define TYPE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 基本类型种类 */
typedef enum {
    TY_INT = 0,
    TY_FLOAT = 1
} BasicKind;


/** 顶层类型标签 */
typedef enum {
    T_BASIC = 0,
    T_ARRAY = 1,
    T_STRUCT = 2
} TypeKind;


/** 前置声明 */
typedef struct Type_ *Type;
typedef struct FieldList_ *FieldList;


/** 结构体域链表 */
struct FieldList_ {
    char *name; // 域名（可为 NULL，用于匿名或占位）
    Type type; // 域类型
    FieldList tail; // 下一个域
};


/** 类型描述 */
struct Type_ {
    TypeKind kind;
    union {
        BasicKind basic; // 基本类型
        struct { Type elem; int size; } array; // 数组：元素类型 + 大小（>0）
        FieldList structure; // 结构体：域链表（有序）
    } u;
};

char *xstrdup(const char *s);

/*======================== 构造/销毁 ========================*/
Type type_make_basic(BasicKind k);
Type type_make_array(Type elem, int size);
Type type_make_struct(FieldList fields);

FieldList field_make(const char *name, Type type, FieldList tail);
Type type_deepcopy(const Type t);

void type_free(Type t);
void fieldlist_free(FieldList f);


/*======================== 判等/工具 ========================*/
/**
* 类型等价判定：
* - structural==false：名等价/指针等价（STRUCT 按指针、数组按尺寸 + 元素指针等价）
* - structural==true ：结构等价（STRUCT 逐域比较，数组递归比较）
*/
bool type_equal(const Type a, const Type b, bool structural);


/** 是否为整型/浮点/数值 */
bool type_is_int(const Type t);
bool type_is_float(const Type t);
bool type_is_numeric(const Type t);


/** 数组/结构体的便捷判断 */
bool type_is_array(const Type t);
bool type_is_struct(const Type t);


/** 获取数组元素类型（若非数组返回 NULL） */
Type type_array_elem(const Type t);


/** 打印（调试用），输出到 stdout，单行不换行 */
void type_print(const Type t);


#ifdef __cplusplus
}
#endif


#endif // TYPE_H