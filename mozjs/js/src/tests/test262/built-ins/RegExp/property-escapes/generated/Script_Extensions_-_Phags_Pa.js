// |reftest| skip -- regexp-unicode-property-escapes is not supported
// Copyright 2019 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `Script_Extensions=Phags_Pa`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v12.1.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x001805
  ],
  ranges: [
    [0x001802, 0x001803],
    [0x00A840, 0x00A877]
  ]
});
testPropertyEscapes(
  /^\p{Script_Extensions=Phags_Pa}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Phags_Pa}"
);
testPropertyEscapes(
  /^\p{Script_Extensions=Phag}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Phag}"
);
testPropertyEscapes(
  /^\p{scx=Phags_Pa}+$/u,
  matchSymbols,
  "\\p{scx=Phags_Pa}"
);
testPropertyEscapes(
  /^\p{scx=Phag}+$/u,
  matchSymbols,
  "\\p{scx=Phag}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x001804
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x001801],
    [0x001806, 0x00A83F],
    [0x00A878, 0x00DBFF],
    [0x00E000, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script_Extensions=Phags_Pa}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Phags_Pa}"
);
testPropertyEscapes(
  /^\P{Script_Extensions=Phag}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Phag}"
);
testPropertyEscapes(
  /^\P{scx=Phags_Pa}+$/u,
  nonMatchSymbols,
  "\\P{scx=Phags_Pa}"
);
testPropertyEscapes(
  /^\P{scx=Phag}+$/u,
  nonMatchSymbols,
  "\\P{scx=Phag}"
);

reportCompare(0, 0);
