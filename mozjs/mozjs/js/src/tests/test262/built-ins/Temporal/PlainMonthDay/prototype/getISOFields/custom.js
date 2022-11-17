// |reftest| skip -- Temporal is not supported
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainmonthday.prototype.getisofields
description: getISOFields does not call into user code.
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const calendar = TemporalHelpers.calendarThrowEverything();
const instance = new Temporal.PlainMonthDay(5, 2, calendar);
const result = instance.getISOFields();

assert.sameValue(result.isoYear, 1972, "isoYear result");
assert.sameValue(result.isoMonth, 5, "isoMonth result");
assert.sameValue(result.isoDay, 2, "isoDay result");
assert.sameValue(result.calendar, calendar, "calendar result");

reportCompare(0, 0);
