#include "legato.h"
#include "interfaces.h"
#include "spiLibrary.h"

typedef struct spi_DeviceHandle
{
    int fd;
    ino_t inode;
    le_msg_SessionRef_t owningSession;
    le_dls_Link_t link;
} spi_DeviceHandle_t;

static bool isHandleOwnedByCaller(const spi_DeviceHandle_t* handle);
static spi_DeviceHandle_t* findDeviceHandleWithInode(ino_t inode);
static void closeAllHandlesOwnedByClient(le_msg_SessionRef_t owner);
static void clientSessionClosedHandler(le_msg_SessionRef_t clientSession, void* context);

static struct
{
    le_dls_List_t deviceHandles;
} g;


le_result_t spi_Open(const char* deviceName, spi_DeviceHandleRef_t* handle)
{
    le_result_t result = LE_OK;

    char devicePath[256];
    const int snprintfResult = snprintf(devicePath, sizeof(devicePath), "/dev/%s", deviceName);
    if (snprintfResult > (sizeof(devicePath) - 1))
    {
        LE_ERROR("deviceName argument is too long (%s)", deviceName);
        result = LE_BAD_PARAMETER;
        goto resultKnown;
    }
    if (snprintfResult < 0)
    {
        LE_ERROR("String formatting error");
        result = LE_FAULT;
        goto resultKnown;
    }

    struct stat deviceFileStat;
    const int statResult = stat(devicePath, &deviceFileStat);
    if (statResult != 0)
    {
        if (errno == ENOENT)
        {
            result = LE_NOT_FOUND;
        }
        else if (errno == EACCES)
        {
            result = LE_NOT_PERMITTED;
        }
        else
        {
            result = LE_FAULT;
        }
        goto resultKnown;
    }
    spi_DeviceHandle_t* foundHandle =
        findDeviceHandleWithInode(deviceFileStat.st_ino);
    if (foundHandle != NULL)
    {
        LE_ERROR(
            "Device file \"%s\" has already been opened by a client with id (%p)",
            devicePath,
            foundHandle->owningSession);
        result = LE_DUPLICATE;
        goto resultKnown;
    }

    const int openResult = open(devicePath, O_RDWR);
    if (openResult < 0)
    {
        if (errno == ENOENT)
        {
            result = LE_NOT_FOUND;
        }
        else if (errno == EACCES)
        {
            result = LE_NOT_PERMITTED;
        }
        else
        {
            result = LE_FAULT;
        }
        goto resultKnown;
    }

    spi_DeviceHandle_t* newHandle = calloc(sizeof(spi_DeviceHandle_t), 1);
    LE_ASSERT(newHandle);
    newHandle->fd = openResult;
    newHandle->inode = deviceFileStat.st_ino;
    newHandle->owningSession = spi_GetClientSessionRef();
    newHandle->link = LE_DLS_LINK_INIT;
    le_dls_Stack(&g.deviceHandles, &newHandle->link);
    *handle = newHandle;

resultKnown:
    return result;
}

void spi_Close(spi_DeviceHandleRef_t handle)
{
    if (!isHandleOwnedByCaller(handle))
    {
        LE_KILL_CLIENT("Cannot close handle as it is not owned by the caller");
    }

    spi_DeviceHandle_t* foundHandle =
        findDeviceHandleWithInode(handle->inode);
    if (foundHandle == NULL)
    {
        LE_KILL_CLIENT("Could not find record of the provided handle");
    }
    else if (foundHandle != handle)
    {
        LE_KILL_CLIENT("The handle with the matching inode isn't part of the supplied handle");
    }

    le_dls_Remove(&g.deviceHandles, &handle->link);
    int closeResult = close(handle->fd);
    if (closeResult != 0)
    {
        LE_WARN("Couldn't close the fd cleanly: (%m)");
    }
    free(handle);
}

static bool isHandleOwnedByCaller(const spi_DeviceHandle_t* handle)
{
    return handle->owningSession == spi_GetClientSessionRef();
}

static spi_DeviceHandle_t* findDeviceHandleWithInode(ino_t inode)
{
    spi_DeviceHandle_t* result = NULL;

    le_dls_Link_t* link = le_dls_Peek(&g.deviceHandles);
    while (link != NULL)
    {
        spi_DeviceHandle_t* handle = CONTAINER_OF(link, spi_DeviceHandle_t, link);
        if (handle->inode == inode)
        {
            result = handle;
            break;
        }
        link = le_dls_PeekNext(&g.deviceHandles, link);
    }

    return result;
}

static void closeAllHandlesOwnedByClient(le_msg_SessionRef_t owner)
{
    bool handleFound = false;
    do
    {
        le_dls_Link_t* link = le_dls_Peek(&g.deviceHandles);
        while (link != NULL)
        {
            spi_DeviceHandle_t* handle = CONTAINER_OF(link, spi_DeviceHandle_t, link);
            if (handle->owningSession == owner)
            {
                spi_Close(handle);
                handleFound = true;
                break;
            }
            link = le_dls_PeekNext(&g.deviceHandles, link);
        }
        handleFound = false;
    } while (handleFound);
}

static void clientSessionClosedHandler(le_msg_SessionRef_t clientSession, void* context)
{
    closeAllHandlesOwnedByClient(clientSession);
}

COMPONENT_INIT
{
    LE_INFO("spiServiceComponent initializing");

    // Register a handler to be notified when clients disconnect
    le_msg_AddServiceCloseHandler(spi_GetServiceRef(), clientSessionClosedHandler, NULL);
}
