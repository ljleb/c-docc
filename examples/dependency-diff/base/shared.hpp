#define INITIAL 1

extern "C" int c_value(void);
namespace app {
int a(int n);
int b(int n);
static int helper() { return 4; }
struct Base { int n; };
struct Derived : Base {};
struct Reader { int read() const & { return 7; } };
}
