// |reftest| skip -- regexp-unicode-property-escapes is not supported
// Copyright 2019 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script=Old_Permic`
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
    [0x010350, 0x01037A]
  ]
});
testPropertyEscapes(
  /^\p{Script=Old_Permic}+$/u,
  matchSymbols,
  "\\p{Script=Old_Permic}"
);
testPropertyEscapes(
  /^\p{Script=Perm}+$/u,
  matchSymbols,
  "\\p{Script=Perm}"
);
testPropertyEscapes(
  /^\p{sc=Old_Permic}+$/u,
  matchSymbols,
  "\\p{sc=Old_Permic}"
);
testPropertyEscapes(
  /^\p{sc=Perm}+$/u,
  matchSymbols,
  "\\p{sc=Perm}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00DBFF],
    [0x00E000, 0x01034F],
    [0x01037B, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script=Old_Permic}+$/u,
  nonMatchSymbols,
  "\\P{Script=Old_Permic}"
);
testPropertyEscapes(
  /^\P{Script=Perm}+$/u,
  nonMatchSymbols,
  "\\P{Script=Perm}"
);
testPropertyEscapes(
  /^\P{sc=Old_Permic}+$/u,
  nonMatchSymbols,
  "\\P{sc=Old_Permic}"
);
testPropertyEscapes(
  /^\P{sc=Perm}+$/u,
  nonMatchSymbols,
  "\\P{sc=Perm}"
);

reportCompare(0, 0);
