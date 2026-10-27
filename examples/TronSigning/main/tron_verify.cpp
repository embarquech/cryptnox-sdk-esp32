/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_verify.cpp
 * @brief Implementation of the local TransferContract field check.
 *
 * Decodes the protobuf nesting
 * @c raw_data.contract[0].parameter.value and reads @c owner_address,
 * @c to_address and @c amount at their decoded positions. The contract
 * must be a single @c TransferContract (type and @c Any.type_url both
 * checked); unknown or repeated fields inside it are rejected, so bytes
 * smuggled into another field or another contract type cannot pass.
 *
 * Self-check:
 * @code
 * g++ -DTRON_VERIFY_SELFTEST tron_verify.cpp -o tron_verify_selftest
 * ./tron_verify_selftest
 * @endcode
 */

#include "tron_verify.h"

#include <string.h>

/* Protobuf wire format. */
#define PB_TAG_SHIFT          (3U)
#define PB_WIRE_MASK          (0x07U)
#define PB_WIRE_VARINT        (0U)
#define PB_WIRE_FIXED64       (1U)
#define PB_WIRE_LEN           (2U)
#define PB_WIRE_FIXED32       (5U)
#define PB_FIXED64_BYTES      (8U)
#define PB_FIXED32_BYTES      (4U)

#define VARINT_PAYLOAD_MASK   (0x7FU)
#define VARINT_CONT_BIT       (0x80U)
#define VARINT_BITS_PER_BYTE  (7U)
/** Longest base-128 varint for a uint64. */
#define VARINT_MAX_BYTES      (10U)
/** The 10th varint byte carries only bit 63; anything larger overflows. */
#define VARINT_LAST_BYTE_MAX  (0x01U)

/* Transaction.raw */
#define RAW_FIELD_CONTRACT        (11U)
/* Transaction.Contract */
#define CONTRACT_FIELD_TYPE       (1U)
#define CONTRACT_FIELD_PARAMETER  (2U)
#define CONTRACT_TYPE_TRANSFER    (1U)
/* google.protobuf.Any */
#define ANY_FIELD_TYPE_URL        (1U)
#define ANY_FIELD_VALUE           (2U)
/* TransferContract */
#define TRANSFER_FIELD_OWNER      (1U)
#define TRANSFER_FIELD_TO         (2U)
#define TRANSFER_FIELD_AMOUNT     (3U)

static const char TRANSFER_TYPE_URL[] = "type.googleapis.com/protocol.TransferContract";

static const char ERR_MALFORMED[]    = "malformed protobuf";
static const char ERR_NOT_TRANSFER[] = "not a TransferContract";
static const char ERR_UNEXPECTED[]   = "unexpected or repeated field";
static const char ERR_CONTRACTS[]    = "expected exactly one contract";
static const char ERR_OWNER[]        = "sender address mismatch";
static const char ERR_TO[]           = "recipient address mismatch";
static const char ERR_AMOUNT[]       = "amount mismatch";

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
} pb_reader_t;

typedef struct {
    uint64_t       number;
    uint32_t       wire;
    uint64_t       varint;     /* value (VARINT) or length (LEN) */
    const uint8_t *data;       /* LEN payload */
    size_t         data_len;
} pb_field_t;

/** @brief Read one base-128 varint. False on truncation or overflow. */
static bool pb_read_varint(pb_reader_t *r, uint64_t *out)
{
    uint64_t value = 0U;
    uint32_t count = 0U;
    bool     done  = false;
    bool     ok    = true;

    while ((!done) && ok) {
        if ((r->pos >= r->len) || (count >= VARINT_MAX_BYTES)) {
            ok = false;
        } else {
            const uint8_t byte = r->buf[r->pos];
            r->pos++;
            if ((count == (VARINT_MAX_BYTES - 1U)) &&
                ((uint32_t)(byte & VARINT_PAYLOAD_MASK) > VARINT_LAST_BYTE_MAX)) {
                ok = false;
            } else {
                value |= ((uint64_t)(byte & VARINT_PAYLOAD_MASK)) << (count * VARINT_BITS_PER_BYTE);
                done = ((byte & VARINT_CONT_BIT) == 0U);
            }
            count++;
        }
    }
    *out = value;
    return ok;
}

