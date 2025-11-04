%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "syntax.tab.h"

int yycolumn = 1;
int lne[4096] = {0};

#define YY_USER_ACTION \
    yylloc.first_line = yylloc.last_line = yylineno; \
    yylloc.first_column = yycolumn; \
    yylloc.last_column = yycolumn + yyleng -1; \
    yycolumn += yyleng;

extern int lexical_error;
%}

%option yylineno

digit   [0-9]
letter  [_a-zA-Z]
int     [1-9]{digit}*|0
float   {digit}+\.{digit}+
id      {letter}({digit}|{letter})*

%%
[ \t\r]+   ;
\n         { yycolumn = 1;}
"struct" {return STRUCT;}
"return" {return RETURN;}
"if"    {return IF;}
"else"  {return ELSE;}
"while" {return WHILE;}
";"     {return SEMI;}
","     {return COMMA;}
"="     {return ASSIGNOP;}
">="|"<="|"=="|"!="|">"|"<" {return RELOP;}
"+"     {return PLUS;}
"-"     {return MINUS;}
"*"     {return STAR;}
"/"     {return DIV;}
"&&"    {return AND;}
"||"    {return OR;}
"."     {return DOT;}
"!"     {return NOT;}
"int"   {yylval.sval = strdup(yytext); return TYPE;}
"float" {yylval.sval = strdup(yytext); return TYPE;}
"("     {return LP;}
")"     {return RP;}
"["     {return LB;}
"]"     {return RB;}
"{"     {return LC;}
"}"     {return RC;}

{float} {yylval.fval = atof(yytext); return FLOAT;}
{int}   {yylval.ival = atoi(yytext); return INT;}
{id}    {yylval.sval = strdup(yytext); return ID;}

"//"    {
    char c = input();
    while(c != '\n'){
        c = input();
    }
}

"/*"    {
    char a = input(), b = input();
    while(!(a == '*' && b == '/')){
        a = b;
        b = input();
    }
}

. {
    printf("Error type A at Line %d: Mysterious character '%s'.\n",
    yylineno, yytext);
    lexical_error = 1;
    lne[yylineno] = 1;
}
%%