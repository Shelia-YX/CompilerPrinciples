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
    if(argc < 3){
        fprintf(stderr, "Usage: %s input.cmm output.ir\n", argv[0]);
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
    IRList code = translate_Program(root);

    FILE *out = fopen(argv[2], "w");
    if(!out){
        perror(argv[2]);
        fclose(f);
        return 1;
    }
    irlist_print(out, code);
    fclose(out);
    irlist_free(code);

    if(root != NULL){
        Delete(root);
        root = NULL;
    }
    fclose(f);
    return 0;
}
