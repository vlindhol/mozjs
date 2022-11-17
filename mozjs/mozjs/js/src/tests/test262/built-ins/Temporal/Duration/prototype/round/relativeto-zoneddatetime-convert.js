// |reftest| skip -- Temporal is not supported
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.round
description: An exception from TimeZone#getOffsetNanosecondsFor() is propagated.
features: [Temporal]
---*/

class TZ extends Temporal.TimeZone {
  constructor() { super("UTC") }
  getOffsetNanosecondsFor() { throw new Test262Error() }
}

const tz = new TZ();
const arg = new Temporal.ZonedDateTime(0n, tz);
const instance = new Temporal.Duration(0, 0, 0, 365);

assert.throws(Test262Error, () => instance.round({ relativeTo: arg, largestUnit: "years" }));

reportCompare(0, 0);
