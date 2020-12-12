#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "impinj_reader_r700.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/crt_abstractions.h"

#include "azure_c_shared_utility/const_defines.h"

int ImpinjReader_TelemetryWorker(
    void* context)
{
    PNPBRIDGE_COMPONENT_HANDLE componentHandle = (PNPBRIDGE_COMPONENT_HANDLE) context;
    PIMPINJ_READER device = PnpComponentHandleGetContext(componentHandle);


    // Report telemetry every 5 seconds till we are asked to stop
    while (true)
    {
        if (device->ShuttingDown)
        {
            return IOTHUB_CLIENT_OK;
        }

        // ImpinjReader_SendTelemetryMessagesAsync(componentHandle);
        LogInfo("Telemetry Worker: Ping");  // Placeholder for actual data
        // Sleep for 5 seconds
        ThreadAPI_Sleep(5000);
    }
    return IOTHUB_CLIENT_OK;
}

IOTHUB_CLIENT_RESULT ImpinjReader_CreatePnpAdapter(
    const JSON_Object* AdapterGlobalConfig,
    PNPBRIDGE_ADAPTER_HANDLE AdapterHandle)
{
    AZURE_UNREFERENCED_PARAMETER(AdapterGlobalConfig);
    AZURE_UNREFERENCED_PARAMETER(AdapterHandle);

    curl_global_init(CURL_GLOBAL_DEFAULT);  // initialize cURL globally

    return IOTHUB_CLIENT_OK;
}

IOTHUB_CLIENT_RESULT
ImpinjReader_CreatePnpComponent(
    PNPBRIDGE_ADAPTER_HANDLE AdapterHandle,
    const char* ComponentName, 
    const JSON_Object* AdapterComponentConfig,
    PNPBRIDGE_COMPONENT_HANDLE BridgeComponentHandle)
{
    AZURE_UNREFERENCED_PARAMETER(AdapterComponentConfig);
    AZURE_UNREFERENCED_PARAMETER(AdapterHandle);
    IOTHUB_CLIENT_RESULT result = IOTHUB_CLIENT_OK;
    PIMPINJ_READER device = NULL;

    /* print component creation message */
    char compCreateStr[] = "Creating Impinj Reader component: ";
    const char* compHostname;
    compHostname = json_object_dotget_string(AdapterComponentConfig, "hostname");
    strcat(compCreateStr, ComponentName);
    strcat(compCreateStr, "\n       Hostname: ");
    strcat(compCreateStr, compHostname);

    LogInfo(compCreateStr);

    if (strlen(ComponentName) > PNP_MAXIMUM_COMPONENT_LENGTH)
    {
        LogError("ComponentName=%s is too long.  Maximum length is=%d", ComponentName, PNP_MAXIMUM_COMPONENT_LENGTH);
        BridgeComponentHandle = NULL;
        result = IOTHUB_CLIENT_ERROR;
        goto exit;
    }

    /* initialize cURL handles */
    CURL *static_handle;
    CURL *stream_handle;
    CURLM *multi_handle;
    static_handle = curl_easy_init();
    stream_handle = curl_easy_init();   

    /* initialize base HTTP strings */
    char str_http[] = "http://";
    char str_basepath[] = "/api/v1/";
    char str_endpoint_status[] = "status";
    char str_endpoint_stream[] = "data/stream";

    char str_url_always[100] = "";
    strcat(str_url_always, str_http);
    strcat(str_url_always, compHostname);
    strcat(str_url_always, str_basepath);

    char str_url_status[100] = "";
    strcat(str_url_status, str_url_always);
    strcat(str_url_status, str_endpoint_status);

    char str_url_stream[100] = "";
    strcat(str_url_stream, str_url_always);
    strcat(str_url_stream, str_endpoint_stream);

    char http_user[] = "root";
    char http_pass[] = "impinj";
    char http_login[50] = "";
    strcat(http_login, http_user);
    strcat(http_login, ":");
    strcat(http_login, http_pass);

    LogInfo("Status Endpoint: %s", str_url_status);
    LogInfo("Stream Endpoint: %s", str_url_stream);
    LogInfo("Reader Login: %s", http_login);

    curl_easy_setopt(static_handle, CURLOPT_URL, str_url_status);
    curl_easy_setopt(static_handle, CURLOPT_USERPWD, http_login);
    curl_easy_setopt(static_handle, CURLOPT_WRITEFUNCTION, DataReadCallback);

    curl_easy_setopt(stream_handle, CURLOPT_URL, str_url_stream);
    curl_easy_setopt(stream_handle, CURLOPT_USERPWD, http_login);
    curl_easy_setopt(stream_handle, CURLOPT_WRITEFUNCTION, DataReadCallback);


    device = calloc(1, sizeof(IMPINJ_READER));
    if (NULL == device) {

        LogError("Unable to allocate memory for Impinj Reader component.");
        result = IOTHUB_CLIENT_ERROR;
        goto exit;
    }

    device->SensorState = calloc(1, sizeof(IMPINJ_READER_STATE));
    if (NULL == device) {
        LogError("Unable to allocate memory for Impinj Reader component state.");
        result = IOTHUB_CLIENT_ERROR;
        goto exit;
    }

    mallocAndStrcpy_s(&device->SensorState->componentName, ComponentName);

    PnpComponentHandleSetContext(BridgeComponentHandle, device);
    // PnpComponentHandleSetPropertyUpdateCallback(BridgeComponentHandle, ImpinjReader_ProcessPropertyUpdate); // not yet implemented
    // PnpComponentHandleSetCommandCallback(BridgeComponentHandle, ImpinjReader_ProcessCommand);  // not yet implemented

exit:
    return result;
}