/** @brief Advance past @p n bytes. False if fewer remain. */
static bool pb_skip(pb_reader_t *r, size_t n)
{
    bool ok = (n <= (r->len - r->pos));
    if (ok) {
        r->pos += n;
    }
    return ok;
}

/** @brief Read the next field (tag + value) and advance past it. */
static bool pb_next(pb_reader_t *r, pb_field_t *f)
{
    uint64_t tag = 0U;
    bool     ok  = false;

    (void)memset(f, 0, sizeof(*f));
    ok = pb_read_varint(r, &tag);
    if (ok) {
        f->number = tag >> PB_TAG_SHIFT;
        f->wire   = (uint32_t)(tag & PB_WIRE_MASK);
        ok = (f->number != 0U);
    }
    if (ok) {
        switch (f->wire) {
        case PB_WIRE_VARINT:
            ok = pb_read_varint(r, &f->varint);
            break;
        case PB_WIRE_LEN:
            ok = pb_read_varint(r, &f->varint);
            if (ok) {
                ok = (f->varint <= (uint64_t)(r->len - r->pos));
            }
            if (ok) {
                f->data     = &r->buf[r->pos];
                f->data_len = (size_t)f->varint;
                r->pos += f->data_len;
            }
            break;
        case PB_WIRE_FIXED64:
            ok = pb_skip(r, PB_FIXED64_BYTES);
            break;
        case PB_WIRE_FIXED32:
            ok = pb_skip(r, PB_FIXED32_BYTES);
            break;
        default:
            ok = false;
            break;
        }
    }
    return ok;
}

/** @brief True when @p f is length-delimited and equals @p expected. */
static bool pb_bytes_equal(const pb_field_t *f, const uint8_t *expected, size_t len)
{
    return (f->data_len == len) && (memcmp(f->data, expected, len) == 0);
}

/** @brief True when @p f is field @p number with wire type @p wire. */
static bool pb_is(const pb_field_t *f, uint64_t number, uint32_t wire)
{
    return (f->number == number) && (f->wire == wire);
}

/** @brief Check a TransferContract message: each field exactly once, nothing else. */
static const char *check_transfer(const uint8_t *buf, size_t len,
                                  const uint8_t *owner21, const uint8_t *to21,
                                  uint64_t amount)
{
    pb_reader_t r = { buf, len, 0U };
    pb_field_t  f;
    bool        seen_owner  = false;
    bool        seen_to     = false;
    bool        seen_amount = false;
    const char *reason      = NULL;

    (void)memset(&f, 0, sizeof(f));
    while ((reason == NULL) && (r.pos < r.len)) {
        if (!pb_next(&r, &f)) {
            reason = ERR_MALFORMED;
        } else if (pb_is(&f, TRANSFER_FIELD_OWNER, PB_WIRE_LEN) && (!seen_owner)) {
            seen_owner = true;
            if (!pb_bytes_equal(&f, owner21, TRON_ADDR_RAW_BYTES)) {
                reason = ERR_OWNER;
            }
        } else if (pb_is(&f, TRANSFER_FIELD_TO, PB_WIRE_LEN) && (!seen_to)) {
            seen_to = true;
            if (!pb_bytes_equal(&f, to21, TRON_ADDR_RAW_BYTES)) {
                reason = ERR_TO;
            }
        } else if (pb_is(&f, TRANSFER_FIELD_AMOUNT, PB_WIRE_VARINT) && (!seen_amount)) {
            seen_amount = true;
            if (f.varint != amount) {
                reason = ERR_AMOUNT;
            }
        } else {
            reason = ERR_UNEXPECTED;
        }
    }

    if (reason == NULL) {
        if (!seen_owner) {
            reason = ERR_OWNER;
        } else if (!seen_to) {
            reason = ERR_TO;
        } else if (!seen_amount) {
            reason = ERR_AMOUNT;
        } else {
            /* all three present and matching */
        }
    }
    return reason;
}

