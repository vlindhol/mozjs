// |reftest| skip -- Temporal is not supported
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.zoneddatetime.prototype.withplaindate
description: Getters are not called when converting a ZonedDateTime to a PlainDate.
includes: [compareArray.js]
features: [Temporal]
---*/

const actual = [];
const prototypeDescrs = Object.getOwnPropertyDescriptors(Temporal.ZonedDateTime.prototype);
const getters = ["year", "month", "monthCode", "day", "hour", "minute", "second", "millisecond", "microsecond", "nanosecond", "calendar"];

for (const property of getters) {
  Object.defineProperty(Temporal.ZonedDateTime.prototype, property, {
    get() {
      actual.push(`get ${property}`);
      const value = prototypeDescrs[property].get.call(this);
      return {
        toString() {
          actual.push(`toString ${property}`);
          return value.toString();
        },
        valueOf() {
          actual.push(`valueOf ${property}`);
          return value;
        },
      };
    },
  });
}

const arg = new Temporal.ZonedDateTime(0n, "UTC");
const instance = new Temporal.ZonedDateTime(1_000_000_000_000_000_000n, "UTC");
instance.withPlainDate(arg);
assert.compareArray(actual, []);

reportCompare(0, 0);
