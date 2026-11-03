/*
 * Harness for target fuzz-mms-server-decode-mms-pdu: feeds each input as one MMS PDU to a live IedServer.
 *
 * Upstream's fuzz/fuzz_mms_server_decode_mms_pdu.c creates, starts and destroys a server per input and
 * closes the client right after sending. With the default CONFIG_MMS_SINGLE_THREADED=ON the PDU is
 * decoded on the server thread, which is usually torn down before it gets to it (no coverage, ~20 exec/s).
 * Here one server runs for the whole process and an MMS identify round trip after each PDU makes the
 * server finish decoding it first (the server handles a connection's PDUs in order).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "iec61850_server.h"
#include "iec61850_client.h"

static IedModel* model;
static IedServer iedServer;
static IedConnection con;
static int port;

static IedModel*
setupModel(void)
{
    IedModel* m = IedModel_create("testmodel");

    LogicalDevice* lDevice1 = LogicalDevice_create("SENSORS", m);

    LogicalNode* lln0 = LogicalNode_create("LLN0", lDevice1);

    CDC_ENS_create("Mod", (ModelNode*) lln0, 0);
    CDC_ENS_create("Health", (ModelNode*) lln0, 0);

    LogicalNode* ttmp1 = LogicalNode_create("TTMP1", lDevice1);
    CDC_SAV_create("TmpSv", (ModelNode*) ttmp1, 0, false);

    LogicalNode* qvtr1 = LogicalNode_create("QVTR1", lDevice1);
    CDC_SPS_create("VarStr", (ModelNode*) qvtr1, 0);
    CDC_HST_create("EvtCnt", (ModelNode*) qvtr1, 0, 10);

    DataSet* dataSet = DataSet_create("events", lln0);
    DataSetEntry_create(dataSet, "TTMP1$MX$TmpSv$instMag$f", -1, NULL);

    uint8_t rptOptions = RPT_OPT_SEQ_NUM | RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION;

    ReportControlBlock_create("events01", lln0, "events01", false, NULL, 1, TRG_OPT_DATA_CHANGED, rptOptions, 50, 0);
    ReportControlBlock_create("events02", lln0, "events02", false, NULL, 1, TRG_OPT_DATA_UPDATE | TRG_OPT_INTEGRITY, rptOptions, 50, 0);

    return m;
}

int
LLVMFuzzerInitialize(int* argc, char*** argv)
{
    (void) argc;
    (void) argv;

    /* pid-derived port so concurrent fuzzer processes in one network namespace don't share a server */
    port = 10000 + (int) (getpid() % 22000);

    model = setupModel();
    iedServer = IedServer_create(model);
    IedServer_start(iedServer, port);

    return 0;
}

static int
connectToServer(void)
{
    IedClientError error;

    con = IedConnection_create();
    IedConnection_connect(con, &error, "localhost", port);

    if (error != IED_ERROR_OK) {
        IedConnection_destroy(con);
        con = NULL;
        return -1;
    }

    return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (con == NULL && connectToServer() != 0)
        return 0;

    MmsConnection mmsCon = IedConnection_getMmsConnection(con);
    MmsError mmsError;

    MmsConnection_sendRawData(mmsCon, &mmsError, (uint8_t*) data, (int) size);

    if (mmsError == MMS_ERROR_NONE) {
        MmsServerIdentity* identity = MmsConnection_identify(mmsCon, &mmsError);

        if (identity)
            MmsServerIdentity_destroy(identity);
    }

    /* the PDU may have made the server drop the association: reconnect on the next input */
    if (IedConnection_getState(con) != IED_STATE_CONNECTED) {
        IedConnection_close(con);
        IedConnection_destroy(con);
        con = NULL;
    }

    return 0;
}
