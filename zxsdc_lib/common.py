# Copyright (c) 2026 zxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Shared compiler constants, diagnostics, identifiers, and XSD value helpers."""

import base64
import binascii
import re
from decimal import Decimal, InvalidOperation

VERSION = "0.20.0"
ABI_VERSIONS = {"runtime": 1, "descriptor": 1, "binding": 1}

XSD_URI = "http://www.w3.org/2001/XMLSchema"
XSI_URI = "http://www.w3.org/2001/XMLSchema-instance"
XS = "{" + XSD_URI + "}"
PRIMITIVES = {
    "string": "ZXSD_T_STRING",
    "boolean": "ZXSD_T_BOOLEAN",
    "int": "ZXSD_T_INT",
    "unsignedInt": "ZXSD_T_UNSIGNED_INT",
    "integer": "ZXSD_T_INTEGER",
    "decimal": "ZXSD_T_DECIMAL",
    "float": "ZXSD_T_FLOAT",
    "double": "ZXSD_T_DOUBLE",
    "long": "ZXSD_T_LONG",
    "short": "ZXSD_T_SHORT",
    "byte": "ZXSD_T_BYTE",
    "nonPositiveInteger": "ZXSD_T_NON_POSITIVE_INTEGER",
    "negativeInteger": "ZXSD_T_NEGATIVE_INTEGER",
    "nonNegativeInteger": "ZXSD_T_NON_NEGATIVE_INTEGER",
    "positiveInteger": "ZXSD_T_POSITIVE_INTEGER",
    "unsignedLong": "ZXSD_T_UNSIGNED_LONG",
    "unsignedShort": "ZXSD_T_UNSIGNED_SHORT",
    "unsignedByte": "ZXSD_T_UNSIGNED_BYTE",
    "normalizedString": "ZXSD_T_NORMALIZED_STRING",
    "token": "ZXSD_T_TOKEN",
    "language": "ZXSD_T_LANGUAGE",
    "Name": "ZXSD_T_NAME",
    "NCName": "ZXSD_T_NCNAME",
    "NMTOKEN": "ZXSD_T_NMTOKEN",
    "NMTOKENS": "ZXSD_T_NMTOKEN",
    "ID": "ZXSD_T_ID",
    "IDREF": "ZXSD_T_IDREF",
    "IDREFS": "ZXSD_T_IDREF",
    "ENTITY": "ZXSD_T_ENTITY",
    "ENTITIES": "ZXSD_T_ENTITY",
    "QName": "ZXSD_T_QNAME",
    "anyURI": "ZXSD_T_ANY_URI",
    "duration": "ZXSD_T_DURATION",
    "date": "ZXSD_T_DATE",
    "dateTime": "ZXSD_T_DATE_TIME",
    "time": "ZXSD_T_TIME",
    "gYear": "ZXSD_T_GYEAR",
    "gYearMonth": "ZXSD_T_GYEAR_MONTH",
    "gMonth": "ZXSD_T_GMONTH",
    "gMonthDay": "ZXSD_T_GMONTH_DAY",
    "gDay": "ZXSD_T_GDAY",
    "hexBinary": "ZXSD_T_HEX_BINARY",
    "base64Binary": "ZXSD_T_BASE64_BINARY",
    "NOTATION": "ZXSD_T_NOTATION",
    "anySimpleType": "ZXSD_T_STRING",
}
LIST_BUILTINS = {"NMTOKENS", "IDREFS", "ENTITIES"}
WHITESPACE_PRESERVE = {"string", "anySimpleType"}
WHITESPACE_REPLACE = {"normalizedString"}
MAX_XSD = 8 * 1024 * 1024
MAX_SCHEMA_COMPLEXITY = 4096
XML_URI = "http://www.w3.org/XML/1998/namespace"


class SchemaError(Exception):
    """Raised when a schema cannot be represented safely by zxsd."""


class InvalidSchemaError(SchemaError):
    """Raised when an XSD violates a schema-for-schemas constraint."""


