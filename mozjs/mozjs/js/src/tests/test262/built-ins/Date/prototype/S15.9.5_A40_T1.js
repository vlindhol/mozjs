// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: The Date.prototype has the property "setFullYear"
esid: sec-properties-of-the-date-prototype-object
description: The Date.prototype has the property "setFullYear"
---*/
assert.sameValue(
  Date.prototype.hasOwnProperty("setFullYear"),
  true,
  'Date.prototype.hasOwnProperty("setFullYear") must return true'
);

reportCompare(0, 0);