IOTHUB_CLIENT_RESULT ImpinjReader_StartPnpComponent(
    PNPBRIDGE_ADAPTER_HANDLE AdapterHandle,
    PNPBRIDGE_COMPONENT_HANDLE PnpComponentHandle)
{
    IOTHUB_CLIENT_RESULT result = IOTHUB_CLIENT_OK;
    AZURE_UNREFERENCED_PARAMETER(AdapterHandle);
    PIMPINJ_READER device = PnpComponentHandleGetContext(PnpComponentHandle);

    // Store client handle before starting Pnp component
    device->ClientHandle = PnpComponentHandleGetClientHandle(PnpComponentHandle);

    // Set shutdown state
    device->ShuttingDown = false;
    LogInfo("Impinj Reader: Starting Pnp Component");

    PnpComponentHandleSetContext(PnpComponentHandle, device);

    // Report Device State Async
    // result = ImpinjReader_ReportDeviceStateAsync(PnpComponentHandle, device->SensorState->componentName);

    // Create a thread to periodically publish telemetry
    if (ThreadAPI_Create(&device->WorkerHandle, ImpinjReader_TelemetryWorker, PnpComponentHandle) != THREADAPI_OK) {
        LogError("ThreadAPI_Create failed");
        return IOTHUB_CLIENT_ERROR;
    }
    return IOTHUB_CLIENT_OK;
}

IOTHUB_CLIENT_RESULT ImpinjReader_StopPnpComponent(
    PNPBRIDGE_COMPONENT_HANDLE PnpComponentHandle)
{
    PIMPINJ_READER device = PnpComponentHandleGetContext(PnpComponentHandle);

    if (device) {
        device->ShuttingDown = true;
        ThreadAPI_Join(device->WorkerHandle, NULL);
    }
    return IOTHUB_CLIENT_OK;
}

IOTHUB_CLIENT_RESULT ImpinjReader_DestroyPnpComponent(
    PNPBRIDGE_COMPONENT_HANDLE PnpComponentHandle)
{
    PIMPINJ_READER device = PnpComponentHandleGetContext(PnpComponentHandle);
    if (device != NULL)
    {
        if (device->SensorState != NULL)
        {
            if (device->SensorState->customerName != NULL)
            {
                free(device->SensorState->customerName);
            }
            free(device->SensorState);
        }
        free(device);

        PnpComponentHandleSetContext(PnpComponentHandle, NULL);
    }

    return IOTHUB_CLIENT_OK;
}


IOTHUB_CLIENT_RESULT ImpinjReader_DestroyPnpAdapter(
    PNPBRIDGE_ADAPTER_HANDLE AdapterHandle)
{
    AZURE_UNREFERENCED_PARAMETER(AdapterHandle);

    curl_global_cleanup();  // cleanup cURL globally

    return IOTHUB_CLIENT_OK;
}

PNP_ADAPTER ImpinjReaderR700 = {
    .identity = "impinj-reader-r700",
    .createAdapter = ImpinjReader_CreatePnpAdapter,
    .createPnpComponent = ImpinjReader_CreatePnpComponent,
    .startPnpComponent = ImpinjReader_StartPnpComponent,
    .stopPnpComponent = ImpinjReader_StopPnpComponent,
    .destroyPnpComponent = ImpinjReader_DestroyPnpComponent,
    .destroyAdapter = ImpinjReader_DestroyPnpAdapter
};