var i: int = 2;

proc foo(s: sync int) {
  s = i;
  i += 1;
}

var s: sync int;
foo(s);
writeln(s);
foo(s);
writeln(s);
