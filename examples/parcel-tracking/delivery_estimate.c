int depot_sorting_days(void);
int delivery_estimate_days(void) { return depot_sorting_days() + 1; }
