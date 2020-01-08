// |reftest| skip -- regexp-unicode-property-escapes is not supported
// Copyright 2019 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script_Extensions=Modi`
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
    [0x00A830, 0x00A839],
    [0x011600, 0x011644],
    [0x011650, 0x011659]
  ]
});
testPropertyEscapes(
  /^\p{Script_Extensions=Modi}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Modi}"
);
testPropertyEscapes(
  /^\p{Script_Extensions=Modi}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Modi}"
);
testPropertyEscapes(
  /^\p{scx=Modi}+$/u,
  matchSymbols,
  "\\p{scx=Modi}"
);
testPropertyEscapes(
  /^\p{scx=Modi}+$/u,
  matchSymbols,
  "\\p{scx=Modi}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00A82F],
    [0x00A83A, 0x00DBFF],
    [0x00E000, 0x0115FF],
    [0x011645, 0x01164F],
    [0x01165A, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script_Extensions=Modi}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Modi}"
);
testPropertyEscapes(
  /^\P{Script_Extensions=Modi}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Modi}"
);
testPropertyEscapes(
  /^\P{scx=Modi}+$/u,
  nonMatchSymbols,
  "\\P{scx=Modi}"
);
testPropertyEscapes(
  /^\P{scx=Modi}+$/u,
  nonMatchSymbols,
  "\\P{scx=Modi}"
);

reportCompare(0, 0);
