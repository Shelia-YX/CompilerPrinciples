#include "type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*--------------------- 内部工具 ---------------------*/
static void *xmalloc(size_t n){
    void *p = malloc(n);
    if(!p){ 
        fprintf(stderr, "[type] OOM\n"); 
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s){
    if(!s) return NULL;
    size_t n = strlen(s)+1;
    char *p = (char*)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/*--------------------- 构造函数 ---------------------*/
Type type_make_basic(BasicKind k){
    Type t = (Type)xmalloc(sizeof(*t));
    t->kind = T_BASIC;
    t->u.basic = k;
    return t;
}

Type type_make_array(Type elem, int size){
    if(size <= 0) size = 0; // 容错，保持不为负
    Type t = (Type)xmalloc(sizeof(*t));
    t->kind = T_ARRAY;
    t->u.array.elem = elem; // 引用，不拷贝（由调用方决定共享策略）
    t->u.array.size = size;
    return t;
}

Type type_make_struct(FieldList fields){
    Type t = (Type)xmalloc(sizeof(*t));
    t->kind = T_STRUCT;
    t->u.structure = fields; // 接管所有权
    return t;
}

FieldList field_make(const char *name, Type type, FieldList tail){
    FieldList f = (FieldList)xmalloc(sizeof(*f));
    f->name = xstrdup(name);
    f->type = type; // 引用，不拷贝
    f->tail = tail;
    return f;
}

/*--------------------- 深拷贝 ---------------------*/

static FieldList fieldlist_deepcopy(FieldList f){
    if(!f) return NULL;
    // 递归复制，保持顺序
    FieldList tail_copy = fieldlist_deepcopy(f->tail);
    Type tcopy = type_deepcopy(f->type);
    return field_make(f->name, tcopy, tail_copy);
}

Type type_deepcopy(const Type t){
    if(!t) return NULL;
    switch(t->kind){
        case T_BASIC:
            return type_make_basic(t->u.basic);
        case T_ARRAY: {
            Type elem_copy = type_deepcopy(t->u.array.elem);
            return type_make_array(elem_copy, t->u.array.size);
        }
        case T_STRUCT: {
            FieldList copy = fieldlist_deepcopy(t->u.structure);
            return type_make_struct(copy);
        }
        default:
            return NULL;
    }
}

void fieldlist_free(FieldList f){
    while(f){
        FieldList nxt = f->tail;
        if(f->name) free(f->name);
        if(f->type) type_free(f->type);
        free(f);
        f = nxt;
    }
}

void type_free(Type t){
    if(!t) return;
    switch(t->kind){
        case T_BASIC: break;
        case T_ARRAY:
            if(t->u.array.elem) type_free(t->u.array.elem);
            break;
        case T_STRUCT:
            fieldlist_free(t->u.structure);
            break;
    }
    free(t);
}




static bool type_equal_structural(const Type a, const Type b){
    if(a == b) return true; // 快速路径
    if(!a || !b) return false;
    if(a->kind != b->kind) return false;
    switch(a->kind){
        case T_BASIC: 
            return a->u.basic == b->u.basic;
        case T_ARRAY: // 结构等价：数组大小必须一致，且元素类型结构等价
            return a->u.array.size == b->u.array.size && type_equal_structural(a->u.array.elem, b->u.array.elem);
        case T_STRUCT: {
            FieldList fa = a->u.structure, fb = b->u.structure;
            for(;;){
                if(!fa && !fb) return true;
                if(!fa || !fb) return false;
// 结构等价：不强制域名相同，但通常实验要求域名与顺序也一致
// 这里按“域数与顺序一致 + 每个域类型结构等价”实现；若需忽略域名，将下面名称比较删掉
                if((fa->name==NULL) != (fb->name==NULL)) return false;
                if(fa->name && fb->name && strcmp(fa->name, fb->name)!=0) return false;
                if(!type_equal_structural(fa->type, fb->type)) return false;
                fa = fa->tail; fb = fb->tail;
            }
        }
    }
    return false;
}

static bool type_equal_nominal(const Type a, const Type b){
    if(a == b) return true;
    if(!a || !b) return false;
    if(a->kind != b->kind) return false;
    switch(a->kind){
        case T_BASIC:
            return a->u.basic == b->u.basic;
        case T_ARRAY:
            return a->u.array.size == b->u.array.size
                && type_equal_nominal(a->u.array.elem, b->u.array.elem);
        case T_STRUCT: {
            FieldList fa = a->u.structure, fb = b->u.structure;
            for(;;){
                if(!fa && !fb) return true;
                if(!fa || !fb) return false;
                // 名字与类型（名等价）都一致
                if((fa->name==NULL)!=(fb->name==NULL)) return false;
                if(fa->name && fb->name && strcmp(fa->name, fb->name)!=0) return false;
                if(!type_equal_nominal(fa->type, fb->type)) return false;
                fa = fa->tail; fb = fb->tail;
            }
        }
    }
    return false;
}


bool type_equal(const Type a, const Type b, bool structural){
    return structural ? type_equal_structural(a, b) : type_equal_nominal(a, b);
}

/*--------------------- 工具 ---------------------*/
bool type_is_int(const Type t){ return t && t->kind==T_BASIC && t->u.basic==TY_INT; }
bool type_is_float(const Type t){ return t && t->kind==T_BASIC && t->u.basic==TY_FLOAT; }
bool type_is_numeric(const Type t){ return type_is_int(t) || type_is_float(t); }

bool type_is_array(const Type t){ return t && t->kind==T_ARRAY; }
bool type_is_struct(const Type t){ return t && t->kind==T_STRUCT; }

Type type_array_elem(const Type t){ return (t && t->kind==T_ARRAY) ? t->u.array.elem : NULL; }


static void type_print_internal(const Type t){
    if(!t){ fputs("<null>", stdout); return; }
    switch(t->kind){
        case T_BASIC:
            fputs(t->u.basic==TY_INT?"int":"float", stdout);
            break;
        case T_ARRAY:
            type_print_internal(t->u.array.elem);
            printf("[%d]", t->u.array.size);
            break;
        case T_STRUCT: {
            fputs("struct{ ", stdout);
            FieldList f = t->u.structure; bool first=true;
            while(f){
                if(!first) fputs("; ", stdout); first=false;
                if(f->name){ fputs(f->name, stdout); fputs(": ", stdout); }
                type_print_internal(f->type);
                f = f->tail;
            }
            fputs(" }", stdout);
            break;
        }
    }
}

void type_print(const Type t){ type_print_internal(t); }