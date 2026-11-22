#include <stdint.h>
struct State { int32_t count; };
static int helper(void) { return 3; }
int c_value(void) { struct State s = {helper()}; return s.count; }
