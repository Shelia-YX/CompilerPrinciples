#include <stdio.h>

extern FILE *yyin;

int main(int argc, char **argv){
    if(argc <= 1) return 1;
    else{
        FILE *f = fopen(argv[1], "r");
        if(!f){
            perror(argv[1]);
            return 1;
        }
        // 正常情况
    }
    return 0;
}
