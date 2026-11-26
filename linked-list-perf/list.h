#ifndef LIST_H
#define LIST_H

enum { LINK_NODE_MIN_BYTES = 32, LINK_NODE_MAX_BYTES = 512 };

typedef struct LinkData {
    struct LinkData *prev;
    struct LinkData *next;
    int type;
    int dataSize;
    char *data;
} LinkData;

LinkData *InitLink(int size, char *mem);
/* On failure, leaves the ring unchanged and sets errno. */
void AppendNode(LinkData *firstNode, int size, char *mem);
LinkData *FindNode(LinkData *firstNode, int size, char *mem);
void FreeLink(LinkData *firstNode);

#endif
