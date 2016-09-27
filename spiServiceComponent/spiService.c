#include "legato.h"
#include "interfaces.h"
#include "spiLibrary.h"

typedef struct
{
    int fd;
    ino_t inode;
    le_msg_SessionRef_t owningSession;
} Device_t;


static bool isDeviceOwnedByCaller(const Device_t* handle);
static Device_t const* findDeviceWithInode(ino_t inode);
static void closeAllHandlesOwnedByClient(le_msg_SessionRef_t owner);
static void clientSessionClosedHandler(le_msg_SessionRef_t clientSession, void* context);

static struct
{
    // Memory pool for allocating devices
    le_mem_PoolRef_t devicePool;
    // A map of safe references to device objects
    le_ref_MapRef_t deviceHandleRefMap;
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
    Device_t const* foundDevice = findDeviceWithInode(deviceFileStat.st_ino);
    if (foundDevice != NULL)
    {
        LE_ERROR(
            "Device file \"%s\" has already been opened by a client with id (%p)",
            devicePath,
            foundDevice->owningSession);
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

    Device_t* newDevice = le_mem_ForceAlloc(g.devicePool);
    newDevice->fd = openResult;
    newDevice->inode = deviceFileStat.st_ino;
    newDevice->owningSession = spi_GetClientSessionRef();
    *handle = le_ref_CreateRef(g.deviceHandleRefMap, newDevice);

resultKnown:
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Closes the device associated with the given handle and frees the associated resources.
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
    Device_t* device = le_ref_Lookup(g.deviceHandleRefMap, handle);
    if (device == NULL)
    {
        LE_KILL_CLIENT("Failed to lookup device from handle!");
        return;
    }

    if (!isDeviceOwnedByCaller(device))
    {
        LE_KILL_CLIENT("Cannot close handle as it is not owned by the caller");
        return;
    }

    // Remove the handle from the map so it can't be used again
    le_ref_DeleteRef(g.deviceHandleRefMap, handle);

    int closeResult = close(device->fd);
    if (closeResult != 0)
    {
        LE_WARN("Couldn't close the fd cleanly: (%m)");
    }
    le_mem_Release(device);
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
    Device_t* device = le_ref_Lookup(g.deviceHandleRefMap, handle);
    if (device == NULL)
    {
        LE_KILL_CLIENT("Failed to lookup device from handle!");
        return;
    }

    if (!isDeviceOwnedByCaller(device))
    {
        LE_KILL_CLIENT("Cannot assign handle to configure as it is not owned by the caller");
    }

    spiLib_Configure(device->fd, mode, bits, speed, msb);
}


//--------------------------------------------------------------------------------------------------
/**
 * SPI Half Duplex Write followed by Half Duplex Read
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure.
 */
//--------------------------------------------------------------------------------------------------
le_result_t spi_WriteRead_Hd
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to perform the write-read on
    const uint8_t* writeData,     ///< Tx command/address being sent to slave
    size_t writeDataLength,       ///< Number of bytes in tx message
    uint8_t* readData,            ///< Rx response from slave
    size_t* readDataLength        ///< Number of bytes in rx message
)
{
    Device_t* device = le_ref_Lookup(g.deviceHandleRefMap, handle);
    if (device == NULL)
    {
        LE_KILL_CLIENT("Failed to lookup device from handle!");
        return LE_FAULT;
    }

    if (!isDeviceOwnedByCaller(device))
    {
        LE_KILL_CLIENT("Cannot assign handle to read as it is not owned by the caller");
        return LE_FAULT;
    }

    return spiLib_WriteRead_Hd(
        device->fd,
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
le_result_t spi_Write_Hd
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to perform the write on
    const uint8_t* writeData,     ///< Command/address being sent to slave
    size_t writeDataLength        ///< Number of bytes in tx message
)
{
    Device_t* device = le_ref_Lookup(g.deviceHandleRefMap, handle);
    if (device == NULL)
    {
        LE_KILL_CLIENT("Failed to lookup device from handle!");
        return LE_FAULT;
    }

    if (!isDeviceOwnedByCaller(device))
    {
        LE_KILL_CLIENT("Cannot assign handle to write  as it is not owned by the caller");
    }

    return spiLib_Write_Hd(device->fd, writeData, writeDataLength) == LE_OK ? LE_OK : LE_FAULT;
}



//--------------------------------------------------------------------------------------------------
/**
 * Simultaneous SPI Write and  Read for full duplex communication
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure.
 */
//--------------------------------------------------------------------------------------------------
le_result_t spi_WriteRead_Fd
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to perform the write-read on
    const uint8_t* writeData,     ///< Tx command/address being sent to slave
    size_t writeDataLength,       ///< Number of bytes in tx message
    uint8_t* readData,            ///< Rx response from slave
    size_t *readDataLength        ///< Number of bytes in rx message
)
{
    Device_t* device = le_ref_Lookup(g.deviceHandleRefMap, handle);
    if (device == NULL)
    {
        LE_KILL_CLIENT("Failed to lookup device from handle!");
        return LE_FAULT;
    }

    if (!isDeviceOwnedByCaller(device))
    {
        LE_KILL_CLIENT("Cannot assign handle to read as it is not owned by the caller");
        return LE_FAULT;
    }

    if(*readDataLength < writeDataLength)
    {
        LE_KILL_CLIENT("readData length cannot be less than writeData length");
    }

    return spiLib_WriteRead_Fd(
        device->fd,
        writeData,
        readData,
        writeDataLength) == LE_OK ? LE_OK : LE_FAULT;
}

