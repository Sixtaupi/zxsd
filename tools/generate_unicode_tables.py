#!/usr/bin/env python3
"""Generate the allocation-free XSD 1.0 category and block lookup tables.

Categories come from Unicode 4.0, which was current when XSD 1.0 Second Edition
became a Recommendation. Block names come from Unicode 16.0 as the specification
explicitly permits recognizing later blocks. The script never fetches inputs.
"""

import argparse
import pathlib

CATEGORIES = (
    "Lu",
    "Ll",
    "Lt",
    "Lm",
    "Lo",
    "Mn",
    "Mc",
    "Me",
    "Nd",
    "Nl",
    "No",
    "Pc",
    "Pd",
    "Ps",
    "Pe",
    "Pi",
    "Pf",
    "Po",
    "Zs",
    "Zl",
    "Zp",
    "Sm",
    "Sc",
    "Sk",
    "So",
    "Cc",
    "Cf",
    "Cs",
    "Co",
    "Cn",
)


def category_ranges(path):
    """Return category transitions from an official UnicodeData 4.0 file."""
    values = ["Cn"] * 0x110000
    pending = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        fields = raw_line.split(";")
        codepoint = int(fields[0], 16)
        name = fields[1]
        category = fields[2]
        if name.endswith(", First>"):
            pending = (codepoint, category)
            continue
        if name.endswith(", Last>"):
            if pending is None or pending[1] != category:
                raise ValueError("malformed UnicodeData range")
            values[pending[0] : codepoint + 1] = [category] * (codepoint + 1 - pending[0])
            pending = None
            continue
        values[codepoint] = category
    if pending is not None:
        raise ValueError("unterminated UnicodeData range")
    ranges = []
    previous = None
    for codepoint, category in enumerate(values):
        if category != previous:
            ranges.append((codepoint, CATEGORIES.index(category)))
            previous = category
    return ranges


def blocks(path):
    """Parse the official Unicode Blocks.txt file."""
    result = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        extent, name = (part.strip() for part in line.split(";", 1))
        first, last = (int(value, 16) for value in extent.split("..", 1))
        result.append((name.replace(" ", ""), first, last))
    result.extend(
        [
            ("CombiningMarksforSymbols", 0x20D0, 0x20FF),
            ("CyrillicSupplementary", 0x0500, 0x052F),
            ("Greek", 0x0370, 0x03FF),
        ]
    )
    return sorted(result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("unicode_data", type=pathlib.Path)
    parser.add_argument("blocks", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    lines = [
        "/* Generated from Unicode 4.0 categories and Unicode 16.0 blocks. */",
        '#include "mxsd_regex.h"',
        "",
        "#include <string.h>",
        "",
        "typedef struct { uint32_t start; unsigned char category; } unicode_range_t;",
        "typedef struct { const char *name; uint32_t first; uint32_t last; } unicode_block_t;",
        "",
        "static const unicode_range_t category_ranges[] = {",
    ]
    lines.extend("\t{0x%Xu, %du}," % item for item in category_ranges(args.unicode_data))
    lines.extend(["};", "", "static const unicode_block_t unicode_blocks[] = {"])
    lines.extend('\t{"%s", 0x%Xu, 0x%Xu},' % item for item in blocks(args.blocks))
    lines.extend(
        [
            "};",
            "",
            "static unsigned unicode_category(uint32_t codepoint)",
            "{",
            "\tsize_t low = 0u;",
            "\tsize_t high = sizeof(category_ranges) / sizeof(category_ranges[0]);",
            "\twhile (low + 1u < high) {",
            "\t\tsize_t middle = low + (high - low) / 2u;",
            "\t\tif (category_ranges[middle].start <= codepoint)",
            "\t\t\tlow = middle;",
            "\t\telse",
            "\t\t\thigh = middle;",
            "\t}",
            "\treturn category_ranges[low].category;",
            "}",
            "",
            "int mxsd_unicode_property_matches(uint32_t codepoint, const char *property, size_t length)",
            "{",
            "\tstatic const char categories[][3] = {",
            *['\t\t"%s",' % value for value in CATEGORIES],
            "\t};",
            "\tunsigned category;",
            "\tsize_t low, high;",
            "\tif (codepoint > 0x10FFFFu || property == NULL)",
            "\t\treturn 0;",
            "\tcategory = unicode_category(codepoint);",
            "\tif (length == 1u)",
            "\t\treturn categories[category][0] == property[0];",
            "\tif (length == 2u && property[0] != 'I')",
            "\t\treturn categories[category][0] == property[0] &&",
            "\t\t       categories[category][1] == property[1];",
            "\tif (length <= 2u || property[0] != 'I' || property[1] != 's')",
            "\t\treturn 0;",
            "\tproperty += 2;",
            "\tlength -= 2u;",
            '\tif (length == 10u && memcmp(property, "PrivateUse", 10u) == 0)',
            "\t\treturn (codepoint >= 0xE000u && codepoint <= 0xF8FFu) ||",
            "\t\t       (codepoint >= 0xF0000u && codepoint <= 0xFFFFDu) ||",
            "\t\t       (codepoint >= 0x100000u && codepoint <= 0x10FFFDu);",
            '\tif (length == 8u && memcmp(property, "Specials", 8u) == 0 &&',
            "\t    codepoint == 0xFEFFu)",
            "\t\treturn 1;",
            "\tlow = 0u;",
            "\thigh = sizeof(unicode_blocks) / sizeof(unicode_blocks[0]);",
            "\twhile (low < high) {",
            "\t\tsize_t middle = low + (high - low) / 2u;",
            "\t\tsize_t name_length = strlen(unicode_blocks[middle].name);",
            "\t\tsize_t common = name_length < length ? name_length : length;",
            "\t\tint comparison = memcmp(unicode_blocks[middle].name, property, common);",
            "\t\tif (comparison == 0 && name_length != length)",
            "\t\t\tcomparison = name_length < length ? -1 : 1;",
            "\t\tif (comparison < 0)",
            "\t\t\tlow = middle + 1u;",
            "\t\telse if (comparison > 0)",
            "\t\t\thigh = middle;",
            "\t\telse",
            "\t\t\treturn codepoint >= unicode_blocks[middle].first &&",
            "\t\t\t       codepoint <= unicode_blocks[middle].last;",
            "\t}",
            "\treturn 0;",
            "}",
            "",
        ]
    )
    args.output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
