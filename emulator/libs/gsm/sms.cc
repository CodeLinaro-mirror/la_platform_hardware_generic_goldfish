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
        const size_t bytes_to_copy = (sender.size + 1U) / 2U;
        DCHECK(bytes_to_copy <= std::size(sender.data));
        dst.insert(dst.end(), std::begin(sender.data), std::begin(sender.data) + bytes_to_copy);
    }
        return;
    }

    ABSL_UNREACHABLE();
}

void EncodeSmsHeader(std::vector<uint8_t>& dst, const uint8_t ref_number, const uint8_t pdu_count,
                     const uint8_t pdu_index) {
    dst.push_back(0x05);           // total header length == 5 bytes
    dst.push_back(0x00);           // element id: concatenated message reference number
    dst.push_back(0x03);           // element len: 3 bytes
    dst.push_back(ref_number);     // reference number
    dst.push_back(pdu_count);      // max pdu index
    dst.push_back(pdu_index + 1U);  // current pdu index
}

std::pair<size_t, size_t> CutOnePduGsm7(Utf8Iterator utf8i, size_t remaining_septets) {
    size_t septets_in_this_pdu = 0;
    size_t chars_in_this_pdu = 0;

    for (int32_t unicode; (unicode = utf8i()) >= 0;) {
        const int32_t gsm7 = UnicodeToGsm7(unicode);
        DCHECK(gsm7 >= 0);
        const size_t num_septets = (gsm7 & kGsm7ExtendedBit) ? 2 : 1;

        if (remaining_septets >= num_septets) {
            remaining_septets -= num_septets;
            septets_in_this_pdu += num_septets;
            ++chars_in_this_pdu;
        } else {
            break;
        }
    }

    return {septets_in_this_pdu, chars_in_this_pdu};
}

size_t CutOnePduUcs2(Utf8Iterator utf8i, size_t remaining_chars) {
    size_t chars_in_this_pdu = 0;

    for (int32_t unicode; remaining_chars && (unicode = utf8i()) >= 0;
         --remaining_chars, ++chars_in_this_pdu) {
    }

    return chars_in_this_pdu;
}

