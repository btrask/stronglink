

typedef struct omap_t omap_t;

int omap_create(omap_t **const out);
int omap_count(omap_t *const omap);
int omap_index(omap_t *const omap, unsigned const index, DB_val *const key, DB_val *const data);
int omap_key(omap_t *const omap, DB_val const *const key, DB_val *const data);