/** @brief Check an Any: type_url must name TransferContract, value is checked. */
static const char *check_any(const uint8_t *buf, size_t len,
                             const uint8_t *owner21, const uint8_t *to21,
                             uint64_t amount)
{
    pb_reader_t    r = { buf, len, 0U };
    pb_field_t     f;
    bool           seen_url   = false;
    const uint8_t *value      = NULL;
    size_t         value_len  = 0U;
    const char    *reason     = NULL;

    (void)memset(&f, 0, sizeof(f));
    while ((reason == NULL) && (r.pos < r.len)) {
        if (!pb_next(&r, &f)) {
            reason = ERR_MALFORMED;
        } else if (pb_is(&f, ANY_FIELD_TYPE_URL, PB_WIRE_LEN) && (!seen_url)) {
            seen_url = true;
            if (!pb_bytes_equal(&f, (const uint8_t *)TRANSFER_TYPE_URL,
                                sizeof(TRANSFER_TYPE_URL) - 1U)) {
                reason = ERR_NOT_TRANSFER;
            }
        } else if (pb_is(&f, ANY_FIELD_VALUE, PB_WIRE_LEN) && (value == NULL)) {
            value     = f.data;
            value_len = f.data_len;
        } else {
            reason = ERR_UNEXPECTED;
        }
    }

    if (reason == NULL) {
        if (!seen_url) {
            reason = ERR_NOT_TRANSFER;
        } else if (value == NULL) {
            reason = ERR_MALFORMED;
        } else {
            reason = check_transfer(value, value_len, owner21, to21, amount);
        }
    }
    return reason;
}

/** @brief Check a Transaction.Contract: type must be TransferContract. */
static const char *check_contract(const uint8_t *buf, size_t len,
                                  const uint8_t *owner21, const uint8_t *to21,
                                  uint64_t amount)
{
    pb_reader_t    r = { buf, len, 0U };
    pb_field_t     f;
    bool           seen_type = false;
    const uint8_t *param     = NULL;
    size_t         param_len = 0U;
    const char    *reason    = NULL;

    (void)memset(&f, 0, sizeof(f));
    while ((reason == NULL) && (r.pos < r.len)) {
        if (!pb_next(&r, &f)) {
            reason = ERR_MALFORMED;
        } else if (pb_is(&f, CONTRACT_FIELD_TYPE, PB_WIRE_VARINT) && (!seen_type)) {
            seen_type = true;
            if (f.varint != CONTRACT_TYPE_TRANSFER) {
                reason = ERR_NOT_TRANSFER;
            }
        } else if (pb_is(&f, CONTRACT_FIELD_PARAMETER, PB_WIRE_LEN) && (param == NULL)) {
            param     = f.data;
            param_len = f.data_len;
        } else {
            reason = ERR_UNEXPECTED;
        }
    }

    if (reason == NULL) {
        if (!seen_type) {
            reason = ERR_NOT_TRANSFER;   /* proto3 default 0 = AccountCreateContract */
        } else if (param == NULL) {
            reason = ERR_MALFORMED;
        } else {
            reason = check_any(param, param_len, owner21, to21, amount);
        }
    }
    return reason;
}

const char *tron_verify_transfer(const uint8_t *raw, size_t raw_len,
                                 const uint8_t *owner21, const uint8_t *to21,
                                 uint64_t amount)
{
    pb_reader_t    r = { raw, raw_len, 0U };
    pb_field_t     f;
    const uint8_t *contract     = NULL;
    size_t         contract_len = 0U;
    const char    *reason       = NULL;

    (void)memset(&f, 0, sizeof(f));
    if ((raw == NULL) || (owner21 == NULL) || (to21 == NULL)) {
        reason = ERR_MALFORMED;
    }

    /* Other raw fields (ref block, expiration, timestamp, memo...) do not
     * move funds and are only checked for being well-formed. */
    while ((reason == NULL) && (r.pos < r.len)) {
        if (!pb_next(&r, &f)) {
            reason = ERR_MALFORMED;
        } else if (pb_is(&f, RAW_FIELD_CONTRACT, PB_WIRE_LEN)) {
            if (contract != NULL) {
                reason = ERR_CONTRACTS;
            } else {
                contract     = f.data;
                contract_len = f.data_len;
            }
        } else if (f.number == RAW_FIELD_CONTRACT) {
            reason = ERR_MALFORMED;
        } else {
            /* skipped */
        }
    }

    if (reason == NULL) {
        if (contract == NULL) {
            reason = ERR_CONTRACTS;
        } else {
            reason = check_contract(contract, contract_len, owner21, to21, amount);
        }
    }
    return reason;
}