def lname(tag):
    """Return the local name of an XML Schema element tag."""
    if not isinstance(tag, str) or not tag.startswith(XS):
        raise SchemaError("unsupported foreign XSD element %r" % tag)
    return tag[len(XS) :]


def check(node, allowed):
    """Reject attributes that zxsdc does not explicitly implement."""
    bad = sorted(
        attribute
        for attribute in set(node.attrib) - set(allowed)
        if not (attribute.startswith("{") and not attribute.startswith(XS))
    )
    if bad:
        raise SchemaError("unsupported property %r on xs:%s" % (bad[0], lname(node.tag)))


def ident(s):
    """Convert an XSD name or filename to a valid C identifier."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", s)
    return ("_" + s) if s[:1].isdigit() else s


def cstr(s):
    """Quote text as a deterministic C string literal."""
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def valid_ncname(value):
    """Return whether a string follows the XML 1.0 Fifth Edition NCName production."""

    def start(character):
        codepoint = ord(character)
        return (
            character == "_"
            or "A" <= character <= "Z"
            or "a" <= character <= "z"
            or 0xC0 <= codepoint <= 0xD6
            or 0xD8 <= codepoint <= 0xF6
            or 0xF8 <= codepoint <= 0x2FF
            or 0x370 <= codepoint <= 0x37D
            or 0x37F <= codepoint <= 0x1FFF
            or 0x200C <= codepoint <= 0x200D
            or 0x2070 <= codepoint <= 0x218F
            or 0x2C00 <= codepoint <= 0x2FEF
            or 0x3001 <= codepoint <= 0xD7FF
            or 0xF900 <= codepoint <= 0xFDCF
            or 0xFDF0 <= codepoint <= 0xFFFD
            or 0x10000 <= codepoint <= 0xEFFFF
        )

    def continuation(character):
        codepoint = ord(character)
        return (
            start(character)
            or character in "-."
            or "0" <= character <= "9"
            or codepoint == 0xB7
            or 0x300 <= codepoint <= 0x36F
            or 0x203F <= codepoint <= 0x2040
        )

    return bool(value) and start(value[0]) and all(continuation(x) for x in value[1:])


def valid_qname(value):
    """Return whether a lexical value is an XML QName."""
    parts = value.split(":")
    return len(parts) in (1, 2) and all(valid_ncname(part) for part in parts)


INTEGER_RANGES = {
    "ZXSD_T_LONG": (-(2**63), 2**63 - 1),
    "ZXSD_T_INT": (-(2**31), 2**31 - 1),
    "ZXSD_T_SHORT": (-(2**15), 2**15 - 1),
    "ZXSD_T_BYTE": (-(2**7), 2**7 - 1),
    "ZXSD_T_UNSIGNED_LONG": (0, 2**64 - 1),
    "ZXSD_T_UNSIGNED_INT": (0, 2**32 - 1),
    "ZXSD_T_UNSIGNED_SHORT": (0, 2**16 - 1),
    "ZXSD_T_UNSIGNED_BYTE": (0, 2**8 - 1),
}


def valid_timezone(value):
    """Return whether an optional XSD timezone suffix is well formed."""
    if not value:
        return True
    if value == "Z":
        return True
    match = re.fullmatch(r"[+-]([0-9]{2}):([0-9]{2})", value)
    return bool(match) and (
        int(match.group(1)) < 14 or (int(match.group(1)) == 14 and int(match.group(2)) == 0)
    )


def valid_time(value):
    """Validate the lexical space of xs:time."""
    match = re.fullmatch(
        r"([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.([0-9]+))?(Z|[+-][0-9]{2}:[0-9]{2})?",
        value,
    )
    if not match or not valid_timezone(match.group(5) or ""):
        return False
    hour, minute, second = (int(match.group(index)) for index in range(1, 4))
    return (
        minute <= 59
        and second <= 59
        and hour <= 24
        and (hour != 24 or (minute == 0 and second == 0 and not match.group(4)))
    )


def valid_date(value):
    """Validate the lexical space of xs:date."""
    match = re.fullmatch(
        r"(-?)([0-9]{4,})-([0-9]{2})-([0-9]{2})(Z|[+-][0-9]{2}:[0-9]{2})?",
        value,
    )
    if not match or int(match.group(2)) == 0 or not valid_timezone(match.group(5) or ""):
        return False
    year = -int(match.group(2)) if match.group(1) else int(match.group(2))
    month = int(match.group(3))
    day = int(match.group(4))
    if not 1 <= month <= 12:
        return False
    days = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
    leap = year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)
    return 1 <= day <= days[month - 1] + (1 if month == 2 and leap else 0)


def valid_duration(value):
    """Validate the lexical space of xs:duration."""
    match = re.fullmatch(
        r"-?P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)D)?"
        r"(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)(?:\.(\d+))?S)?)?",
        value,
    )
    return (
        bool(match)
        and any(group is not None for group in match.groups())
        and not value.endswith("T")
    )


def primitive_lexical_form(base, value):
    """Validate a built-in simple type lexical form while compiling a schema."""
    if base in (
        "ZXSD_T_STRING",
        "ZXSD_T_NORMALIZED_STRING",
        "ZXSD_T_TOKEN",
        "ZXSD_T_ANY_URI",
    ):
        return True
    if base == "ZXSD_T_BOOLEAN":
        return value in ("true", "false", "0", "1")
    integer_types = {
        "ZXSD_T_INTEGER",
        "ZXSD_T_LONG",
        "ZXSD_T_INT",
        "ZXSD_T_SHORT",
        "ZXSD_T_BYTE",
        "ZXSD_T_NON_POSITIVE_INTEGER",
        "ZXSD_T_NEGATIVE_INTEGER",
        "ZXSD_T_NON_NEGATIVE_INTEGER",
        "ZXSD_T_POSITIVE_INTEGER",
        "ZXSD_T_UNSIGNED_LONG",
        "ZXSD_T_UNSIGNED_INT",
        "ZXSD_T_UNSIGNED_SHORT",
        "ZXSD_T_UNSIGNED_BYTE",
    }
    if base in integer_types:
        if re.fullmatch(r"[+-]?[0-9]+", value) is None:
            return False
        number = int(value)
        if base in INTEGER_RANGES:
            return INTEGER_RANGES[base][0] <= number <= INTEGER_RANGES[base][1]
        if base == "ZXSD_T_NON_POSITIVE_INTEGER":
            return number <= 0
        if base == "ZXSD_T_NEGATIVE_INTEGER":
            return number < 0
        if base == "ZXSD_T_NON_NEGATIVE_INTEGER":
            return number >= 0
        if base == "ZXSD_T_POSITIVE_INTEGER":
            return number > 0
        return True
    if base == "ZXSD_T_DECIMAL":
        return re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)", value) is not None
    if base in ("ZXSD_T_FLOAT", "ZXSD_T_DOUBLE"):
        return (
            value in ("INF", "-INF", "NaN")
            or re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[Ee][+-]?[0-9]+)?", value)
            is not None
        )
    if base == "ZXSD_T_DURATION":
        return valid_duration(value)
    if base == "ZXSD_T_DATE":
        return valid_date(value)
    if base == "ZXSD_T_DATE_TIME":
        date, separator, time = value.partition("T")
        return bool(separator) and valid_date(date) and valid_time(time)
    if base == "ZXSD_T_TIME":
        return valid_time(value)
    calendar_patterns = {
        "ZXSD_T_GYEAR": r"-?[0-9]{4,}(?:Z|[+-][0-9]{2}:[0-9]{2})?",
        "ZXSD_T_GYEAR_MONTH": r"-?[0-9]{4,}-(?:0[1-9]|1[0-2])(?:Z|[+-][0-9]{2}:[0-9]{2})?",
        "ZXSD_T_GMONTH": r"--(?:0[1-9]|1[0-2])(?:--)?(?:Z|[+-][0-9]{2}:[0-9]{2})?",
        "ZXSD_T_GMONTH_DAY": r"--(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01])(?:Z|[+-][0-9]{2}:[0-9]{2})?",
        "ZXSD_T_GDAY": r"---(?:0[1-9]|[12][0-9]|3[01])(?:Z|[+-][0-9]{2}:[0-9]{2})?",
    }
    if base in calendar_patterns:
        match = re.fullmatch(calendar_patterns[base], value)
        suffix = re.search(r"(Z|[+-][0-9]{2}:[0-9]{2})$", value)
        if not match or not valid_timezone(suffix.group(1) if suffix else ""):
            return False
        if base in ("ZXSD_T_GYEAR", "ZXSD_T_GYEAR_MONTH"):
            year = re.match(r"-?([0-9]{4,})", value)
            if year and int(year.group(1)) == 0:
                return False
        if base == "ZXSD_T_GMONTH_DAY":
            month_day = re.match(r"--([0-9]{2})-([0-9]{2})", value)
            month_days = (31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
            if month_day and int(month_day.group(2)) > month_days[int(month_day.group(1)) - 1]:
                return False
        return True
    if base == "ZXSD_T_HEX_BINARY":
        return len(value) % 2 == 0 and re.fullmatch(r"[0-9A-Fa-f]*", value) is not None
    if base == "ZXSD_T_BASE64_BINARY":
        try:
            base64.b64decode("".join(value.split()), validate=True)
            return True
        except (binascii.Error, ValueError):
            return False
    if base == "ZXSD_T_LANGUAGE":
        return re.fullmatch(r"[A-Za-z]{1,8}(?:-[A-Za-z0-9]{1,8})*", value) is not None
    if base in ("ZXSD_T_NCNAME", "ZXSD_T_ID", "ZXSD_T_IDREF", "ZXSD_T_ENTITY"):
        return valid_ncname(value)
    if base == "ZXSD_T_NAME":
        return re.fullmatch(r"[:A-Za-z_][:A-Za-z0-9_.-]*", value) is not None
    if base == "ZXSD_T_NMTOKEN":
        return re.fullmatch(r"[:A-Za-z0-9_.-]+", value) is not None
    if base in ("ZXSD_T_QNAME", "ZXSD_T_NOTATION"):
        return valid_qname(value)
    return False


def timezone_minutes(value):
    """Return an XSD timezone displacement in minutes, or zero when absent/Z."""
    match = re.search(r"([+-])([0-9]{2}):([0-9]{2})$", value)
    if not match:
        return 0
    minutes = int(match.group(2)) * 60 + int(match.group(3))
    return minutes if match.group(1) == "+" else -minutes


def ordered_value(base, value):
    """Map commonly ordered XSD values to keys usable for facet comparisons."""
    if base in {
        "ZXSD_T_INTEGER",
        "ZXSD_T_LONG",
        "ZXSD_T_INT",
        "ZXSD_T_SHORT",
        "ZXSD_T_BYTE",
        "ZXSD_T_NON_POSITIVE_INTEGER",
        "ZXSD_T_NEGATIVE_INTEGER",
        "ZXSD_T_NON_NEGATIVE_INTEGER",
        "ZXSD_T_POSITIVE_INTEGER",
        "ZXSD_T_UNSIGNED_LONG",
        "ZXSD_T_UNSIGNED_INT",
        "ZXSD_T_UNSIGNED_SHORT",
        "ZXSD_T_UNSIGNED_BYTE",
        "ZXSD_T_DECIMAL",
        "ZXSD_T_FLOAT",
        "ZXSD_T_DOUBLE",
    }:
        if value == "INF":
            return (1, Decimal(0))
        if value == "-INF":
            return (-1, Decimal(0))
        if value == "NaN":
            return None
        return (0, Decimal(value))
    if base == "ZXSD_T_DURATION":
        match = re.fullmatch(
            r"(-?)P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)D)?"
            r"(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)(?:\.(\d+))?S)?)?",
            value,
        )
        if not match:
            return None
        sign = -1 if match.group(1) else 1
        months = int(match.group(2) or 0) * 12 + int(match.group(3) or 0)
        seconds = (
            int(match.group(4) or 0) * 86400
            + int(match.group(5) or 0) * 3600
            + int(match.group(6) or 0) * 60
            + Decimal((match.group(7) or "0") + "." + (match.group(8) or "0"))
        )
        return (sign * months, sign * seconds)
    if base == "ZXSD_T_TIME":
        match = re.match(r"(\d\d):(\d\d):(\d\d)(?:\.([0-9]+))?", value)
        if not match:
            return None
        seconds = (
            int(match.group(1)) * 3600
            + int(match.group(2)) * 60
            + int(match.group(3))
            + Decimal("0." + (match.group(4) or "0"))
        )
        return seconds - timezone_minutes(value) * 60
    if base in ("ZXSD_T_DATE", "ZXSD_T_DATE_TIME"):
        match = re.match(
            r"(-?)(\d{4,})-(\d\d)-(\d\d)(?:T(\d\d):(\d\d):(\d\d)(?:\.([0-9]+))?)?",
            value,
        )
        if not match:
            return None
        year = int(match.group(2)) * (-1 if match.group(1) else 1)
        month = int(match.group(3))
        day = int(match.group(4))
        adjusted_year = year - (1 if month <= 2 else 0)
        era = adjusted_year // 400
        year_of_era = adjusted_year - era * 400
        shifted_month = month + (-3 if month > 2 else 9)
        day_of_year = (153 * shifted_month + 2) // 5 + day - 1
        day_number = (
            era * 146097 + year_of_era * 365 + year_of_era // 4 - year_of_era // 100 + day_of_year
        )
        seconds = Decimal(day_number * 86400)
        if match.group(5):
            seconds += (
                int(match.group(5)) * 3600
                + int(match.group(6)) * 60
                + int(match.group(7))
                + Decimal("0." + (match.group(8) or "0"))
            )
        return seconds - timezone_minutes(value) * 60
    if base == "ZXSD_T_GYEAR":
        match = re.match(r"(-?)(\d{4,})", value)
        if match:
            year = int(match.group(2)) * (-1 if match.group(1) else 1)
            return year * 366 * 86400 - timezone_minutes(value) * 60
    if base == "ZXSD_T_GYEAR_MONTH":
        match = re.match(r"(-?)(\d{4,})-(\d\d)", value)
        if match:
            year = int(match.group(2)) * (-1 if match.group(1) else 1)
            return (year * 12 + int(match.group(3))) * 31 * 86400 - timezone_minutes(value) * 60
    if base == "ZXSD_T_GMONTH_DAY":
        match = re.match(r"--(\d\d)-(\d\d)", value)
        if match:
            return (int(match.group(1)) * 31 + int(match.group(2))) * 86400 - timezone_minutes(
                value
            ) * 60
    if base == "ZXSD_T_GMONTH":
        match = re.match(r"--(\d\d)--", value)
        if match:
            return int(match.group(1)) * 31 * 86400 - timezone_minutes(value) * 60
    if base == "ZXSD_T_GDAY":
        match = re.match(r"---(\d\d)", value)
        if match:
            return int(match.group(1)) * 86400 - timezone_minutes(value) * 60
    return None


def compare_ordered_values(base, left, right):
    """Compare ordered XSD values, returning None for an indeterminate duration order."""
    left_key = ordered_value(base, left)
    right_key = ordered_value(base, right)
    if left_key is None or right_key is None:
        return None
    if base == "ZXSD_T_DURATION":
        comparisons = {
            (left_key[index] > right_key[index]) - (left_key[index] < right_key[index])
            for index in range(2)
        }
        comparisons.discard(0)
        if len(comparisons) > 1:
            return None
        return comparisons.pop() if comparisons else 0
    return (left_key > right_key) - (left_key < right_key)


def normalize_token(value):
    """Apply the fixed collapse whitespace behavior of xs:token."""
    return re.sub(r"[ \t\r\n]+", " ", value.strip(" \t\r\n"))
