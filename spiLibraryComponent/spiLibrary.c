#include "legato.h"
#include "spiLibrary.h"
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>


void spiLib_Configure
(
    int fd,
    int  mode,
    uint8_t bits,
    uint32_t speed,
    int msb
)
{
    LE_INFO("Running the configure library call");
     
    int8_t ret;

    LE_FATAL_IF( ((ret = ioctl(fd, SPI_IOC_WR_MODE, &mode)) < 0),
            "SPI modeset failed with error %d: %d (%m)", ret, errno);


    LE_FATAL_IF( ((ret= ioctl(fd, SPI_IOC_RD_MODE, &mode)) < 0 ),
            "SPI modeget failed with error %d: %d (%m)", ret, errno);


    LE_FATAL_IF( (( ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits)) < 0),
            "SPI bitset failed with error %d : %d (%m)", ret, errno);


    LE_FATAL_IF( (( ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits)) < 0),
            "SPI bitget failed with error %d : %d (%m)", ret, errno);


    LE_FATAL_IF( (( ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) < 0),
            "SPI speedset failed with error %d : %d (%m)", ret, errno);


    LE_FATAL_IF( (( ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed)) < 0 ),
            "SPI speedget failed with error %d : %d (%m)", ret , errno);

    LE_FATAL_IF( (( ret = ioctl(fd, SPI_IOC_WR_LSB_FIRST, &msb)) < 0 ),
            "SPI MSB/LSB write failed with error %d : %d (%m)", ret , errno);


    LE_FATAL_IF( (( ret = ioctl(fd, SPI_IOC_RD_LSB_FIRST, &msb)) < 0 ),
            "SPI MSB/LSB read failed  with error %d : %d (%m)", ret , errno);


    LE_DEBUG("mode is :%d ", mode);
    LE_DEBUG(" speed is :%d ",  speed);
    LE_DEBUG(" Bit is :%d ", bits);
    LE_DEBUG("The setup for MSB is :%d", msb);

}


/**-----------------------------------------------------------------------------------------------
 * Performs SPI WriteRead . You can send send Read command/ address of data to read. 
 * fd - file descriptor SPI port that was open
 * writeData - tx command/address being sent to slave
 * readData - rx response from slave
 * writeDataLength - no. of bytes in tx message
 * readDataLength - no. of bytes in rx message
 *
 * @return
 *      - LE_OK
 *      - LE_FAULT
 */
//--------------------------------------------------------------------------------------------------

le_result_t spiLib_WriteRead
(
    int fd,
    const uint8_t*  writeData,
    size_t writeDataLength,
    uint8_t*  readData,
    size_t* readDataLength
)
{
    int8_t ret, TransferResult ;
    le_result_t result;
    
    struct spi_ioc_transfer tr[2] ;

    memset(tr, 0, sizeof(tr));
    
    tr[0].tx_buf = (unsigned long)writeData;
    tr[0].rx_buf = (unsigned long)NULL;
    tr[0].len = writeDataLength;
    //tr[0].delay_usecs = delay_out;
    tr[0].cs_change = 0 ;
    tr[1].tx_buf = (unsigned long)NULL;
    tr[1].rx_buf = (unsigned long)readData;
    tr[1].len = *readDataLength;
    // tr[1].delay_usecs = delay_in;
     tr[1].cs_change = 0;
    
    if (true)
    {

        LE_DEBUG("\nTransmitting this message...len:%zu", writeDataLength);

        for (ret = 0; ret < writeDataLength; ret++) {
            puts("");
            LE_DEBUG("%.2X ", writeData[ret]);
        }
        puts("");

        TransferResult = ioctl(fd, SPI_IOC_MESSAGE(2), tr);

        if (TransferResult < 1)
        {
            LE_ERROR("Transfer failed with error %d : %d (%m)", TransferResult, errno);
            LE_ERROR("can't send spi message");
            result = LE_FAULT;
        }
        else {
            LE_DEBUG("Successul transmission with success %d", TransferResult);
            result = LE_OK;}

        LE_DEBUG("\nReceived message...");
        for (ret = 0; ret < *readDataLength; ret++) {
            LE_DEBUG("%.2X ", readData[ret]);
        }
        puts("");

    }

    return result;
}


//--------------------------------------------------------------------------------------------------
/**
 * Performs SPI Write . You can send send Write command/ address of data to write/data to write
 * fd - file descriptor SPI port that was open
 * writeData - command/address being sent to slave
 * writeDataLength - no. of bytes in tx message
 *
 * @return
 *      - LE_OK
 *      - LE_FAULT
 */
//--------------------------------------------------------------------------------------------------


le_result_t spiLib_Write
(
    int fd,
    const uint8_t*  writeData,
    size_t writeDataLength
)
{
    int8_t ret,  TransferResult ;
    le_result_t result;
    struct spi_ioc_transfer tr[1];
    memset(tr, 0, sizeof (tr));


    tr[0].tx_buf = (unsigned long)writeData;
    tr[0].rx_buf = (unsigned long)NULL;
    tr[0].len = writeDataLength;
//    tr[0].delay_usecs = delay_out;
    tr[0].cs_change = 0 ;


    LE_DEBUG("\nTransferring this message...len:%zu", writeDataLength);
    for (ret = 0; ret < writeDataLength; ret++) {
        puts("");
        LE_DEBUG("%.2X ", writeData[ret]);
    }
    puts("");

    TransferResult = ioctl(fd, SPI_IOC_MESSAGE(1), tr);
    if (TransferResult < 1)
    {
        LE_ERROR("Transfer failed with error %d : %d (%m)", TransferResult, errno);
        LE_ERROR("can't send spi message");
        result = LE_FAULT;

    }
    else {
        LE_DEBUG("%d", TransferResult);
        result = LE_OK;}

    return result;
}



COMPONENT_INIT
{
    LE_INFO("spiLibraryComponent initializing");
}