/******************************************************************
 * Self-check
 ******************************************************************/

#ifdef TRON_VERIFY_SELFTEST

#include <assert.h>
#include <stdio.h>

#define TAG_OWNER   (0x0AU)   /* field 1, LEN    */
#define TAG_TO      (0x12U)   /* field 2, LEN    */
#define TAG_AMOUNT  (0x18U)   /* field 3, VARINT */
#define CONTRACT_TYPE_TRIGGER (31U)

static const char TRIGGER_TYPE_URL[] = "type.googleapis.com/protocol.TriggerSmartContract";

/** @brief Encode @p value as a protobuf base-128 varint. Returns its length. */
static size_t encode_varint(uint64_t value, uint8_t *out)
{
    uint64_t v = value;
    size_t   n = 0U;
    do {
        uint8_t byte = (uint8_t)(v & VARINT_PAYLOAD_MASK);
        v >>= VARINT_BITS_PER_BYTE;
        if (v != 0U) {
            byte |= VARINT_CONT_BIT;
        }
        out[n] = byte;
        n++;
    } while (v != 0U);
    return n;
}

/** @brief Append tag + length + bytes. */
static size_t put_len(uint8_t *out, uint8_t tag, const uint8_t *data, size_t len)
{
    size_t n = 0U;
    out[n] = tag;
    n++;
    n += encode_varint((uint64_t)len, &out[n]);
    (void)memcpy(&out[n], data, len);
    n += len;
    return n;
}

/* A Transaction.raw for a TRX transfer, laid out per the protobuf schema:
 *   raw: 1 ref_block_bytes | 4 ref_block_hash | 8 expiration
 *        11 contract { 1 type | 2 parameter: Any { 1 type_url | 2 value } }
 *        14 timestamp
 * @p extra is appended to the inner contract value, to model smuggled bytes. */
