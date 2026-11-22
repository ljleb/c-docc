#include "shared.hpp"
int app::a(int n) { return n ? b(n - 1) : INITIAL; }
