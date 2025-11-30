#include "node.h"

Node *root = NULL;

Node* CreateNode(const char *name, int lineno){
    Node *node = (Node*)malloc(sizeof(Node));
    node->lineno = lineno;
    node->name = xstrdup(name);
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
    tid->value = xstrdup(nm);
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