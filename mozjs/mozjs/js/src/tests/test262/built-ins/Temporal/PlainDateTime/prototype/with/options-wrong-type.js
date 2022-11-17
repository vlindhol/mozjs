// |reftest| skip -- Temporal is not supported
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.prototype.with
description: TypeError thrown when options argument is a primitive
features: [BigInt, Symbol, Temporal]
---*/

const badOptions = [
  null,
  true,
  "some string",
  Symbol(),
  1,
  2n,
];

const instance = new Temporal.PlainDateTime(2000, 5, 2);
for (const value of badOptions) {
  assert.throws(TypeError, () => instance.with({ day: 5 }, value),
    `TypeError on wrong options type ${typeof value}`);
};

reportCompare(0, 0);
