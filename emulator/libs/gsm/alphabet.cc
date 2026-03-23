// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "goldfish/gsm/alphabet.h"

#include "goldfish/parsing/utf8_iterator.h"

namespace goldfish::gsm {
// 3GPP TS 23.038: 6.2.1 GSM 7 bit Default Alphabet
int32_t UnicodeToGsm7(const uint32_t unicode) {
    switch (unicode) {
    case '\n':
        return 0x0A;
    case '\f':
        return kGsm7ExtendedBit | 0x0A;
    case '\r':
        return 0x0D;
    case ' ':
        return 0x20;
    case '!':
        return 0x21;
    case '\"':
        return 0x22;
    case '#':
        return 0x23;
    case '$':
        return 0x02;
    case '%':
        return 0x25;
    case '&':
        return 0x26;
    case '\'':
        return 0x27;
    case '(':
        return 0x28;
    case ')':
        return 0x29;
    case '*':
        return 0x2A;
    case '+':
        return 0x2B;
    case ',':
        return 0x2C;
    case '-':
        return 0x2D;
    case '.':
        return 0x2E;
    case '/':
        return 0x2F;
    case '0':
        return 0x30;
    case '1':
        return 0x31;
    case '2':
        return 0x32;
    case '3':
        return 0x33;
    case '4':
        return 0x34;
    case '5':
        return 0x35;
    case '6':
        return 0x36;
    case '7':
        return 0x37;
    case '8':
        return 0x38;
    case '9':
        return 0x39;
    case ':':
        return 0x3A;
    case ';':
        return 0x3B;
    case '<':
        return 0x3C;
    case '=':
        return 0x3D;
    case '>':
        return 0x3E;
    case '?':
        return 0x3F;
    case '@':
        return 0x00;
    case 'A':
        return 0x41;
    case 'B':
        return 0x42;
    case 'C':
        return 0x43;
    case 'D':
        return 0x44;
    case 'E':
        return 0x45;
    case 'F':
        return 0x46;
    case 'G':
        return 0x47;
    case 'H':
        return 0x48;
    case 'I':
        return 0x49;
    case 'J':
        return 0x4A;
    case 'K':
        return 0x4B;
    case 'L':
        return 0x4C;
    case 'M':
        return 0x4D;
    case 'N':
        return 0x4E;
    case 'O':
        return 0x4F;
    case 'P':
        return 0x50;
    case 'Q':
        return 0x51;
    case 'R':
        return 0x52;
    case 'S':
        return 0x53;
    case 'T':
        return 0x54;
    case 'U':
        return 0x55;
    case 'V':
        return 0x56;
    case 'W':
        return 0x57;
    case 'X':
        return 0x58;
    case 'Y':
        return 0x59;
    case 'Z':
        return 0x5A;
    case '[':
        return kGsm7ExtendedBit | 0x3C;
    case '\\':
        return kGsm7ExtendedBit | 0x2F;
    case ']':
        return kGsm7ExtendedBit | 0x3E;
    case '^':
        return kGsm7ExtendedBit | 0x14;
    case '_':
        return 0x11;
    case 'a':
        return 0x61;
    case 'b':
        return 0x62;
    case 'c':
        return 0x63;
    case 'd':
        return 0x64;
    case 'e':
        return 0x65;
    case 'f':
        return 0x66;
    case 'g':
        return 0x67;
    case 'h':
        return 0x68;
    case 'i':
        return 0x69;
    case 'j':
        return 0x6A;
    case 'k':
        return 0x6B;
    case 'l':
        return 0x6C;
    case 'm':
        return 0x6D;
    case 'n':
        return 0x6E;
    case 'o':
        return 0x6F;
    case 'p':
        return 0x70;
    case 'q':
        return 0x71;
    case 'r':
        return 0x72;
    case 's':
        return 0x73;
    case 't':
        return 0x74;
    case 'u':
        return 0x75;
    case 'v':
        return 0x76;
    case 'w':
        return 0x77;
    case 'x':
        return 0x78;
    case 'y':
        return 0x79;
    case 'z':
        return 0x7A;
    case '{':
        return kGsm7ExtendedBit | 0x28;
    case '|':
        return kGsm7ExtendedBit | 0x40;
    case '}':
        return kGsm7ExtendedBit | 0x29;
    case '~':
        return kGsm7ExtendedBit | 0x3D;
    case 0x00A1:
        return 0x40;  // ¡
    case 0x00A3:
        return 0x01;  // £
    case 0x00A4:
        return 0x24;  // ¤
    case 0x00A5:
        return 0x03;  // ¥
    case 0x00A7:
        return 0x5F;  // §
    case 0x00BF:
        return 0x60;  // ¿
    case 0x00C4:
        return 0x5B;  // Ä
    case 0x00C5:
        return 0x0E;  // Å
    case 0x00C6:
        return 0x1C;  // Æ
    case 0x00C7:
        return 0x09;  // Ç
    case 0x00C9:
        return 0x1F;  // É
    case 0x00D1:
        return 0x5D;  // Ñ
    case 0x00D6:
        return 0x5C;  // Ö
    case 0x00D8:
        return 0x0B;  // Ø
    case 0x00DC:
        return 0x5E;  // Ü
    case 0x00DF:
        return 0x1E;  // ß
    case 0x00E0:
        return 0x7F;  // à
    case 0x00E4:
        return 0x7B;  // ä
    case 0x00E5:
        return 0x0F;  // å
    case 0x00E6:
        return 0x1D;  // æ
    case 0x00E8:
        return 0x04;  // è
    case 0x00E9:
        return 0x05;  // é
    case 0x00EC:
        return 0x07;  // ì
    case 0x00F1:
        return 0x7D;  // ñ
    case 0x00F2:
        return 0x08;  // ò
    case 0x00F6:
        return 0x7C;  // ö
    case 0x00F8:
        return 0x0C;  // ø
    case 0x00F9:
        return 0x06;  // ù
    case 0x00FC:
        return 0x7E;  // ü
    case 0x0393:
        return 0x13;  // Γ
    case 0x0394:
        return 0x10;  // Δ
    case 0x0398:
        return 0x19;  // Θ
    case 0x039B:
        return 0x14;  // Λ
    case 0x039E:
        return 0x1A;  // Ξ
    case 0x03A0:
        return 0x16;  // Π
    case 0x03A3:
        return 0x18;  // Σ
    case 0x03A6:
        return 0x12;  // Φ
    case 0x03A8:
        return 0x17;  // Ψ
    case 0x03A9:
        return 0x15;  // Ω
    case 0x20AC:
        return kGsm7ExtendedBit | 0x65;  // €
    default:
        return -1;
    }
}

bool IsValidUcs2(const uint32_t unicode) {
    // 1. Must be within the 16-bit range
    // 2. Must not be in the UTF-16 surrogate range (D800-DFFF)
    return (unicode < 0xD800U) || (unicode >= 0xE000U && unicode < 0x10000U);
}

int32_t CalculateGsm7SizeSeptets(const std::string_view utf8str) {
    parsing::Utf8Iterator utf8i(utf8str);
    int32_t numSeptets = 0;

    for (int32_t unicode; (unicode = utf8i()) >= 0;) {
        const int32_t gsm7 = UnicodeToGsm7(unicode);
        if (gsm7 < 0) {
            return -1;
        }

        numSeptets += (gsm7 & kGsm7ExtendedBit) ? 2 : 1;
    }

    return numSeptets;
}

int32_t CalculateUcs2SizeSymbols(const std::string_view utf8str) {
    parsing::Utf8Iterator utf8i(utf8str);
    int32_t numSymbols = 0;

    for (int32_t unicode; (unicode = utf8i()) >= 0;) {
        if (!IsValidUcs2(unicode)) {
            return -1;
        }

        ++numSymbols;
    }

    return numSymbols;
}

}  // namespace goldfish::gsm
