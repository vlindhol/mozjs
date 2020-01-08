// Add |Intl.ListFormat| to the Intl object if not already present.
function addIntlListFormat(global) {
    if (!global.Intl.ListFormat && typeof global.addIntlExtras === "function") {
        let obj = {};
        global.addIntlExtras(obj);

        Object.defineProperty(global.Intl, "ListFormat", {
            value: obj.ListFormat,
            writable: true, enumerable: false, configurable: true
        });
    }
}

addIntlListFormat(this);

function GenericPartCreator(type) {
    return str => ({ type, value: str });
}

const ListFormatParts = {
    Element: GenericPartCreator("element"),
    Literal: GenericPartCreator("literal"),
};

function assertParts(lf, x, expected) {
    var parts = lf.formatToParts(x);
    assertEq(parts.map(part => part.value).join(""), lf.format(x),
             "formatToParts and format must agree");

    var len = parts.length;
    assertEq(len, expected.length, "parts count mismatch");
    for (var i = 0; i < len; i++) {
        assertEq(parts[i].type, expected[i].type, "type mismatch at " + i);
        assertEq(parts[i].value, expected[i].value, "value mismatch at " + i);
    }
}