static size_t build_sample_raw(uint8_t *out, uint64_t type, const char *type_url,
                               const uint8_t *owner21, const uint8_t *to21,
                               uint64_t amount, const uint8_t *extra, size_t extra_len)
{
    uint8_t value[128];
    uint8_t any[256];
    uint8_t contract[300];
    size_t  t = 0U;
    size_t  a = 0U;
    size_t  c = 0U;
    size_t  n = 0U;

    t += put_len(&value[t], TAG_OWNER, owner21, TRON_ADDR_RAW_BYTES);
    t += put_len(&value[t], TAG_TO, to21, TRON_ADDR_RAW_BYTES);
    value[t] = TAG_AMOUNT;
    t++;
    t += encode_varint(amount, &value[t]);
    if (extra_len > 0U) {
        (void)memcpy(&value[t], extra, extra_len);
        t += extra_len;
    }

    a += put_len(&any[a], 0x0AU, (const uint8_t *)type_url, strlen(type_url));
    a += put_len(&any[a], 0x12U, value, t);

    contract[c] = 0x08U;
    c++;
    c += encode_varint(type, &contract[c]);
    c += put_len(&contract[c], 0x12U, any, a);

    static const uint8_t REF_BYTES[] = { 0x02U, 0xDBU };
    static const uint8_t REF_HASH[]  = { 0xC0U, 0xC1U, 0xC2U, 0xC3U, 0xC4U, 0xC5U, 0xC6U, 0xC7U };
    n += put_len(&out[n], 0x0AU, REF_BYTES, sizeof(REF_BYTES));
    n += put_len(&out[n], 0x22U, REF_HASH, sizeof(REF_HASH));
    out[n] = 0x40U;   /* expiration */
    n++;
    n += encode_varint(1739000000000ULL, &out[n]);
    n += put_len(&out[n], 0x5AU, contract, c);
    out[n] = 0x70U;   /* timestamp */
    n++;
    n += encode_varint(1738999000000ULL, &out[n]);
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

    uint8_t raw[512];
    uint8_t extra[64];
    const uint64_t amount = 1000000ULL;   /* 1 TRX */
    size_t len = build_sample_raw(raw, CONTRACT_TYPE_TRANSFER, TRANSFER_TYPE_URL,
                                  owner, to, amount, NULL, 0U);

    /* Matching transaction is accepted. */
    assert(tron_verify_transfer(raw, len, owner, to, amount) == NULL);

    /* A node that swapped the recipient, sender, or amount is caught. */
    assert(tron_verify_transfer(raw, len, owner, other, amount) != NULL);
    assert(tron_verify_transfer(raw, len, other, to, amount) != NULL);
    assert(tron_verify_transfer(raw, len, owner, to, amount + 1U) != NULL);

    /* Truncated input must not read out of bounds or pass — except the cut
     * that drops exactly the trailing timestamp, which is a valid message. */
    const size_t valid_cut = len - 1U - encode_varint(1738999000000ULL, extra);
    for (size_t cut = 0U; cut < len; cut++) {
        if (cut != valid_cut) {
            assert(tron_verify_transfer(raw, cut, owner, to, amount) != NULL);
        }
    }

    /* A transaction paying `owner` twice must not pass as owner -> to. */
    len = build_sample_raw(raw, CONTRACT_TYPE_TRANSFER, TRANSFER_TYPE_URL,
                           owner, owner, amount, NULL, 0U);
    assert(tron_verify_transfer(raw, len, owner, to, amount) != NULL);

    /* TriggerSmartContract shares tags 1/2/3 (owner, contract, call_value):
     * same field bytes, plus `12 15 <to>` smuggled after them, is rejected. */
    size_t e = put_len(extra, TAG_TO, to, TRON_ADDR_RAW_BYTES);
    len = build_sample_raw(raw, CONTRACT_TYPE_TRIGGER, TRIGGER_TYPE_URL,
                           owner, other, amount, extra, e);
    assert(tron_verify_transfer(raw, len, owner, to, amount) != NULL);
    len = build_sample_raw(raw, CONTRACT_TYPE_TRIGGER, TRIGGER_TYPE_URL,
                           owner, to, amount, NULL, 0U);
    assert(tron_verify_transfer(raw, len, owner, to, amount) != NULL);

    /* Right contract type but wrong Any.type_url is rejected. */
    len = build_sample_raw(raw, CONTRACT_TYPE_TRANSFER, TRIGGER_TYPE_URL,
                           owner, to, amount, NULL, 0U);
    assert(tron_verify_transfer(raw, len, owner, to, amount) != NULL);

    /* Wrong real amount with the expected `18 <amount>` embedded after it. */
    e = 0U;
    extra[e] = TAG_AMOUNT;
    e++;
    e += encode_varint(amount, &extra[e]);
    len = build_sample_raw(raw, CONTRACT_TYPE_TRANSFER, TRANSFER_TYPE_URL,
                           owner, to, 999U * amount, extra, e);
    assert(tron_verify_transfer(raw, len, owner, to, amount) != NULL);

    /* An unknown field inside TransferContract is rejected. */
    extra[0] = 0x28U;   /* field 5, VARINT */
    extra[1] = 0x01U;
    len = build_sample_raw(raw, CONTRACT_TYPE_TRANSFER, TRANSFER_TYPE_URL,
                           owner, to, amount, extra, 2U);
    assert(tron_verify_transfer(raw, len, owner, to, amount) != NULL);

    /* A second contract appended to raw is rejected. */
    len = build_sample_raw(raw, CONTRACT_TYPE_TRANSFER, TRANSFER_TYPE_URL,
                           owner, to, amount, NULL, 0U);
    uint8_t second[512];
    const size_t second_len = build_sample_raw(second, CONTRACT_TYPE_TRIGGER, TRIGGER_TYPE_URL,
                                               owner, other, amount, NULL, 0U);
    (void)memcpy(&raw[len], second, second_len);
    assert(tron_verify_transfer(raw, len + second_len, owner, to, amount) != NULL);

    printf("tron_verify self-check OK\n");
    return 0;
}

#endif /* TRON_VERIFY_SELFTEST */
