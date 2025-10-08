/* include part */
%{
    ;
%}

/* definition part */
%option yylineno

digit   [0-9]
letter  [_a-zA-Z]
int     [1-9]{digit}*
float   {digit}+\.{digit}+
id      {letter}{{digit}|{letter}}*

%%
/* rules */
"struct"{printf("STRUCT\n"); return STRUCT;}
"return"{printf("RETURN\n"); return RETURN;}
"if"    {printf("IF\n"); return IF;}
"else"  {printf("ELSE\n"); return ELSE;}
"while" {printf("WHILE\n"); return WHILE;}
";"     {printf("SEMI\n"); return SEMI;}
","     {printf("COMMA\n"); return COMMA;}
"="     {printf("ASSIGNOP\n"); return ASSIGNOP;}
">="|"<="|"=="|"!="|">"|"<"
        {printf("RELOP\n"); return RELOP;}
"+"     {printf("PLUS\n"); return PLUS;}
"-"     {printf("MINUS\n"); return MINUS;}
"*"     {printf("STAR\n"); return STAR;}
"/"     {printf("DIV\n"); return DIV;}
"&&"    {printf("AND\n"); return AND;}
"||"    {printf("OR\n"); return OR;}
"."     {printf("DOT\n"); return DOT;}
"!"     {printf("NOT\n"); return NOT;}
"int"|"float"
        {printf("TYPE\n"); return TYPE;}
"("     {printf("LP\n"); return LP;}
")"     {printf("RP\n"); return RP;}
"["     {printf("LB\n"); return LB;}
"]"     {printf("RB\n"); return RB;}
"{"     {printf("LC\n"); return LC;}
"}"     {printf("RC\n"); return RC;}

{id}    {yylval.sval = strdup(yytext); return ID;}
{float} {yylval.fval = atof(yytext); return FLOAT;}
{int}   {yylval.ival = atoi(yytext); return INT;}

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

%%
/* other */