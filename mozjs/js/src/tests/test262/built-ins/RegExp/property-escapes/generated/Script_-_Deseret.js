// |reftest| skip -- regexp-unicode-property-escapes is not supported
// Copyright 2019 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script=Deseret`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v12.1.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x010400, 0x01044F]
  ]
});
testPropertyEscapes(
  /^\p{Script=Deseret}+$/u,
  matchSymbols,
  "\\p{Script=Deseret}"
);
testPropertyEscapes(
  /^\p{Script=Dsrt}+$/u,
  matchSymbols,
  "\\p{Script=Dsrt}"
);
testPropertyEscapes(
  /^\p{sc=Deseret}+$/u,
  matchSymbols,
  "\\p{sc=Deseret}"
);
testPropertyEscapes(
  /^\p{sc=Dsrt}+$/u,
  matchSymbols,
  "\\p{sc=Dsrt}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00DBFF],
    [0x00E000, 0x0103FF],
    [0x010450, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script=Deseret}+$/u,
  nonMatchSymbols,
  "\\P{Script=Deseret}"
);
testPropertyEscapes(
  /^\P{Script=Dsrt}+$/u,
  nonMatchSymbols,
  "\\P{Script=Dsrt}"
);
testPropertyEscapes(
  /^\P{sc=Deseret}+$/u,
  nonMatchSymbols,
  "\\P{sc=Deseret}"
);
testPropertyEscapes(
  /^\P{sc=Dsrt}+$/u,
  nonMatchSymbols,
  "\\P{sc=Dsrt}"
);

reportCompare(0, 0);
