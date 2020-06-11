typedef struct hashset_node {
  struct hashset_node *next;
  unsigned hash;
  int level;
  /* char key[]; */
} hashset_node;

typedef struct {
  hashset_node **buckets;
  unsigned nbuckets, nnodes;
} hashset;

void hashset_init(hashset *m);
int hashset_getlevel(hashset *m, const char *key);
void hashset_free(hashset *m);
int hashset_add(hashset *m, const char *key, int level);