Utf8Iterator PackGsm7Septets(std::vector<uint8_t>& dst, Utf8Iterator msgi,
                             size_t msg_chars_in_this_pdu, const unsigned pad_bits) {
    uint32_t shift_register = 0;
    unsigned bits_in_register = pad_bits;

    const auto append7 = [&](const uint8_t x) {
        shift_register |= static_cast<uint32_t>(x) << bits_in_register;
        bits_in_register += 7U;

        while (bits_in_register >= CHAR_BIT) {
            dst.push_back(static_cast<uint8_t>(shift_register));
            shift_register >>= CHAR_BIT;
            bits_in_register -= CHAR_BIT;
        }
    };

    for (; msg_chars_in_this_pdu; --msg_chars_in_this_pdu) {
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
    if (bits_in_register > 0) {
        dst.push_back(static_cast<uint8_t>(shift_register));
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

    const int32_t total_septets = CalculateGsm7SizeSeptets(number);
    if ((total_septets < 0) || (static_cast<size_t>(total_septets) > kMaxAddressSizeSeptets)) {
        return false;
    }

    DCHECK(!number.empty());
    const auto [numSeptets, numChars] = CutOnePduGsm7(Utf8Iterator(number), total_septets);

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

Utf8Iterator EncodeSmsPduGsm7(std::vector<uint8_t>& dst, Utf8Iterator msgi, const size_t num_pdus,
                              const size_t max_septets_per_pdu, const size_t pdu_index) {
    auto [msgSeptetsInThisPdu, msgCharsInThisPdu] = CutOnePduGsm7(msgi, max_septets_per_pdu);

    unsigned pad_bits;
    if (num_pdus > 1U) {
        // 6 bytes of UDH = 48 bits
        constexpr size_t kHeaderBytes = 6U;
        constexpr size_t kHeaderBits = kHeaderBytes * CHAR_BIT;
        constexpr size_t kHeaderSeptets = (kHeaderBits + 7U - 1U) / 7U;
        pad_bits = kHeaderSeptets * 7U - kHeaderBits;
        dst.push_back(kHeaderSeptets + msgSeptetsInThisPdu);
        EncodeSmsHeader(dst, 0, num_pdus, pdu_index);
    } else {
        pad_bits = 0;
        dst.push_back(msgSeptetsInThisPdu);
    }

    return PackGsm7Septets(dst, msgi, msgCharsInThisPdu, pad_bits);
}

Utf8Iterator EncodeSmsPduUcs2(std::vector<uint8_t>& dst, Utf8Iterator msgi, const size_t num_pdus,
                              const size_t max_chars_per_pdu, const size_t pdu_index) {
    size_t chars_in_this_pdu = CutOnePduUcs2(msgi, max_chars_per_pdu);

    if (num_pdus > 1U) {
        constexpr size_t kHeaderBytes = 6U;
        dst.push_back(kHeaderBytes + (chars_in_this_pdu * 2U));
        EncodeSmsHeader(dst, 0, num_pdus, pdu_index);
    } else {
        dst.push_back(chars_in_this_pdu * 2);
    }

    for (; chars_in_this_pdu; --chars_in_this_pdu) {
        int32_t unicode = msgi();
        DCHECK(unicode >= 0);
        DCHECK(IsValidUcs2(unicode));

        dst.push_back(static_cast<uint8_t>(unicode >> CHAR_BIT));
        dst.push_back(static_cast<uint8_t>(unicode));
    }

    return msgi;
}

Utf8Iterator EncodeSmsPdu(std::vector<uint8_t>& dst, const uint8_t message_reference,
                          const SmsAddress& recipient, Utf8Iterator msgi, const size_t num_pdus,
                          const Encoding encoding, const size_t units_per_pdu,
                          const size_t pdu_index) {
    // 1. SCA (Service Center Address): 00 means use default from SIM/Modem
    dst.push_back(0);

    // 2. PDU-Type: 0x01 is SMS-SUBMIT.
    // Bit 6 (0x40) is the UDHI (User Data Header Indicator) for multipart.
    dst.push_back((num_pdus > 1) ? 0x41 : 0x01);

    // 3. MR (Message Reference): Typically an incrementing counter
    dst.push_back(message_reference);

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
        return EncodeSmsPduGsm7(dst, msgi, num_pdus, units_per_pdu, pdu_index);
    case Encoding::UCS2:
        return EncodeSmsPduUcs2(dst, msgi, num_pdus, units_per_pdu, pdu_index);
    }

    ABSL_UNREACHABLE();
}

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromUtf8(const uint8_t message_reference,
                                                    const SmsAddress& sender,
                                                    const std::string_view message,
                                                    const size_t num_pdus, const Encoding encoding,
                                                    const size_t units_per_pdu) {
    std::vector<SmsPdu> pdus(num_pdus);

    Utf8Iterator msgi(message);
    for (size_t i = 0; i < num_pdus; ++i) {
        msgi = EncodeSmsPdu(pdus[i].data, message_reference, sender, msgi, num_pdus, encoding,
                            units_per_pdu, i);
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

    size_t units_per_pdu;
    size_t num_pdus;
    Encoding encoding;

    if (message.empty()) {
        units_per_pdu = kMaxSeptetsPerPdu;
        num_pdus = 1;
        encoding = Encoding::GSM7;
    } else if (const int32_t num_septets = CalculateGsm7SizeSeptets(message); num_septets > 0) {
        if (num_septets <= kMaxSeptetsPerPdu) {
            units_per_pdu = kMaxSeptetsPerPdu;
            num_pdus = 1;
        } else {
            units_per_pdu = kMaxSeptetsWithHeaderPerPdu;
            num_pdus = CalculateGsm7NumPdus(message, units_per_pdu);
        }

        encoding = Encoding::GSM7;
    } else if (const int32_t num_symbols = CalculateUcs2SizeSymbols(message); num_symbols > 0) {
        const size_t num_bytes = sizeof(uint16_t) * static_cast<size_t>(num_symbols);

        const size_t bytes_per_pdu =
                (num_bytes > kMaxBytesPerPdu) ? kMaxBytesWithHeaderPerPdu : num_bytes;

        units_per_pdu = bytes_per_pdu / sizeof(uint16_t);
        num_pdus = (num_bytes + bytes_per_pdu - 1U) / bytes_per_pdu;
        encoding = Encoding::UCS2;
    } else {
        return absl::InvalidArgumentError(
                absl::StrCat("Message is not representable: '", message, "'"));
    }

    if (num_pdus > 255U) {
        return absl::InvalidArgumentError("Message is too long");
    }

    return SmsPdusFromUtf8(0, *address, message, num_pdus, encoding, units_per_pdu);
}

absl::StatusOr<std::vector<SmsPdu>> SmsPdusFromBinary(std::vector<uint8_t> binary) {
    SmsPdu pdu = {.data = std::move(binary)};
    std::vector<SmsPdu> pdus = {std::move(pdu)};
    return pdus;
}

size_t CalculateGsm7NumPdus(const std::string_view message, const size_t septets_per_pdu) {
    DCHECK(septets_per_pdu >= 2);

    size_t num_pdus = 0;
    size_t remaining_septets = 0;

    Utf8Iterator utf8i(message);
    for (int32_t unicode; (unicode = utf8i()) >= 0;) {
        const int32_t gsm7 = UnicodeToGsm7(unicode);
        DCHECK(gsm7 >= 0);
        const size_t num_septets = (gsm7 & kGsm7ExtendedBit) ? 2 : 1;

        if (remaining_septets >= num_septets) {
            remaining_septets -= num_septets;
        } else {
            ++num_pdus;
            remaining_septets = septets_per_pdu - num_septets;
        }
    }

    return num_pdus;
}

}  // namespace goldfish::gsm
