use Random;

config const n = 100, trials = 100, useRndSeed = true;

proc getNextSeed(oldSeed) {
  var newSeed = SeedGenerator.currentTime;
  while newSeed == oldSeed {
    newSeed = SeedGenerator.currentTime;
  }
  return newSeed;
}

proc main {
  var A: [1..n] real;
  var initSeed = if useRndSeed then SeedGenerator.currentTime else 31415;
  var diffCnt: int;

  fillRandom(A, initSeed);
  for i in 1..trials {
    if !useRndSeed then initSeed += 2;
    var seed = if useRndSeed then getNextSeed(initSeed) else initSeed;
    var B: [1..n] real;
    fillRandom(B, seed);

    if !(&& reduce (A == B)) then
      diffCnt += 1;
    A = B;
    initSeed = seed;
  }
  if diffCnt < trials*0.9 then
    writeln("That wasn't very random looking.");
}

