#ifndef NODE_H
#define NODE_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "type.h"

typedef struct Node{
    int lineno;
    char *name;
    char *value;
    struct Node *child;
    struct Node *nxt;
}Node;

extern Node *root;

Node* CreateNode(const char *name, int lineno);
Node* CreateBinary(int lineno, const char *nm, Node *lft, Node *rgt);
Node* CreateFunc(int lineno, const char *nm, Node *args);
Node* CreateOp(int lineno, const char *nm, Node* n);
void AddChild(Node *prt, Node *child);
void Print(Node *root, int high);
void Delete(Node *root);

#endif