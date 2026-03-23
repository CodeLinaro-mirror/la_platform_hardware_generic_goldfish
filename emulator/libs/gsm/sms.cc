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
#include "goldfish/gsm/sms.h"

#include <algorithm>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

#include "goldfish/gsm/alphabet.h"

namespace goldfish::gsm {
using parsing::Utf8Iterator;

namespace {

constexpr size_t kMaxSeptetsPerPdu = 160;
constexpr size_t kMaxBytesPerPdu = 140;

/*
 * If a message does not fit onto one PDU, it is delivered
 * via several PDU but each one needs an extra  header.
 */
constexpr size_t kMaxSeptetsWithHeaderPerPdu = 153;
constexpr size_t kMaxBytesWithHeaderPerPdu = 134;

void EncodeSmsAddress(std::vector<uint8_t>& dst, const SmsAddress& sender) {
    dst.push_back(sender.size);
    dst.push_back(static_cast<uint8_t>(sender.toa));

    switch (sender.toa) {
    case SmsAddress::TOA::DOMESTIC:
    case SmsAddress::TOA::INTERNATIONAL:
    case SmsAddress::TOA::ALPHANUMERIC: {
        const size_t bytesToCopy = (sender.size + 1U) / 2U;
        DCHECK(bytesToCopy <= std::size(sender.data));
        dst.insert(dst.end(), std::begin(sender.data), std::begin(sender.data) + bytesToCopy);
    }
        return;
    }

    ABSL_UNREACHABLE();
}

void EncodeSmsHeader(std::vector<uint8_t>& dst, const uint8_t refNumber, const uint8_t pduCount,
                     const uint8_t pduIndex) {
    dst.push_back(0x05);           // total header length == 5 bytes
    dst.push_back(0x00);           // element id: concatenated message reference number
    dst.push_back(0x03);           // element len: 3 bytes
    dst.push_back(refNumber);      // reference number
    dst.push_back(pduCount);       // max pdu index
    dst.push_back(pduIndex + 1U);  // current pdu index
}

std::pair<size_t, size_t> CutOnePduGsm7(Utf8Iterator utf8i, size_t remainingSeptets) {
    size_t septetsInThisPdu = 0;
    size_t charsInThisPdu = 0;

    for (int32_t unicode; (unicode = utf8i()) >= 0;) {
        const int32_t gsm7 = UnicodeToGsm7(unicode);
        DCHECK(gsm7 >= 0);
        const size_t numSeptets = (gsm7 & kGsm7ExtendedBit) ? 2 : 1;

        if (remainingSeptets >= numSeptets) {
            remainingSeptets -= numSeptets;
            septetsInThisPdu += numSeptets;
            ++charsInThisPdu;
        } else {
            break;
        }
    }

    return {septetsInThisPdu, charsInThisPdu};
}

size_t CutOnePduUcs2(Utf8Iterator utf8i, size_t remainingChars) {
    size_t charsInThisPdu = 0;

    for (int32_t unicode; remainingChars && (unicode = utf8i()) >= 0;
         --remainingChars, ++charsInThisPdu) {
    }

    return charsInThisPdu;
}

Utf8Iterator PackGsm7Septets(std::vector<uint8_t>& dst, Utf8Iterator msgi, size_t msgCharsInThisPdu,
                             const unsigned padBits) {
    uint32_t shiftRegister = 0;
    unsigned bitsInRegister = padBits;

    const auto append7 = [&](const uint8_t x) {
        shiftRegister |= uint32_t(x) << bitsInRegister;
        bitsInRegister += 7U;

        while (bitsInRegister >= CHAR_BIT) {
            dst.push_back(static_cast<uint8_t>(shiftRegister));
            shiftRegister >>= CHAR_BIT;
            bitsInRegister -= CHAR_BIT;
        }
    };

    for (; msgCharsInThisPdu; --msgCharsInThisPdu) {
        const int32_t unicode = msgi();
        int32_t gsm7 = UnicodeToGsm7(unicode);
        DCHECK(gsm7 >= 0);

        // Handle Extended Alphabet (Escape 0x1B + Index)
        if (gsm7 & kGsm7ExtendedBit) {
            append7(0x1BU);             // 1. Pack the ESC code (0x1B)
            gsm7 &= ~kGsm7ExtendedBit;  // 2. Clear the extended bit to get the raw 7-bit index
        }

        append7(static_cast<uint8_t>(gsm7));
    }

    // Flush any remaining bits.
    // Handsets expect trailing bits in the last octet to be 0.
    if (bitsInRegister > 0) {
        dst.push_back(static_cast<uint8_t>(shiftRegister));
    }

    return msgi;
}

bool ParseSmsAddressNumeric(const std::string_view number, SmsAddress& dst) {
    if (number.empty()) return false;

    size_t i = 0;
    // Skip any leading whitespace before checking for '+'
    while (i < number.size() && std::isspace(static_cast<unsigned char>(number[i]))) {
        i++;
    }

    if (i < number.size() && number[i] == '+') {
        dst.toa = SmsAddress::TOA::INTERNATIONAL;
        i++;
    } else {
        dst.toa = SmsAddress::TOA::DOMESTIC;
    }

    size_t digit_count = 0;
    const size_t max_digits = dst.data.size() * 2U;
    ::memset(dst.data.data(), 0, dst.data.size());

    for (; i < number.size(); ++i) {
        const char c = number[i];

        if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == '(' || c == ')') {
            continue;
        }

        if (c < '0' || c > '9') {
            return false;
        }

        if (digit_count >= max_digits) {
            return false;
        }

        dst.data[digit_count / 2U] |= (c - '0') << ((digit_count % 2U) * 4U);
        digit_count++;
    }

