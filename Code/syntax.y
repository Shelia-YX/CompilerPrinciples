%code requires{
    typedef struct Node Node;
}

%union{
    int ival;
    float fval;
    char *sval;
    Node *node; 
}

%code{
    #include <stdio.h>
    #include "lex.yy.c"

    void yyerror(const char *s);
    extern int lne[4096];

    typedef struct Node{
        int lineno;
        char *name;
        char *value;
        struct Node *child;
        struct Node *nxt;
    }Node;

    Node *root = NULL;

    Node* CreateNode(const char *name, int lineno);
    Node* CreateBinary(int lineno, const char *nm, Node *lft, Node *rgt);
    Node* CreateFunc(int lineno, const char *nm, Node *args);
    Node* CreateOp(int lineno, const char *nm, Node* n);
    void AddChild(Node *prt, Node *child);
    void Print(Node *root, int high);
    void Delete(Node *root);

    int lexical_error = 0;
    int syntax_error = 0;
}

%error-verbose
%locations

%token <ival> INT
%token <fval> FLOAT
%token <sval> ID TYPE
%token PLUS MINUS STAR DIV
%token ASSIGNOP RELOP
%token COMMA SEMI LP RP LC RC DOT LB RB
%token IF ELSE WHILE RETURN STRUCT

%type <node> Program ExtDefList ExtDef Specifier StructSpecifier VarDec ExtDecList
%type <node> OptTag DefList Tag
%type <node> FunDec VarList ParamDec
%type <node> CompSt StmtList Stmt
%type <node> Def DecList Dec Exp Args

%start Program

%left COMMA
%right ASSIGNOP
%left OR
%left AND
%left RELOP
%left PLUS MINUS
%left STAR DIV
%right NOT

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

Program:ExtDefList {
    $$ = CreateNode("Program", @1.first_line);
    root = $$;
    AddChild($$, $1);
};

ExtDefList: ExtDef ExtDefList{
    $$ = CreateNode("ExtDefList",@1.first_line);
    AddChild($$,$1);
    if($2) AddChild($$,$2);
}
|{
    $$ = NULL;
};

ExtDef: Specifier ExtDecList SEMI{
    $$ = CreateNode("ExtDef", @1.first_line);
    AddChild($$,$1);
    AddChild($$,$2);
    AddChild($$,CreateNode("SEMI",-1));
}
| Specifier SEMI{
    $$ = CreateNode("ExtDef",@1.first_line);
    AddChild($$,$1);
    AddChild($$,CreateNode("SEMI",-1));
}
| Specifier FunDec CompSt{
    $$ = CreateNode("ExtDef", @1.first_line);
    AddChild($$, $1);
    AddChild($$, $2);
    AddChild($$, $3);
}
| error SEMI{$$ = CreateNode("ERROR", -1);}
| Specifier error SEMI{$$ = CreateNode("ERROR", -1);}
| error Specifier SEMI{$$ = CreateNode("ERROR", -1);};

ExtDecList: VarDec{
    $$ = CreateNode("ExtDecList", @1.first_line);
    AddChild($$, $1);
}
| VarDec COMMA ExtDecList{
    $$ = CreateNode("ExtDecList", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("COMMA",-1));
    AddChild($$, $3);
}
| VarDec error ExtDecList{$$ = CreateNode("ERROR", -1);}
| error {$$ = CreateNode("ERROR", -1);};

Specifier: TYPE{
    $$ = CreateNode("Specifier", @1.first_line);
    Node* tmp = CreateNode("TYPE",-1);
    if($1) tmp->value = strdup($1);
    AddChild($$, tmp);
}
| StructSpecifier{
    $$ = CreateNode("Specifier", @1.first_line);
    AddChild($$, $1);
}
| error{$$ = CreateNode("ERROR", -1);};

StructSpecifier: STRUCT OptTag LC DefList RC{
    $$ = CreateNode("StructSpecifier", @1.first_line);
    AddChild($$, CreateNode("STRUCT", -1));
    AddChild($$, $2);
    AddChild($$, CreateNode("LC", -1));
    AddChild($$, $4);
    AddChild($$, CreateNode("RC", -1));
}
| STRUCT OptTag LC error RC{$$ = CreateNode("ERROR", -1);}
| STRUCT Tag{
    $$ = CreateNode("StructSpecifier", @1.first_line);
    AddChild($$, CreateNode("STRUCT", -1));
    AddChild($$, $2);
};

OptTag: ID{
    $$ = CreateNode("OptTag", @1.first_line);
    Node* tmp = CreateNode("ID", -1);
    tmp->value = strdup($1);
    AddChild($$, tmp);
}
| {
    $$ = NULL;
};

