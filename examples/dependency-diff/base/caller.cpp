#include "shared.hpp"
#include <vector>
using Count = int;
int inspect(Count n) { return app::a(n) + c_value(); }
int inspect(double) { return sizeof(std::vector<int>); }
int use(const app::Derived &value) { return value.n; }