    if (digit_count == 0) {
        return false;
    }

    if (digit_count % 2) {
        dst.data[digit_count / 2] |= 0xF0U;
    }

    dst.size = digit_count;
    return true;
}

bool ParseSmsAddressAlphabetic(const std::string_view number, SmsAddress& dst) {
    constexpr size_t kMaxAddressSizeSeptets = 11;

    const int32_t totalSeptets = CalculateGsm7SizeSeptets(number);
    if ((totalSeptets < 0) || (size_t(totalSeptets) > kMaxAddressSizeSeptets)) {
        return false;
    }

    DCHECK(!number.empty());
    const auto [numSeptets, numChars] = CutOnePduGsm7(Utf8Iterator(number), totalSeptets);

    std::vector<uint8_t> buffer;
    PackGsm7Septets(buffer, Utf8Iterator(number), numChars, 0);
    if (buffer.size() > dst.data.size()) {
        return false;
    }

    std::fill(dst.data.begin(), dst.data.end(), 0);
    std::copy(buffer.begin(), buffer.end(), dst.data.begin());

    dst.toa = SmsAddress::TOA::ALPHANUMERIC;
    dst.size = (numSeptets * 7U + 4U - 1U) / 4U;  // This is a multiple of nibbles
    return true;
}

Utf8Iterator EncodeSmsPduGsm7(std::vector<uint8_t>& dst, Utf8Iterator msgi, const size_t numPdus,
                              const size_t maxSeptetsPerPdu, const size_t pduIndex) {
    auto [msgSeptetsInThisPdu, msgCharsInThisPdu] = CutOnePduGsm7(msgi, maxSeptetsPerPdu);

    unsigned padBits;
    if (numPdus > 1U) {
        // 6 bytes of UDH = 48 bits
        constexpr size_t kHeaderBytes = 6U;
        constexpr size_t kHeaderBits = kHeaderBytes * CHAR_BIT;
        constexpr size_t kHeaderSeptets = (kHeaderBits + 7U - 1U) / 7U;
        padBits = kHeaderSeptets * 7U - kHeaderBits;
        dst.push_back(kHeaderSeptets + msgSeptetsInThisPdu);
        EncodeSmsHeader(dst, 0, numPdus, pduIndex);
    } else {
        padBits = 0;
        dst.push_back(msgSeptetsInThisPdu);
    }

    return PackGsm7Septets(dst, msgi, msgCharsInThisPdu, padBits);
}

Utf8Iterator EncodeSmsPduUcs2(std::vector<uint8_t>& dst, Utf8Iterator msgi, const size_t numPdus,
                              const size_t maxCharsPerPdu, const size_t pduIndex) {
    size_t charsInThisPdu = CutOnePduUcs2(msgi, maxCharsPerPdu);

    if (numPdus > 1U) {
        constexpr size_t kHeaderBytes = 6U;
        dst.push_back(kHeaderBytes + (charsInThisPdu * 2U));
        EncodeSmsHeader(dst, 0, numPdus, pduIndex);
    } else {
        dst.push_back(charsInThisPdu * 2);
    }

    for (; charsInThisPdu; --charsInThisPdu) {
        int32_t unicode = msgi();
        DCHECK(unicode >= 0);
        DCHECK(IsValidUcs2(unicode));

        dst.push_back(static_cast<uint8_t>(unicode >> CHAR_BIT));
        dst.push_back(static_cast<uint8_t>(unicode));
    }

    return msgi;
}

Utf8Iterator EncodeSmsPdu(std::vector<uint8_t>& dst, const uint8_t messageReference,
                          const SmsAddress& recipient, Utf8Iterator msgi, const size_t numPdus,
                          const Encoding encoding, const size_t unitsPerPdu,
                          const size_t pduIndex) {
    // 1. SCA (Service Center Address): 00 means use default from SIM/Modem
    dst.push_back(0);

    // 2. PDU-Type: 0x01 is SMS-SUBMIT.
    // Bit 6 (0x40) is the UDHI (User Data Header Indicator) for multipart.
    dst.push_back((numPdus > 1) ? 0x41 : 0x01);

    // 3. MR (Message Reference): Typically an incrementing counter
    dst.push_back(messageReference);

    // 4. DA (Destination Address): Replaces OA/Sender
    EncodeSmsAddress(dst, recipient);

    // 5. PID (Protocol Identifier): 00 is standard
    dst.push_back(0);

    // 6. DCS (Data Coding Scheme): 00 for GSM7, 08 for UCS2
    dst.push_back((encoding == Encoding::GSM7) ? 0x00 : 0x08);

    // Note: VP (Validity Period) is omitted here because PDU-Type bits 3-4 are 0.

    // 7 & 8. UDL and UD (handled inside specific encoders)
    switch (encoding) {
    case Encoding::GSM7:
        return EncodeSmsPduGsm7(dst, msgi, numPdus, unitsPerPdu, pduIndex);
    case Encoding::UCS2:
        return EncodeSmsPduUcs2(dst, msgi, numPdus, unitsPerPdu, pduIndex);
    }

    ABSL_UNREACHABLE();
}

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromUtf8(const uint8_t messageReference,
                                                    const SmsAddress& sender,
                                                    const std::string_view message,
                                                    const size_t numPdus, const Encoding encoding,
                                                    const size_t unitsPerPdu) {
    std::vector<SmsPdu> pdus(numPdus);

    Utf8Iterator msgi(message);
    for (size_t i = 0; i < numPdus; ++i) {
        msgi = EncodeSmsPdu(pdus[i].data, messageReference, sender, msgi, numPdus, encoding,
                            unitsPerPdu, i);
    }

    return pdus;
}

}  // namespace

