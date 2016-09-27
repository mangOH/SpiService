#ifndef SPI_LIBRARY_H
#define SPI_LIBRARY_H

#include "interfaces.h"
#include "legato.h"

LE_SHARED void spiLib_Configure(int fd, int mode, uint8_t bits, uint32_t speed, int msb);

LE_SHARED le_result_t spiLib_WriteReadHD(
    int fd,
    const uint8_t* writeData,
    size_t writeDataLength,
    uint8_t* readData,
    size_t* readDataLength);

LE_SHARED le_result_t spiLib_WriteHD(int fd, const uint8_t* writeData, size_t writeDataLength);

LE_SHARED le_result_t spiLib_WriteReadFD(
    int fd,
    const uint8_t* writeData,
    uint8_t* readData,
    size_t dataLength);

LE_SHARED le_result_t spiLib_ReadHD(int fd, uint8_t* readData, size_t* readDataLength);

#endif  // SPI_LIBRARY_H
