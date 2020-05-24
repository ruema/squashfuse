typedef struct hashset_node {
  struct hashset_node *next;
  unsigned hash;
  /* char key[]; */
} hashset_node;

typedef struct {
  hashset_node **buckets;
  unsigned nbuckets, nnodes;
} hashset;

void hashset_init(hashset *m);
int hashset_contains(hashset *m, const char *key);
void hashset_free(hashset *m);
int hashset_add(hashset *m, const char *key);