Tag: ID{
    $$ = CreateNode("Tag", @1.first_line);
    Node* tmp = CreateNode("ID", -1);
    tmp->value = strdup($1);
    AddChild($$, tmp);
};

VarDec: ID{
    $$ = CreateNode("VarDec", @1.first_line);
    Node* tmp = CreateNode("ID", -1);
    tmp->value = strdup($1);
    AddChild($$, tmp);
}
| VarDec LB INT RB{
    $$ = CreateNode("VarDec", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("LB", -1));
    Node* tmp = CreateNode("INT", -1);
    char buffer[32];
    if($3){
        sprintf(buffer, "%d", $3);
        tmp->value = strdup(buffer);
    }
    AddChild($$, tmp);
    AddChild($$, CreateNode("RB", -1));
}
| VarDec LB INT error RB{$$ = CreateNode("ERROR", -1);};

FunDec: ID LP VarList RP{
    $$ = CreateNode("FunDec", @1.first_line);
    Node* tmp = CreateNode("ID", -1);
    tmp->value = strdup($1);
    AddChild($$, tmp);
    AddChild($$, CreateNode("LP", -1));
    AddChild($$, $3);
    AddChild($$, CreateNode("RP", -1));
}
| ID LP error RP{$$ = CreateNode("ERROR", -1);}
| ID LP RP{
    $$ = CreateNode("FunDec", @1.first_line);
    Node* tmp = CreateNode("ID", -1);
    tmp->value = strdup($1);
    AddChild($$, tmp);
    AddChild($$, CreateNode("LP", -1));
    AddChild($$, CreateNode("RP", -1));
}
| error LP VarList RP{$$ = CreateNode("ERROR", -1);};

VarList: ParamDec COMMA VarList{
    $$ = CreateNode("VarList", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("COMMA", -1));
    AddChild($$, $3);
}
| ParamDec COMMA error{$$ = CreateNode("ERROR", -1);}
| ParamDec{
    $$ = CreateNode("VarList", @1.first_line);
    AddChild($$, $1);
}
| error {$$ = CreateNode("ERROR", -1);};

ParamDec: Specifier VarDec{
    $$ = CreateNode("ParamDec", @1.first_line);
    AddChild($$, $1);
    AddChild($$, $2);
};

CompSt: LC DefList StmtList RC{
    $$ = CreateNode("CompSt", @1.first_line);
    AddChild($$, CreateNode("LC", -1));
    AddChild($$, $2);
    AddChild($$, $3);
    AddChild($$, CreateNode("RC", -1));
}
| LC error RC{$$ = CreateNode("ERROR", -1);};

StmtList: Stmt StmtList{
    $$ = CreateNode("StmtList", @1.first_line);
    AddChild($$, $1);
    AddChild($$, $2);
}
| {
    $$ = NULL;
};

Stmt: Exp SEMI{
    $$ = CreateNode("Stmt", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("SEMI", -1));
}
| Exp error SEMI{$$ = CreateNode("ERROR", -1);}
| CompSt{
    $$ = CreateNode("Stmt", @1.first_line);
    AddChild($$, $1);
}
| RETURN Exp SEMI{
    $$ = CreateNode("Stmt", @1.first_line);
    AddChild($$, CreateNode("RETURN", -1));
    AddChild($$, $2);
    AddChild($$, CreateNode("SEMI", -1));
}
| RETURN Exp error{$$ = CreateNode("ERROR", -1);}
| IF LP Exp RP Stmt{
    $$ = CreateNode("Stmt", @1.first_line);
    AddChild($$, CreateNode("IF", -1));
    AddChild($$, CreateNode("LP", -1));
    AddChild($$, $3);
    AddChild($$, CreateNode("RP", -1));
    AddChild($$, $5);
}
| IF LP Exp RP Stmt ELSE Stmt{
    $$ = CreateNode("Stmt", @1.first_line);
    AddChild($$, CreateNode("IF", -1));
    AddChild($$, CreateNode("LP", -1));
    AddChild($$, $3);
    AddChild($$, CreateNode("RP", -1));
    AddChild($$, $5);
    AddChild($$, CreateNode("ELSE", -1));
    AddChild($$, $7);
}
| IF LP Exp RP error ELSE Stmt {$$ = CreateNode("ERROR", -1);}
| WHILE LP Exp RP Stmt{
    $$ = CreateNode("Stmt", @1.first_line);
    AddChild($$, CreateNode("WHILE", -1));
    AddChild($$, CreateNode("LP", -1));
    AddChild($$, $3);
    AddChild($$, CreateNode("RP", -1));
    AddChild($$, $5);
}
| WHILE LP Exp error RP Stmt{$$ = CreateNode("ERROR", -1);}
| error SEMI{$$ = CreateNode("ERROR", -1);}
| error RC{$$ = CreateNode("ERROR", -1);}
| Exp error SEMI{$$ = CreateNode("ERROR", -1);}
| RETURN Exp error{$$ = CreateNode("ERROR", -1);};

