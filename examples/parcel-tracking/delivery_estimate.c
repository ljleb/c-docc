int depot_sorting_days();

int delivery_estimate_days()
{
    return depot_sorting_days() + 1;
}
