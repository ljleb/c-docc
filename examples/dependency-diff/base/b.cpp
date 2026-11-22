#include "shared.hpp"
int app::b(int n) { return n ? a(n - 1) : helper(); }
