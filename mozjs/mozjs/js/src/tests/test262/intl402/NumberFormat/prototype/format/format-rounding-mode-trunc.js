// |reftest| skip-if(release_or_beta) -- Intl.NumberFormat-v3 is not released yet
// Copyright 2021 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-intl.numberformat.prototype.format
description: >
  When roundingMode is "trunc", rounding tends toward zero
features: [Intl.NumberFormat-v3]
includes: [testIntl.js]
---*/

var locales = [
  new Intl.NumberFormat().resolvedOptions().locale, 'ar', 'de', 'th', 'ja'
];
var numberingSystems = ['arab', 'latn', 'thai', 'hanidec'];

testNumberFormat(
  locales,
  numberingSystems,
  {useGrouping: false, roundingMode: 'trunc', maximumSignificantDigits: 2},
  {
    '1.101': '1.1',
    '1.15': '1.1',
    '1.1999': '1.1',
    '1.25': '1.2',
    '0': '0',
    '-0': '-0',
    '-1.101': '-1.1',
    '-1.15': '-1.1',
    '-1.1999': '-1.1',
    '-1.25': '-1.2'
  }
);

reportCompare(0, 0);
