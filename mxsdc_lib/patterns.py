# Copyright (c) 2026 mxsd contributors
# SPDX-License-Identifier: Apache-2.0

"""Bounded XML Schema regular-expression grammar and resource accounting."""

import re

from .common import SchemaError


class PatternParser:
    """Validate the XML Schema regular-expression grammar without compiling it."""

    SINGLE_ESCAPES = set("nrt\\|.-^?*+{}()[]")
    MULTI_ESCAPES = set("sSiIcCdDwW")
    CATEGORIES = {
        "L",
        "Lu",
        "Ll",
        "Lt",
        "Lm",
        "Lo",
        "M",
        "Mn",
        "Mc",
        "Me",
        "N",
        "Nd",
        "Nl",
        "No",
        "P",
        "Pc",
        "Pd",
        "Ps",
        "Pe",
        "Pi",
        "Pf",
        "Po",
        "Z",
        "Zs",
        "Zl",
        "Zp",
        "S",
        "Sm",
        "Sc",
        "Sk",
        "So",
        "C",
        "Cc",
        "Cf",
        "Co",
        "Cs",
        "Cn",
    }
    BLOCKS = frozenset("""BasicLatin Latin-1Supplement LatinExtended-A LatinExtended-B IPAExtensions
        SpacingModifierLetters CombiningDiacriticalMarks GreekandCoptic Cyrillic
        CyrillicSupplement Armenian Hebrew Arabic Syriac ArabicSupplement Thaana NKo Samaritan
        Mandaic SyriacSupplement ArabicExtended-B ArabicExtended-A Devanagari Bengali Gurmukhi
        Gujarati Oriya Tamil Telugu Kannada Malayalam Sinhala Thai Lao Tibetan Myanmar Georgian
        HangulJamo Ethiopic EthiopicSupplement Cherokee UnifiedCanadianAboriginalSyllabics Ogham
        Runic Tagalog Hanunoo Buhid Tagbanwa Khmer Mongolian
        UnifiedCanadianAboriginalSyllabicsExtended Limbu TaiLe NewTaiLue KhmerSymbols Buginese
        TaiTham CombiningDiacriticalMarksExtended Balinese Sundanese Batak Lepcha OlChiki
        CyrillicExtended-C GeorgianExtended SundaneseSupplement VedicExtensions PhoneticExtensions
        PhoneticExtensionsSupplement CombiningDiacriticalMarksSupplement LatinExtendedAdditional
        GreekExtended GeneralPunctuation SuperscriptsandSubscripts CurrencySymbols
        CombiningDiacriticalMarksforSymbols LetterlikeSymbols NumberForms Arrows
        MathematicalOperators MiscellaneousTechnical ControlPictures OpticalCharacterRecognition
        EnclosedAlphanumerics BoxDrawing BlockElements GeometricShapes MiscellaneousSymbols Dingbats
        MiscellaneousMathematicalSymbols-A SupplementalArrows-A BraillePatterns SupplementalArrows-B
        MiscellaneousMathematicalSymbols-B SupplementalMathematicalOperators
        MiscellaneousSymbolsandArrows Glagolitic LatinExtended-C Coptic GeorgianSupplement Tifinagh
        EthiopicExtended CyrillicExtended-A SupplementalPunctuation CJKRadicalsSupplement
        KangxiRadicals IdeographicDescriptionCharacters CJKSymbolsandPunctuation Hiragana Katakana
        Bopomofo HangulCompatibilityJamo Kanbun BopomofoExtended CJKStrokes
        KatakanaPhoneticExtensions EnclosedCJKLettersandMonths CJKCompatibility
        CJKUnifiedIdeographsExtensionA YijingHexagramSymbols CJKUnifiedIdeographs YiSyllables
        YiRadicals Lisu Vai CyrillicExtended-B Bamum ModifierToneLetters LatinExtended-D SylotiNagri
        CommonIndicNumberForms Phags-pa Saurashtra DevanagariExtended KayahLi Rejang
        HangulJamoExtended-A Javanese MyanmarExtended-B Cham MyanmarExtended-A TaiViet
        MeeteiMayekExtensions EthiopicExtended-A LatinExtended-E CherokeeSupplement MeeteiMayek
        HangulSyllables HangulJamoExtended-B HighSurrogates HighPrivateUseSurrogates LowSurrogates
        PrivateUseArea CJKCompatibilityIdeographs AlphabeticPresentationForms
        ArabicPresentationForms-A VariationSelectors VerticalForms CombiningHalfMarks
        CJKCompatibilityForms SmallFormVariants ArabicPresentationForms-B HalfwidthandFullwidthForms
        Specials LinearBSyllabary LinearBIdeograms AegeanNumbers AncientGreekNumbers AncientSymbols
        PhaistosDisc Lycian Carian CopticEpactNumbers OldItalic Gothic OldPermic Ugaritic OldPersian
        Deseret Shavian Osmanya Osage Elbasan CaucasianAlbanian Vithkuqi Todhri LinearA
        LatinExtended-F CypriotSyllabary ImperialAramaic Palmyrene Nabataean Hatran Phoenician
        Lydian MeroiticHieroglyphs MeroiticCursive Kharoshthi OldSouthArabian OldNorthArabian
        Manichaean Avestan InscriptionalParthian InscriptionalPahlavi PsalterPahlavi OldTurkic
        OldHungarian HanifiRohingya Garay RumiNumeralSymbols Yezidi ArabicExtended-C OldSogdian
        Sogdian OldUyghur Chorasmian Elymaic Brahmi Kaithi SoraSompeng Chakma Mahajani Sharada
        SinhalaArchaicNumbers Khojki Multani Khudawadi Grantha Tulu-Tigalari Newa Tirhuta Siddham
        Modi MongolianSupplement Takri MyanmarExtended-C Ahom Dogra WarangCiti DivesAkuru
        Nandinagari ZanabazarSquare Soyombo UnifiedCanadianAboriginalSyllabicsExtended-A PauCinHau
        DevanagariExtended-A Sunuwar Bhaiksuki Marchen MasaramGondi GunjalaGondi Makasar Kawi
        LisuSupplement TamilSupplement Cuneiform CuneiformNumbersandPunctuation
        EarlyDynasticCuneiform Cypro-Minoan EgyptianHieroglyphs EgyptianHieroglyphFormatControls
        EgyptianHieroglyphsExtended-A AnatolianHieroglyphs GurungKhema BamumSupplement Mro Tangsa
        BassaVah PahawhHmong KiratRai Medefaidrin Miao IdeographicSymbolsandPunctuation Tangut
        TangutComponents KhitanSmallScript TangutSupplement KanaExtended-B KanaSupplement
        KanaExtended-A SmallKanaExtension Nushu Duployan ShorthandFormatControls
        SymbolsforLegacyComputingSupplement ZnamennyMusicalNotation ByzantineMusicalSymbols
        MusicalSymbols AncientGreekMusicalNotation KaktovikNumerals MayanNumerals TaiXuanJingSymbols
        CountingRodNumerals MathematicalAlphanumericSymbols SuttonSignWriting LatinExtended-G
        GlagoliticSupplement CyrillicExtended-D NyiakengPuachueHmong Toto Wancho NagMundari OlOnal
        EthiopicExtended-B MendeKikakui Adlam IndicSiyaqNumbers OttomanSiyaqNumbers
        ArabicMathematicalAlphabeticSymbols MahjongTiles DominoTiles PlayingCards
        EnclosedAlphanumericSupplement EnclosedIdeographicSupplement
        MiscellaneousSymbolsandPictographs Emoticons OrnamentalDingbats TransportandMapSymbols
        AlchemicalSymbols GeometricShapesExtended SupplementalArrows-C
        SupplementalSymbolsandPictographs ChessSymbols SymbolsandPictographsExtended-A
        SymbolsforLegacyComputing CJKUnifiedIdeographsExtensionB CJKUnifiedIdeographsExtensionC
        CJKUnifiedIdeographsExtensionD CJKUnifiedIdeographsExtensionE CJKUnifiedIdeographsExtensionF
        CJKUnifiedIdeographsExtensionI CJKCompatibilityIdeographsSupplement
        CJKUnifiedIdeographsExtensionG CJKUnifiedIdeographsExtensionH Tags
        VariationSelectorsSupplement SupplementaryPrivateUseArea-A
        SupplementaryPrivateUseArea-B Greek CyrillicSupplementary CombiningMarksforSymbols
        PrivateUse""".split())

    def __init__(self, pattern):
        self.pattern = pattern
        self.position = 0
        self.depth = 0

    def error(self, message):
        raise SchemaError("invalid xs:pattern at offset %d: %s" % (self.position, message))

    def escape(self, in_class=False):
        self.position += 1
        if self.position >= len(self.pattern):
            self.error("trailing escape")
        kind = self.pattern[self.position]
        if kind in self.SINGLE_ESCAPES or kind in self.MULTI_ESCAPES:
            self.position += 1
            return kind in self.SINGLE_ESCAPES
        if kind not in "pP" or self.position + 1 >= len(self.pattern):
            self.error("unsupported character escape")
        end = self.pattern.find("}", self.position + 2)
        if self.pattern[self.position + 1] != "{" or end < 0:
            self.error("malformed Unicode property escape")
        prop = self.pattern[self.position + 2 : end]
        if prop not in self.CATEGORIES and not (prop.startswith("Is") and prop[2:] in self.BLOCKS):
            self.error("invalid Unicode category or block")
        self.position = end + 1
        return False

    def class_character(self):
        if self.position >= len(self.pattern):
            self.error("unterminated character class")
        if self.pattern[self.position] == "\\":
            start = self.position
            if not self.escape(True):
                return False, None
            escape = self.pattern[start + 1]
            values = {"n": "\n", "r": "\r", "t": "\t"}
            return True, ord(values.get(escape, escape))
        elif self.pattern[self.position] in "[]":
            self.error("unescaped bracket in character class")
        else:
            codepoint = ord(self.pattern[self.position])
            self.position += 1
            return True, codepoint

    def character_class(self, subtraction_depth=0):
        if subtraction_depth > 32:
            self.error("character-class subtraction exceeds MXSD_MAX_REGEX_DEPTH")
        self.position += 1
        if self.position < len(self.pattern) and self.pattern[self.position] == "^":
            self.position += 1
        item_count = 0
        while self.position < len(self.pattern) and self.pattern[self.position] != "]":
            if self.pattern.startswith("-[", self.position) and item_count:
                self.position += 1
                self.character_class(subtraction_depth + 1)
                if self.position >= len(self.pattern) or self.pattern[self.position] != "]":
                    self.error("class subtraction must be the final class item")
                break
            if (
                item_count
                and self.pattern[self.position] == "-"
                and self.position + 1 < len(self.pattern)
                and self.pattern[self.position + 1] != "]"
            ):
                # XML Schema permits adjacent ranges in the compact form
                # a-d-b-c; the intervening dash separates the ranges and is
                # not itself a member of the class.
                self.position += 1
                continue
            first_position = self.position
            first_single, first_codepoint = self.class_character()
            item_count += 1
            if (
                first_single
                and not (self.pattern[first_position] == "-" and item_count == 1)
                and self.position < len(self.pattern)
                and self.pattern[self.position] == "-"
                and self.position + 1 < len(self.pattern)
                and self.pattern[self.position + 1] != "]"
                and self.pattern[self.position + 1] != "["
            ):
                self.position += 1
                last_single, last_codepoint = self.class_character()
                if not last_single:
                    self.error("character range endpoints must be single characters")
                if first_codepoint > last_codepoint:
                    self.error("character range is reversed")
        if item_count == 0 or self.position >= len(self.pattern):
            self.error("empty or unterminated character class")
        self.position += 1

    def atom(self):
        if self.position >= len(self.pattern):
            self.error("expected atom")
        character = self.pattern[self.position]
        if character == "(":
            self.depth += 1
            if self.depth > 32:
                self.error("group nesting exceeds MXSD_MAX_REGEX_DEPTH")
            self.position += 1
            self.regexp()
            if self.position >= len(self.pattern) or self.pattern[self.position] != ")":
                self.error("unterminated group")
            self.position += 1
            self.depth -= 1
        elif character == "[":
            self.character_class()
        elif character == "\\":
            self.escape()
        elif character == ".":
            self.position += 1
        elif character in "?*+{}|)]":
            self.error("unexpected operator")
        else:
            self.position += 1

    def quantifier(self):
        if self.position >= len(self.pattern):
            return
        if self.pattern[self.position] in "?*+":
            self.position += 1
            return
        if self.pattern[self.position] != "{":
            return
        match = re.match(r"\{(\d+)(?:,(\d*))?\}", self.pattern[self.position :])
        if not match:
            self.error("invalid repetition")
        minimum = int(match.group(1))
        maximum = minimum
        if match.group(2) is not None:
            maximum = None if match.group(2) == "" else int(match.group(2))
        if minimum > 4294967295 or (maximum is not None and maximum > 4294967295):
            self.error("repetition exceeds the configured representation")
        if maximum is not None and minimum > maximum:
            self.error("repetition minimum exceeds maximum")
        self.position += len(match.group(0))

    def branch(self):
        while self.position < len(self.pattern) and self.pattern[self.position] not in "|)":
            self.atom()
            self.quantifier()

    def regexp(self):
        self.branch()
        while self.position < len(self.pattern) and self.pattern[self.position] == "|":
            self.position += 1
            self.branch()

    def parse(self):
        self.regexp()
        if self.position != len(self.pattern):
            self.error("unexpected closing parenthesis")


