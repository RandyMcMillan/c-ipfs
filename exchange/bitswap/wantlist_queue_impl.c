#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct wantlist_node {
    char cid_str[64];
    int priority;
    struct wantlist_node *next;
} wantlist_node_t;

typedef struct {
    wantlist_node_t *head;
    wantlist_node_t *tail;
    size_t size;
    pthread_mutex_t lock;
} wantlist_queue_t;

wantlist_queue_t *wantlist_queue_new(void) {
    wantlist_queue_t *q = calloc(1, sizeof(wantlist_queue_t));
    if (!q) return NULL;
    pthread_mutex_init(&q->lock, NULL);
    return q;
}

int wantlist_queue_add(wantlist_queue_t *q, const char *cid_str, int priority) {
    if (!q || !cid_str) return 0;
    pthread_mutex_lock(&q->lock);

    wantlist_node_t *curr = q->head;
    while (curr) {
        if (strcmp(curr->cid_str, cid_str) == 0) {
            curr->priority = priority;
            pthread_mutex_unlock(&q->lock);
            return 1;
        }
        curr = curr->next;
    }

    wantlist_node_t *node = calloc(1, sizeof(wantlist_node_t));
    strncpy(node->cid_str, cid_str, sizeof(node->cid_str) - 1);
    node->priority = priority;

    if (!q->tail) {
        q->head = q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->size++;

    pthread_mutex_unlock(&q->lock);
    return 1;
}

int wantlist_queue_pop(wantlist_queue_t *q, char *out_cid, int *out_priority) {
    if (!q) return 0;
    pthread_mutex_lock(&q->lock);

    if (!q->head) {
        pthread_mutex_unlock(&q->lock);
        return 0;
    }

    wantlist_node_t *node = q->head;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->size--;

    if (out_cid) strcpy(out_cid, node->cid_str);
    if (out_priority) *out_priority = node->priority;

    free(node);
    pthread_mutex_unlock(&q->lock);
    return 1;
}

void wantlist_queue_free(wantlist_queue_t *q) {
    if (!q) return;
    pthread_mutex_lock(&q->lock);
    wantlist_node_t *curr = q->head;
    while (curr) {
        wantlist_node_t *tmp = curr;
        curr = curr->next;
        free(tmp);
    }
    pthread_mutex_unlock(&q->lock);
    pthread_mutex_destroy(&q->lock);
    free(q);
}