absl::StatusOr<SmsAddress> ParseSmsAddress(const std::string_view str) {
    if (str.empty()) {
        return absl::InvalidArgumentError("The number is empty");
    }

    SmsAddress address;
    if (ParseSmsAddressNumeric(str, address)) {
        return address;
    } else if (ParseSmsAddressAlphabetic(str, address)) {
        return address;
    } else {
        return absl::InvalidArgumentError(absl::StrCat("Invalid number: ", str));
    }
}

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromUtf8(const std::string_view sender,
                                                    const std::string_view message) {
    auto address = ParseSmsAddress(sender);
    if (!address.ok()) {
        return std::move(address.status());
    }

    size_t unitsPerPdu;
    size_t numPdus;
    Encoding encoding;

    if (message.empty()) {
        unitsPerPdu = kMaxSeptetsPerPdu;
        numPdus = 1;
        encoding = Encoding::GSM7;
    } else if (const int32_t numSeptets = CalculateGsm7SizeSeptets(message); numSeptets > 0) {
        if (numSeptets <= kMaxSeptetsPerPdu) {
            unitsPerPdu = kMaxSeptetsPerPdu;
            numPdus = 1;
        } else {
            unitsPerPdu = kMaxSeptetsWithHeaderPerPdu;
            numPdus = CalculateGsm7NumPdus(message, unitsPerPdu);
        }

        encoding = Encoding::GSM7;
    } else if (const int32_t numSymbols = CalculateUcs2SizeSymbols(message); numSymbols > 0) {
        const size_t numBytes = sizeof(uint16_t) * size_t(numSymbols);

        const size_t bytesPedPdu =
                (numBytes > kMaxBytesPerPdu) ? kMaxBytesWithHeaderPerPdu : numBytes;

        unitsPerPdu = bytesPedPdu / sizeof(uint16_t);
        numPdus = (numBytes + bytesPedPdu - 1U) / bytesPedPdu;
        encoding = Encoding::UCS2;
    } else {
        return absl::InvalidArgumentError(
                absl::StrCat("Message is not representable: '", message, "'"));
    }

    if (numPdus > 255U) {
        return absl::InvalidArgumentError("Message is too long");
    }

    return SmsPdusFromUtf8(0, *address, message, numPdus, encoding, unitsPerPdu);
}

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromBinary(std::vector<uint8_t> binary) {
    SmsPdu pdu = {.data = std::move(binary)};
    std::vector<SmsPdu> pdus = {std::move(pdu)};
    return pdus;
}

size_t CalculateGsm7NumPdus(const std::string_view message, const size_t septetsPerPdu) {
    DCHECK(septetsPerPdu >= 2);

    size_t numPdus = 0;
    size_t remainingSeptets = 0;

    Utf8Iterator utf8i(message);
    for (int32_t unicode; (unicode = utf8i()) >= 0;) {
        const int32_t gsm7 = UnicodeToGsm7(unicode);
        DCHECK(gsm7 >= 0);
        const size_t numSeptets = (gsm7 & kGsm7ExtendedBit) ? 2 : 1;

        if (remainingSeptets >= numSeptets) {
            remainingSeptets -= numSeptets;
        } else {
            ++numPdus;
            remainingSeptets = septetsPerPdu - numSeptets;
        }
    }

    return numPdus;
}

}  // namespace goldfish::gsm
