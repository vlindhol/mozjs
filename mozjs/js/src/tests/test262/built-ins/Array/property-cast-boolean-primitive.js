// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    A property name P (in the form of a string value) is an array index
    if and only if ToString(ToUint32(P)) is equal to P and ToUint32(P) is not equal to 2^32 - 1
es5id: 15.4_A1.1_T1
description: Checking for boolean primitive
---*/

var x = [];

x[true] = 1;
assert.sameValue(x[1], undefined, "x[1]");
assert.sameValue(x["true"], 1, "x['true']");

x[false] = 0;
assert.sameValue(x[0], undefined, "x[0]");
assert.sameValue(x["false"], 0, "x['false']")

reportCompare(0, 0);