/*
 * Client-side GoCB services, driven by an attacker-chosen object reference (the input is the
 * GoCB reference string). An unconnected IedConnection is enough: the reference is parsed
 * into the itemId buffer before any request is sent.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iec61850_client.h"

static void
getHandler(uint32_t invokeId, void* parameter, IedClientError err, ClientGooseControlBlock goCB)
{
    if (goCB)
        ClientGooseControlBlock_destroy(goCB);
}

static void
setHandler(uint32_t invokeId, void* parameter, IedClientError err)
{
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    char* ref = (char*) malloc(size + 1);
    if (ref == NULL)
        return 0;
    memcpy(ref, data, size);
    ref[size] = 0;

    IedConnection con = IedConnection_createEx(NULL, false);
    IedClientError error;

    ClientGooseControlBlock gocb = IedConnection_getGoCBValues(con, &error, ref, NULL);
    if (gocb)
        ClientGooseControlBlock_destroy(gocb);

    IedConnection_getGoCBValuesAsync(con, &error, ref, NULL, getHandler, NULL);

    ClientGooseControlBlock setGocb = ClientGooseControlBlock_create(ref);
    IedConnection_setGoCBValues(con, &error, setGocb, 0, true);
    IedConnection_setGoCBValuesAsync(con, &error, setGocb, 0, true, setHandler, NULL);
    ClientGooseControlBlock_destroy(setGocb);

    IedConnection_destroy(con);
    free(ref);
    return 0;
}
