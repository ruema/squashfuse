#include <stdlib.h>
#include <string.h>
#include "hashset.h"

void hashset_init(hashset *m) {
	m->buckets = NULL;
	m->nbuckets = m->nnodes = 0;
}

static unsigned str_hash(const char *str) {
  unsigned hash = 5381;
  while (*str) {
    hash = ((hash << 5) + hash) ^ *str++;
  }
  return hash;
}

static hashset_node *hashset_newnode(const char *key) {
  hashset_node *node;
  int ksize = strlen(key) + 1;
  node = malloc(sizeof(*node) + ksize);
  if (!node) return NULL;
  memcpy(node + 1, key, ksize);
  node->hash = str_hash(key);
  return node;
}

static int hashset_bucketidx(hashset *m, unsigned hash) {
  /* If the implementation is changed to allow a non-power-of-2 bucket count,
   * the line below should be changed to use mod instead of AND */
  return hash & (m->nbuckets - 1);
}

static int hashset_resize(hashset *m, int nbuckets) {
  hashset_node *nodes, *node, *next;
  hashset_node **buckets;
  int i; 
  /* Chain all nodes together */
  nodes = NULL;
  i = m->nbuckets;
  while (i--) {
    node = (m->buckets)[i];
    while (node) {
      next = node->next;
      node->next = nodes;
      nodes = node;
      node = next;
    }
  }
  /* Reset buckets */
  buckets = realloc(m->buckets, sizeof(*m->buckets) * nbuckets);
  if (buckets == NULL) return -1;
  m->buckets = buckets;
  m->nbuckets = nbuckets;
  memset(m->buckets, 0, sizeof(*m->buckets) * m->nbuckets);
  /* Re-add nodes to buckets */
  node = nodes;
  while (node) {
    next = node->next;
    int n = hashset_bucketidx(m, node->hash);
	node->next = m->buckets[n];
	m->buckets[n] = node;
    node = next;
  }
  return 0;
}

int hashset_contains(hashset *m, const char *key) {
  unsigned hash = str_hash(key);
  hashset_node *node;
  if (m->nbuckets > 0) {
    node = m->buckets[hashset_bucketidx(m, hash)];
    while (node) {
      if (node->hash == hash && !strcmp((char*) (node + 1), key)) {
        return 1;
      }
      node = node->next;
    }
  }
  return 0;
}

void hashset_free(hashset *m) {
  hashset_node *next, *node;
  int i;
  i = m->nbuckets;
  while (i--) {
    node = m->buckets[i];
    while (node) {
      next = node->next;
      free(node);
      node = next;
    }
  }
  free(m->buckets);
}

int hashset_add(hashset *m, const char *key) {
  int n, err;
  hashset_node *node;
  if(hashset_contains(m, key)) {
  	return 1;
  }
  /* Add new node */
  node = hashset_newnode(key);
  if (node == NULL) return -1;
  if (m->nnodes >= m->nbuckets) {
    n = (m->nbuckets > 0) ? (m->nbuckets << 1) : 1;
    err = hashset_resize(m, n);
    if (err) {
	  free(node);
	  return -1;
	}
  }
  n = hashset_bucketidx(m, node->hash);
  node->next = m->buckets[n];
  m->buckets[n] = node;
  m->nnodes++;
  return 0;
}

