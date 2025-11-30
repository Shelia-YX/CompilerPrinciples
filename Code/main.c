#include <stdio.h>
#include "syntax.tab.h"

extern FILE *yyin;
extern Node* root;
extern void Print(Node *root, int high);
extern void Delete(Node *root);
extern int lexical_error;
extern int syntax_error;

int main(int argc, char **argv){
    if(argc <= 1) return 1;
    else{
        FILE *f = fopen(argv[1], "r");
        if(!f){
            perror(argv[1]);
            return 1;
        }
        // 正常情况
        lexical_error = 0;
        syntax_error = 0;
        yyrestart(f);
        int result = yyparse();
        if(result == 0 && lexical_error == 0 && syntax_error == 0){
            semantic_init();
            semantic_analyze(root);
        }
        if(root!=NULL){
            Delete(root);
            root = NULL;
        }
        fclose(f);
    }
    return 0;
}