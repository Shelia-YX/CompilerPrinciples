#ifndef SEMANTIC_H
#define SEMANTIC_H


#include "type.h"
#include "node.h" // <- 使用你提供的 Node 结构
#include "symbol_table.h"

#ifdef __cplusplus
extern "C" {
#endif


// 初始化语义分析（清空符号表、错误计数等）
void semantic_init(void);


// 从 Program 结点开始进行语义分析
void semantic_analyze(Node* root);


// 返回累计的语义错误数量
int semantic_error_count(void);


#ifdef __cplusplus
}
#endif


#endif // SEMANTIC_H