DefList: Def DefList{
    $$ = CreateNode("DefList", @1.first_line);
    AddChild($$, $1);
    AddChild($$, $2);
}
| {
    $$ = NULL;
}; 

Def: Specifier DecList SEMI{
    $$ = CreateNode("Def", @1.first_line);
    AddChild($$, $1);
    AddChild($$, $2);
    AddChild($$, CreateNode("SEMI", -1));
}
| Specifier DecList error{$$ = CreateNode("ERROR", -1);};

DecList: Dec{
    $$ = CreateNode("DecList", @1.first_line);
    AddChild($$, $1);
}
| Dec COMMA DecList{
    $$ = CreateNode("DecList", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("COMMA", -1));
    AddChild($$, $3);
}
| Dec COMMA error{$$ = CreateNode("ERROR", -1);};

Dec: VarDec{
    $$ = CreateNode("Dec", @1.first_line);
    AddChild($$, $1);    
}
| VarDec ASSIGNOP Exp{
    $$ = CreateNode("Dec", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("ASSIGNOP", -1));
    AddChild($$, $3);
};

Exp: Exp ASSIGNOP Exp { $$ = CreateBinary(@2.first_line, "ASSIGNOP", $1, $3); }
| Exp AND Exp         { $$ = CreateBinary(@2.first_line, "AND", $1, $3); }
| Exp OR Exp          { $$ = CreateBinary(@2.first_line, "OR", $1, $3); }
| Exp RELOP Exp       { $$ = CreateBinary(@2.first_line, "RELOP", $1, $3); }
| Exp PLUS Exp        { $$ = CreateBinary(@2.first_line, "PLUS", $1, $3); }
| Exp MINUS Exp       { $$ = CreateBinary(@2.first_line, "MINUS", $1, $3); }
| Exp STAR Exp        { $$ = CreateBinary(@2.first_line, "STAR", $1, $3); }
| Exp DIV Exp         { $$ = CreateBinary(@2.first_line, "DIV", $1, $3); }
| LP Exp RP           { $$ = CreateNode("Exp", @2.first_line);
                        AddChild($$,CreateNode("LP",-1));
                        AddChild($$, $2);
                        AddChild($$, CreateNode("RP", -1));  
                    }
| MINUS Exp           { $$ = CreateOp(@1.first_line, "MINUS", $2); }
| NOT Exp             { $$ = CreateOp(@1.first_line, "NOT", $2); }
| ID LP Args RP       { $$ = CreateFunc(@1.first_line, $1, $3); }
| ID LP RP            { $$ = CreateFunc(@1.first_line, $1, NULL); }
| Exp LB Exp RB       { $$ = CreateNode("Exp", @2.first_line);
                        AddChild($$, $1);
                        AddChild($$, CreateNode("LB", -1));
                        AddChild($$, $3);
                        AddChild($$, CreateNode("RB", -1)); }
| Exp DOT ID          { $$ = CreateNode("Exp", @2.first_line);
                        AddChild($$, $1);
                        AddChild($$, CreateNode("DOT", -1));
                        Node *tid = CreateNode("ID", -1);
                        tid->value = strdup($3);
                        AddChild($$, tid); }
| ID                  { $$ = CreateNode("Exp", @1.first_line);
                        Node *tid = CreateNode("ID", -1);
                        tid->value = strdup($1);
                        AddChild($$, tid); }
| INT                 { $$ = CreateNode("Exp", @1.first_line);
                        Node *tin = CreateNode("INT",-1);
                        char buffer[32];
                        sprintf(buffer, "%d", $1);
                        tin->value = strdup(buffer);
                        AddChild($$,tin); }
| FLOAT               { $$ = CreateNode("Exp", @1.first_line);
                        Node *tf = CreateNode("FLOAT",-1);
                        char buffer[32];
                        sprintf(buffer, "%f", $1);
                        tf->value = strdup(buffer);
                        AddChild($$,tf); }
