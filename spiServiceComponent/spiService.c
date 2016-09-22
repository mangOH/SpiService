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


//--------------------------------------------------------------------------------------------------
/**
 * Opens an SPI device so that the attached device may be accessed.
 *
 * @return
 *      - LE_OK on success
 *      - LE_BAD_PARAMETER if the device name string is bad
 *      - LE_NOT_FOUND if the SPI device file could not be found
 *      - LE_NOT_PERMITTED if the SPI device file can't be opened for read/write
 *      - LE_DUPLICATE if the given device file is already opened by another spiService client
 *      - LE_FAULT for non-specific failures
 */
//--------------------------------------------------------------------------------------------------
le_result_t spi_Open
(
    const char* deviceName,        ///< [in] Name of the device file.  Do not include the "/dev/"
                                   ///  prefix.
    spi_DeviceHandleRef_t* handle  ///< [out] Handle for passing to related functions in order to
                                   ///  access the SPI device
)
{
    le_result_t result = LE_OK;
    *handle = NULL;

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
    spi_DeviceHandle_t* foundHandle = findDeviceHandleWithInode(deviceFileStat.st_ino);
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

//--------------------------------------------------------------------------------------------------
/**
 * Closes the given handle and frees the associated resources.
 *
 * @note
 *      Once a handle is closed, it is not permitted to use it for future SPI access without first
 *      calling spi_Open.
 */
//--------------------------------------------------------------------------------------------------
void spi_Close
(
    spi_DeviceHandleRef_t handle  ///< Handle to close
)
{
    if (!isHandleOwnedByCaller(handle))
    {
        LE_KILL_CLIENT("Cannot close handle as it is not owned by the caller");
    }

    spi_DeviceHandle_t* foundHandle = findDeviceHandleWithInode(handle->inode);
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

//--------------------------------------------------------------------------------------------------
/**
 * Configures an SPI device
 *
 * @note
 *      This function should be called before any of the Read/Write functions in order to ensure
 *      that the SPI bus configuration is in a known state.
 */
//--------------------------------------------------------------------------------------------------
void spi_Configure
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to configure
    int mode,                     ///<
    uint8_t bits,                 ///<
    uint32_t speed,               ///<
    int msb                       ///<
)
{
    if (!isHandleOwnedByCaller(handle))
    {
        LE_KILL_CLIENT("Cannot assign handle to configure as it is not owned by the caller");
    }
    spi_DeviceHandle_t* foundHandle = findDeviceHandleWithInode(handle->inode);
    if (foundHandle == NULL)
    {
        LE_KILL_CLIENT("Could not find record of the provided handle");
    }
    else if (foundHandle != handle)
    {
        LE_KILL_CLIENT(
            "The handle with the matching inode isn't part of the supplied handle by the "
            "configure call ");
    }

    spiLib_Configure(handle->fd, mode, bits, speed, msb);
}


//--------------------------------------------------------------------------------------------------
/**
 * SPI Read for full duplex communication
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure.
 */
//--------------------------------------------------------------------------------------------------
le_result_t spi_WriteRead
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to perform the write-read on
    const uint8_t* writeData,     ///< Tx command/address being sent to slave
    size_t writeDataLength,       ///< Number of bytes in tx message
    uint8_t* readData,            ///< Rx response from slave
    size_t* readDataLength        ///< Number of bytes in rx message
)
{
    if (!isHandleOwnedByCaller(handle))
    {
        LE_KILL_CLIENT("Cannot assign handle to read as it is not owned by the caller");
    }
    spi_DeviceHandle_t* foundHandle = findDeviceHandleWithInode(handle->inode);
    if (foundHandle == NULL)
    {
        LE_KILL_CLIENT("Could not find record of the provided handle");
    }
    else if (foundHandle != handle)
    {
        LE_KILL_CLIENT(
            "The handle with the matching inode isn't part of the supplied handle by the read "
            "call");
    }

    return spiLib_WriteRead(
        handle->fd,
        writeData,
        writeDataLength,
        readData,
        readDataLength) == LE_OK ? LE_OK : LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * SPI Write for Half Duplex Communication
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure.
 */
//--------------------------------------------------------------------------------------------------
le_result_t spi_Write
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to perform the write on
    const uint8_t* writeData,     ///< Command/address being sent to slave
    size_t writeDataLength        ///< Number of bytes in tx message
)
{
    if (!isHandleOwnedByCaller(handle))
    {
        LE_KILL_CLIENT("Cannot assign handle to write  as it is not owned by the caller");
    }
    spi_DeviceHandle_t* foundHandle = findDeviceHandleWithInode(handle->inode);
    if (foundHandle == NULL)
    {
        LE_KILL_CLIENT("Could not find record of the provided handle");
    }
    else if (foundHandle != handle)
    {
        LE_KILL_CLIENT(
            "The handle with the matching inode isn't part of the supplied handle by the write "
            "call");
    }

    return spiLib_Write(handle->fd, writeData, writeDataLength) == LE_OK ? LE_OK : LE_FAULT;
}

//--------------------------------------------------------------------------------------------------
/**
 * Checks if the given handle is owned by the current client.
 *
 * @return
 *      true if the handle is owned by the current client or false otherwise.
 */
//--------------------------------------------------------------------------------------------------
static bool isHandleOwnedByCaller
(
    const spi_DeviceHandle_t* handle  ///< Handle to check the ownership of
)
{
    return handle->owningSession == spi_GetClientSessionRef();
}

//--------------------------------------------------------------------------------------------------
/**
 * Searches within g.deviceHandles for a handle which contains the given inode value.  It is
 * assumed that there will be either 0 or 1 handle containing the given inode.
 *
 * @return
 *      Handle with the given inode or NULL if a matching handle was not found.
 */
//--------------------------------------------------------------------------------------------------
static spi_DeviceHandle_t* findDeviceHandleWithInode
(
    ino_t inode
)
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

//--------------------------------------------------------------------------------------------------
/**
 * Closes all of the handles that are owned by a specific client session.  The purpose of this
 * function is to free resources on the server side when it is detected that a client has
 * disconnected.
 */
//--------------------------------------------------------------------------------------------------
static void closeAllHandlesOwnedByClient
(
    le_msg_SessionRef_t owner
)
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

//--------------------------------------------------------------------------------------------------
/**
 * A handler for client disconnects which frees all resources associated with the client.
 */
//--------------------------------------------------------------------------------------------------
static void clientSessionClosedHandler
(
    le_msg_SessionRef_t clientSession,
    void* context
)
{
    closeAllHandlesOwnedByClient(clientSession);
}

COMPONENT_INIT
{
    LE_INFO("spiServiceComponent initializing");

    g.deviceHandles = LE_DLS_LIST_INIT;

    // Register a handler to be notified when clients disconnect
    le_msg_AddServiceCloseHandler(spi_GetServiceRef(), clientSessionClosedHandler, NULL);
}
