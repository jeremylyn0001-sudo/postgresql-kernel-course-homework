#include "list.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int valid_data(int size, const char *mem)
{
    return size >= 0 && (size_t)size <= LINK_NODE_MAX_BYTES - sizeof(LinkData) &&
           (size == 0 || mem != NULL);
}

static LinkData *make_node(int size, const char *mem)
{
    if (!valid_data(size, mem)) {
        errno = EINVAL;
        return NULL;
    }
    size_t bytes = sizeof(LinkData) + (size_t)size;
    LinkData *node = malloc(bytes < LINK_NODE_MIN_BYTES ? LINK_NODE_MIN_BYTES : bytes);
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    node->type = 0;
    node->dataSize = size;
    node->data = (char *)(node + 1);
    if (size)
        memcpy(node->data, mem, (size_t)size);
    return node;
}

LinkData *InitLink(int size, char *mem)
{
    LinkData *node = make_node(size, mem);
    if (node) {
        node->prev = node;
        node->next = node;
    }
    return node;
}

void AppendNode(LinkData *firstNode, int size, char *mem)
{
    if (!firstNode) {
        errno = EINVAL;
        return;
    }
    LinkData *node = make_node(size, mem);
    if (!node)
        return;
    LinkData *tail = firstNode->prev;
    node->prev = tail;
    node->next = firstNode;
    tail->next = node;
    firstNode->prev = node;
}

LinkData *FindNode(LinkData *firstNode, int size, char *mem)
{
    if (!firstNode || !valid_data(size, mem))
        return NULL;
    LinkData *node = firstNode;
    do {
        if (node->dataSize == size && (size == 0 || memcmp(node->data, mem, (size_t)size) == 0))
            return node;
        node = node->next;
    } while (node != firstNode);
    return NULL;
}

void FreeLink(LinkData *firstNode)
{
    if (!firstNode)
        return;
    LinkData *node = firstNode->next;
    while (node != firstNode) {
        LinkData *next = node->next;
        free(node);
        node = next;
    }
    free(firstNode);
}
