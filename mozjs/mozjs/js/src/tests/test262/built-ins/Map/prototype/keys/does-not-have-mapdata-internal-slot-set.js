// Copyright (C) 2015 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-map.prototype.keys
description: >
  Throws a TypeError if `this` is a Set object.
info: |
  Map.prototype.keys ()

  1. Let M be the this value.
  2. Return CreateMapIterator(M, "key").

  23.1.5.1 CreateMapIterator Abstract Operation

  ...
  2. If map does not have a [[MapData]] internal slot, throw a TypeError
  exception.
  ...
features: [Set]
---*/

assert.throws(TypeError, function() {
  Map.prototype.keys.call(new Set());
});

assert.throws(TypeError, function() {
  var m = new Map();
  m.keys.call(new Set());
});

reportCompare(0, 0);