| Exp ASSIGNOP error  { $$ = CreateNode("ERROR", -1);}
| Exp AND error       { $$ = CreateNode("ERROR", -1);}
| Exp OR error        { $$ = CreateNode("ERROR", -1);}
| Exp RELOP error     { $$ = CreateNode("ERROR", -1);}
| Exp PLUS error      { $$ = CreateNode("ERROR", -1);}
| Exp MINUS error     { $$ = CreateNode("ERROR", -1);}
| Exp STAR error      { $$ = CreateNode("ERROR", -1);}
| Exp DIV error       { $$ = CreateNode("ERROR", -1);}
| LP error RP         { $$ = CreateNode("ERROR", -1);}
| MINUS error         { $$ = CreateNode("ERROR", -1);}
| NOT error           { $$ = CreateNode("ERROR", -1);}
| ID LP error RP      { $$ = CreateNode("ERROR", -1);}
| Exp LB error RB     { $$ = CreateNode("ERROR", -1);}
| ID error            { $$ = CreateNode("ERROR", -1);}
| INT error           { $$ = CreateNode("ERROR", -1);}
| FLOAT error         { $$ = CreateNode("ERROR", -1);}
| Exp error           { $$ = CreateNode("ERROR", -1);}
| INT DOT ID          { yyerror("syntax error."); YYERROR;}
| error               { $$ = CreateNode("ERROR", -1);}
;

Args: Exp COMMA Args {
    $$ = CreateNode("Args", @1.first_line);
    AddChild($$, $1);
    AddChild($$, CreateNode("COMMA", -1));
    AddChild($$, $3);
}
| Exp {
    $$ = CreateNode("Args", @1.first_line);
    AddChild($$, $1);
}
| error RP{
    yyerrok;
    {$$ = CreateNode("ERROR", -1);}
};

%%

void yyerror(const char *s){
    if(lne[yylineno])    return;
    lne[yylineno] = 1;
    syntax_error = 1;
    int line = yylloc.first_line ? yylloc.first_line : yylineno;
    fprintf(stdout, "Error type B at Line %d: syntax error.\n",line);
}

Node* CreateNode(const char *name, int lineno){
    Node *node = (Node*)malloc(sizeof(Node));
    node->lineno = lineno;
    node->name = strdup(name);
    node->value = NULL;
    node->child = NULL;
    node->nxt = NULL;
    return node;
}

Node* CreateBinary(int lineno, const char *nm, Node *lft, Node *rgt){
    Node *tmp = CreateNode("Exp", lineno);
    AddChild(tmp, lft);
    Node *op = CreateNode(nm, -1);
    AddChild(tmp, op);
    AddChild(tmp, rgt);
    return tmp;
}

Node* CreateOp(int lineno, const char *nm, Node* n){
    Node *tmp = CreateNode("Exp", lineno);
    Node *op = CreateNode(nm, -1);
    AddChild(tmp, op);
    AddChild(tmp, n);
    return tmp;
}

Node* CreateFunc(int lineno, const char *nm, Node *args){
    Node *tmp = CreateNode("Exp", lineno);
    Node *tid = CreateNode("ID", -1);
    tid->value = strdup(nm);
    AddChild(tmp, tid);
    AddChild(tmp, CreateNode("LP", -1));
    if(args)    AddChild(tmp, args);
    AddChild(tmp, CreateNode("RP", -1));
    return tmp;
}

void AddChild(Node *prt, Node *child){
    if(prt == NULL || child == NULL)    return;
    if(prt->child == NULL)    prt->child = child;
    else{
        Node *tmp = prt->child;
        while(tmp->nxt != NULL)    tmp = tmp->nxt;
        tmp->nxt = child;
    }
}

void Print(Node* root, int indent){
    if(root == NULL) return;
    for (int i = 0; i < indent; i++)    printf("  ");
    if(root->lineno > 0)    printf("%s (%d)\n",root->name, root->lineno);
    else if(root->value != NULL){
        if(strcmp(root->name, "TYPE") == 0)    printf("TYPE: %s\n", root->value);
        else if (strcmp(root->name, "ID") == 0)    printf("ID: %s\n", root->value);
        else if (strcmp(root->name, "INT") == 0)    printf("INT: %s\n", root->value);
        else if (strcmp(root->name, "FLOAT") == 0)    printf("FLOAT: %s\n", root->value);
        else    printf("%s: %s\n", root->name, root->value);
    } 
    else    printf("%s\n",root->name);
    Print(root->child, indent+1);
    Print(root->nxt, indent);
}

void Delete(Node* root){
    if(root == NULL) return;
    Delete(root->child);
    Delete(root->nxt);
    free(root->name);
    if(root->value != NULL)    free(root->value);
    free(root);
}