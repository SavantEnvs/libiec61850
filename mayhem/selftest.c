/* Functional known-answer tests for libiec61850's MMS data codec and printer. */
#include <stdio.h>
#include <string.h>
#include "iec61850_server.h"

static int passed, failed;

#define CHECK(name, cond) do { if (cond) { passed++; printf("ok   %s\n", name); } \
                               else { failed++; printf("FAIL %s\n", name); } } while (0)

static MmsValue* decode(uint8_t* buf, int len)
{
    int end = 0;
    MmsValue* v = MmsValue_decodeMmsData(buf, 0, len, &end);
    if (v && end != len) { MmsValue_delete(v); return NULL; }
    return v;
}

/* encode `v` and compare against the expected BER bytes */
static int encodes_to(MmsValue* v, const uint8_t* exp, int explen)
{
    uint8_t out[128];
    int n = MmsValue_encodeMmsData(v, NULL, 0, false);
    if (n != explen || n > (int) sizeof(out)) return 0;
    MmsValue_encodeMmsData(v, out, 0, true);
    return memcmp(out, exp, explen) == 0;
}

int main(void)
{
    uint8_t intBuf[] = { 0x85, 0x02, 0x01, 0x2c };                      /* INTEGER 300 */
    MmsValue* v = decode(intBuf, sizeof intBuf);
    CHECK("decode integer type", v && MmsValue_getType(v) == MMS_INTEGER);
    CHECK("decode integer value == 300", v && MmsValue_toInt32(v) == 300);
    CHECK("integer re-encodes identically", v && encodes_to(v, intBuf, sizeof intBuf));
    if (v) MmsValue_delete(v);

    uint8_t negBuf[] = { 0x85, 0x01, 0xfe };                            /* INTEGER -2 */
    v = decode(negBuf, sizeof negBuf);
    CHECK("decode negative integer == -2", v && MmsValue_toInt32(v) == -2);
    if (v) MmsValue_delete(v);

    uint8_t boolBuf[] = { 0x83, 0x01, 0xff };
    v = decode(boolBuf, sizeof boolBuf);
    CHECK("decode boolean true", v && MmsValue_getType(v) == MMS_BOOLEAN && MmsValue_getBoolean(v));
    if (v) MmsValue_delete(v);

    uint8_t strBuf[] = { 0x8a, 0x03, 'a', 'b', 'c' };
    v = decode(strBuf, sizeof strBuf);
    CHECK("decode visible string == abc", v && MmsValue_getType(v) == MMS_VISIBLE_STRING
          && strcmp(MmsValue_toString(v), "abc") == 0);
    if (v) MmsValue_delete(v);

    MmsValue* s = MmsValue_newVisibleString("hello");
    const uint8_t helloBuf[] = { 0x8a, 0x05, 'h', 'e', 'l', 'l', 'o' };
    CHECK("encode visible string hello", encodes_to(s, helloBuf, sizeof helloBuf));
    MmsValue_delete(s);

    MmsValue* f = MmsValue_newFloat(1.5f);
    const uint8_t floatBuf[] = { 0x87, 0x05, 0x08, 0x3f, 0xc0, 0x00, 0x00 };
    CHECK("encode float 1.5", encodes_to(f, floatBuf, sizeof floatBuf));
    MmsValue_delete(f);

    uint8_t structBuf[] = { 0xa2, 0x09, 0x83, 0x01, 0x00, 0x85, 0x01, 0x07, 0x8a, 0x01, 'x' };
    v = decode(structBuf, sizeof structBuf);
    CHECK("decode structure with 3 elements",
          v && MmsValue_getType(v) == MMS_STRUCTURE && MmsValue_getArraySize(v) == 3);
    CHECK("structure element 1 == 7", v && MmsValue_toInt32(MmsValue_getElement(v, 1)) == 7);
    CHECK("structure re-encodes identically", v && encodes_to(v, structBuf, sizeof structBuf));
    char text[64] = { 0 };
    if (v) MmsValue_printToBuffer(v, text, sizeof text);
    CHECK("structure prints with its integer", strstr(text, "7") != NULL);
    if (v) MmsValue_delete(v);

    uint8_t truncated[] = { 0x85, 0x04, 0x01 };                         /* length exceeds buffer */
    int end = 0;
    CHECK("truncated integer is rejected", MmsValue_decodeMmsData(truncated, 0, sizeof truncated, &end) == NULL);

    printf("%d passed, %d failed\n", passed, failed);
    return failed != 0;
}
