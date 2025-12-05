#include <stdio.h>
#include "syntax.tab.h"
#include "./IR/translate.h"
#include "./IR/temp.h"
#include "./IR/ir.h"

extern FILE *yyin;
extern Node* root;
extern void Delete(Node *root);
extern int lexical_error;
extern int syntax_error;

int main(int argc, char **argv){
    if(argc < 2){
        fprintf(stderr, "Usage: %s input.cmm [output.ir]\n", argv[0]);
        fprintf(stderr, "       If output.ir is omitted, result will be printed to stdout\n");
        return 1;
    }

    /* 1. 打开输入文件 */
    FILE *f = fopen(argv[1], "r");
    if(!f){
        perror(argv[1]);
        return 1;
    }
    yyrestart(f);

    lexical_error = 0;
    syntax_error  = 0;
    int result = yyparse();

    temp_init();
    // Print(root, 0);

    IRList code = translate_Program(root);

    FILE *out = stdout;  // 默认输出到终端
    
    if(argc >= 3){
        // 如果有第三个参数，输出到文件
        out = fopen(argv[2], "w");
        if(!out){
            perror(argv[2]);
            fclose(f);
            return 1;
        }
    }
    
    if(!has_fatal_error){
        irlist_print(out, code);
    }else{
        printf("由于之前的错误，未生成中间代码。\n");
    }

    
    // 如果打开了文件，需要关闭它
    if(out != stdout){
        fclose(out);
    }
    
    irlist_free(code);

    if(root != NULL){
        Delete(root);
        root = NULL;
    }
    fclose(f);
    return 0;
}