def validate_pattern(pattern):
    """Check a complete XML Schema 1.0 regular expression."""
    if len(pattern.encode("utf-8")) > 4096:
        raise SchemaError("pattern is too long")
    PatternParser(pattern).parse()


class PatternResources(PatternParser):
    """Compute runtime AST storage and grammar nesting, matching mxsd_regex.c."""

    def __init__(self, pattern):
        super().__init__(pattern)
        self.nodes = 0
        self.maximum_depth = 0
        self.empty_node = False

    def atom(self):
        grouped = self.pattern[self.position] == "("
        if grouped:
            self.maximum_depth = max(self.maximum_depth, self.depth + 1)
        super().atom()
        if not grouped:
            self.nodes += 1
            self.empty_node = False

    def character_class(self, subtraction_depth=0):
        self.maximum_depth = max(self.maximum_depth, subtraction_depth)
        super().character_class(subtraction_depth)

    def quantifier(self):
        position = self.position
        super().quantifier()
        if self.position != position:
            self.nodes += 1
            self.empty_node = False

    def branch(self):
        pieces = 0
        while self.position < len(self.pattern) and self.pattern[self.position] not in "|)":
            self.atom()
            self.quantifier()
            pieces += 1
        if pieces >= 1024:
            raise SchemaError("Zephyr pattern exceeds the runtime's 1023-piece branch limit")
        if pieces == 0:
            self.nodes += 1
            self.empty_node = True
        elif pieces > 1:
            self.nodes += pieces - 1
            self.empty_node = False

    def regexp(self):
        self.branch()
        empty = self.empty_node
        while self.position < len(self.pattern) and self.pattern[self.position] == "|":
            self.position += 1
            self.branch()
            if not (empty and self.empty_node):
                self.nodes += 1
                empty = False
        self.empty_node = empty
