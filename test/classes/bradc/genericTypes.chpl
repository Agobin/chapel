class C {
  param rank : int;
  type type1;
  type type2 = rank*type1;


  def foo() {
    var x: type1;
    var x2: type2;

    writeln("x is: ", x);
    writeln("x2 is: ", x2);
  }
}

var c = C(2,int);
c.foo();

var c2 = C(3,real);
c2.foo();