//--------------------------------------------------------------------------------------------------
/**
 * SPI Read for Half Duplex Communication
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure.
 */
//--------------------------------------------------------------------------------------------------
le_result_t spi_Read_Hd
(
    spi_DeviceHandleRef_t handle, ///< Handle for the SPI master to perform the write on
    uint8_t* readData,      ///< Command/address being sent to slave
    size_t* readDataLength        ///< Number of bytes in tx message
)
{
    Device_t* device = le_ref_Lookup(g.deviceHandleRefMap, handle);
    if (device == NULL)
    {
        LE_KILL_CLIENT("Failed to lookup device from handle!");
        return LE_FAULT;
    }

    if (!isDeviceOwnedByCaller(device))
    {
        LE_KILL_CLIENT("Cannot assign handle to write  as it is not owned by the caller");
    }

    return spiLib_Read_Hd(device->fd, readData, readDataLength) == LE_OK ? LE_OK : LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Checks if the given handle is owned by the current client.
 *
 * @return
 *      true if the handle is owned by the current client or false otherwise.
 */
//--------------------------------------------------------------------------------------------------
static bool isDeviceOwnedByCaller
(
    const Device_t* device  ///< Device to check the ownership of
)
{
    return device->owningSession == spi_GetClientSessionRef();
}

//--------------------------------------------------------------------------------------------------
/**
 * Searches for a device which contains the given inode value. It is assumed that there will be
 * either 0 or 1 device containing the given inode.
 *
 * @return
 *      Handle with the given inode or NULL if a matching handle was not found.
 */
//--------------------------------------------------------------------------------------------------
static Device_t const* findDeviceWithInode
(
    ino_t inode
)
{
    le_ref_IterRef_t it = le_ref_GetIterator(g.deviceHandleRefMap);
    while (le_ref_NextNode(it) == LE_OK)
    {
        Device_t const* device = le_ref_GetValue(it);
        LE_ASSERT(device != NULL);
        if (device->inode == inode)
        {
            return device;
        }
    }

    return NULL;
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
    le_ref_IterRef_t it = le_ref_GetIterator(g.deviceHandleRefMap);


    bool finished = le_ref_NextNode(it) != LE_OK;
    while (!finished)
    {
        Device_t const* device = le_ref_GetValue(it);
        LE_ASSERT(device != NULL);
        // In order to prevent invalidating the iterator, we store the reference of the device we
        // want to close and advance the iterator before calling spi_Close which will remove the
        // reference from the hashmap.
        spi_DeviceHandleRef_t toClose =
            (device->owningSession == owner) ? ((void*)le_ref_GetSafeRef(it)) : NULL;
        finished = le_ref_NextNode(it) != LE_OK;
        if (toClose != NULL)
        {
            spi_Close(toClose);
        }
    }
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

    g.devicePool = le_mem_CreatePool("SPI Pool", sizeof(Device_t));
    const size_t maxExpectedDevice = 8;
    g.deviceHandleRefMap = le_ref_CreateMap("SPI handles", maxExpectedDevice);

    // Register a handler to be notified when clients disconnect
    le_msg_AddServiceCloseHandler(spi_GetServiceRef(), clientSessionClosedHandler, NULL);
}
