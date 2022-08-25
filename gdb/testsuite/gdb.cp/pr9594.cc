
class Base
{
public:
  virtual int get_foo () { return 1; }
  int base_function_only () { return 2; }
};

class Foo : public Base
{

private:
  int foo_value;

public:
  Foo () { foo_value = 0;}
  Foo (int i) { foo_value = i;}
  ~Foo () { }
  void set_foo (int value);
  int get_foo ();

  // Something similar to a constructor name.
  void Foofoo ();

  bool operator== (const Foo &other) { return foo_value == other.foo_value; }
};
 
void Foo::set_foo (int value)
{
  foo_value = value;
}

int Foo::get_foo ()
{
  return foo_value;
}

void Foo::Foofoo ()
{
}

namespace Test_NS {

int foo;
int bar;

namespace Nested {

int qux;

} /* namespace Nested */

} /* namespace Test_NS */

int main ()
{
  // Anonymous struct with method.
  struct {
    private:
      int unique_name_foo = 5;
    public:
      int get() { return unique_name_foo; } /* BP2.  */
  } a;
  Foo foo1;
  foo1.set_foo (42);		/* BP1.  */
  a.get ();
  return 0;
}
