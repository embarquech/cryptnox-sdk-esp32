/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_verify.cpp
 * @brief Implementation of the local TransferContract field check.
 *
 * ponytail: targeted byte-pattern search on the three TransferContract
 * fields, not a full protobuf parse. Each field is pinned by its tag (and,
 * for the addresses, its 0x15 length prefix), which is unambiguous for a
 * native TRX transfer. Swap in a real TransferContract decoder if this ever
 * needs to cover TRC-20 triggersmartcontract calls, where the recipient
 * lives inside ABI calldata instead of a protobuf field.
 *
 * Self-check:
 * @code
 * g++ -DTRON_VERIFY_SELFTEST tron_verify.cpp -o tron_verify_selftest
 * ./tron_verify_selftest
 * @endcode
 */

#include "tron_verify.h"

#include <string.h>

/* Protobuf field tags inside TransferContract (field number << 3 | wire type). */
#define PB_TAG_OWNER_ADDR   (0x0AU)  /* field 1, length-delimited */
#define PB_TAG_TO_ADDR      (0x12U)  /* field 2, length-delimited */
#define PB_TAG_AMOUNT       (0x18U)  /* field 3, varint */

/** Longest base-128 varint for a uint64. */
#define VARINT_MAX_BYTES    (10U)

/** @brief Find @p needle inside @p haystack. */
static bool bytes_contain(const uint8_t *haystack, size_t haystack_len,
                          const uint8_t *needle, size_t needle_len)
{
    if ((haystack == NULL) || (needle_len > haystack_len)) {
        return false;
    }
    for (size_t i = 0U; i <= (haystack_len - needle_len); i++) {
        if (memcmp(&haystack[i], needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief Encode @p value as a protobuf base-128 varint. Returns its length. */
static size_t encode_varint(uint64_t value, uint8_t *out)
{
    size_t n = 0U;
    do {
        uint8_t byte = (uint8_t)(value & 0x7FU);
        value >>= 7U;
        if (value != 0U) {
            byte |= 0x80U;
        }
        out[n] = byte;
        n++;
    } while (value != 0U);
    return n;
}

const char *tron_verify_transfer(const uint8_t *raw, size_t raw_len,
                                 const uint8_t *owner21, const uint8_t *to21,
                                 uint64_t amount)
{
    uint8_t pattern[2U + TRON_ADDR_RAW_BYTES];

    pattern[0] = PB_TAG_OWNER_ADDR;
    pattern[1] = (uint8_t)TRON_ADDR_RAW_BYTES;
    (void)memcpy(&pattern[2], owner21, TRON_ADDR_RAW_BYTES);
    if (!bytes_contain(raw, raw_len, pattern, sizeof(pattern))) {
        return "sender address mismatch";
    }

    pattern[0] = PB_TAG_TO_ADDR;
    (void)memcpy(&pattern[2], to21, TRON_ADDR_RAW_BYTES);
    if (!bytes_contain(raw, raw_len, pattern, sizeof(pattern))) {
        return "recipient address mismatch";
    }

    uint8_t amount_pattern[1U + VARINT_MAX_BYTES];
    amount_pattern[0] = PB_TAG_AMOUNT;
    size_t amount_len = 1U + encode_varint(amount, &amount_pattern[1]);
    if (!bytes_contain(raw, raw_len, amount_pattern, amount_len)) {
        return "amount mismatch";
    }

    return NULL;
}

/******************************************************************
 * Self-check
 ******************************************************************/

#ifdef TRON_VERIFY_SELFTEST

#include <assert.h>
#include <stdio.h>

/* A Transaction.raw for a TRX transfer, laid out per the protobuf schema:
 *   raw: 1 ref_block_bytes | 4 ref_block_hash | 8 expiration
 *        11 contract { 1 type | 2 parameter: Any { 1 type_url | 2 value } }
 *        14 timestamp
 * The nested `value` is the TransferContract the check has to find. */
static size_t build_sample_raw(uint8_t *out, const uint8_t *owner21,
                               const uint8_t *to21, uint64_t amount)
{
    uint8_t transfer[64];
    size_t  t = 0U;

    transfer[t++] = PB_TAG_OWNER_ADDR;
    transfer[t++] = (uint8_t)TRON_ADDR_RAW_BYTES;
    (void)memcpy(&transfer[t], owner21, TRON_ADDR_RAW_BYTES);
    t += TRON_ADDR_RAW_BYTES;
    transfer[t++] = PB_TAG_TO_ADDR;
    transfer[t++] = (uint8_t)TRON_ADDR_RAW_BYTES;
    (void)memcpy(&transfer[t], to21, TRON_ADDR_RAW_BYTES);
    t += TRON_ADDR_RAW_BYTES;
    transfer[t++] = PB_TAG_AMOUNT;
    t += encode_varint(amount, &transfer[t]);

    static const char TYPE_URL[] = "type.googleapis.com/protocol.TransferContract";
    const size_t url_len = sizeof(TYPE_URL) - 1U;

    size_t n = 0U;
    /* ref_block_bytes (2) */
    out[n++] = 0x0AU; out[n++] = 0x02U; out[n++] = 0x02U; out[n++] = 0xDBU;
    /* ref_block_hash (8) */
    out[n++] = 0x22U; out[n++] = 0x08U;
    for (unsigned i = 0U; i < 8U; i++) { out[n++] = (uint8_t)(0xC0U + i); }
    /* expiration (varint) */
    out[n++] = 0x40U; n += encode_varint(1739000000000ULL, &out[n]);
    /* contract: field 11, length-delimited */
    const size_t any_len      = 2U + url_len + 2U + t;   /* type_url + value */
    const size_t contract_len = 2U + 2U + any_len;       /* type + parameter */
    out[n++] = 0x5AU; out[n++] = (uint8_t)contract_len;
    out[n++] = 0x08U; out[n++] = 0x01U;                  /* type = TransferContract */
    out[n++] = 0x12U; out[n++] = (uint8_t)any_len;       /* parameter (Any) */
    out[n++] = 0x0AU; out[n++] = (uint8_t)url_len;
    (void)memcpy(&out[n], TYPE_URL, url_len); n += url_len;
    out[n++] = 0x12U; out[n++] = (uint8_t)t;
    (void)memcpy(&out[n], transfer, t); n += t;
    /* timestamp (varint) */
    out[n++] = 0x70U; n += encode_varint(1738999000000ULL, &out[n]);

    return n;
}

int main(void)
{
    uint8_t owner[TRON_ADDR_RAW_BYTES];
    uint8_t to[TRON_ADDR_RAW_BYTES];
    uint8_t other[TRON_ADDR_RAW_BYTES];
    for (unsigned i = 0U; i < TRON_ADDR_RAW_BYTES; i++) {
        owner[i] = (uint8_t)((i == 0U) ? 0x41U : (0x10U + i));
        to[i]    = (uint8_t)((i == 0U) ? 0x41U : (0x50U + i));
        other[i] = (uint8_t)((i == 0U) ? 0x41U : (0x90U + i));
    }

    uint8_t raw[256];
    const uint64_t amount  = 1000000ULL;   /* 1 TRX */
    const size_t   raw_len = build_sample_raw(raw, owner, to, amount);

    /* Matching transaction is accepted. */
    assert(tron_verify_transfer(raw, raw_len, owner, to, amount) == NULL);

    /* A node that swapped the recipient, sender, or amount is caught. */
    assert(tron_verify_transfer(raw, raw_len, owner, other, amount) != NULL);
    assert(tron_verify_transfer(raw, raw_len, other, to, amount) != NULL);
    assert(tron_verify_transfer(raw, raw_len, owner, to, amount + 1U) != NULL);

    /* The recipient must appear behind the *to* tag, not just anywhere:
     * a transaction paying `owner` twice must not pass as owner -> to. */
    const size_t swapped_len = build_sample_raw(raw, owner, owner, amount);
    assert(tron_verify_transfer(raw, swapped_len, owner, to, amount) != NULL);

    /* Truncated input must not read out of bounds or pass. */
    assert(tron_verify_transfer(raw, 4U, owner, to, amount) != NULL);

    printf("tron_verify self-check OK\n");
    return 0;
}

#endif /* TRON_VERIFY_SELFTEST */
