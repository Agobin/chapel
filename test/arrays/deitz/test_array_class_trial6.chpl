class array1d {
  type t;
  var x1 : t;
  var x2 : t;
  var x3 : t;
  function indexedby(i : integer) var : t {
    write("[Access of ", i, "]");
    select i {
      when 1 do return x1;
      when 2 do return x2;
      when 3 do return x3;
      otherwise writeln("[Out of bounds]");
    }
    return x1;
  }
}

var a : array1d = array1d(integer);

a.indexedby(1) = 3;
a.indexedby(2) = 2;
a.indexedby(3) = 1;
writeln(a.indexedby(1), a.indexedby(2), a.indexedby(3));
