#pragma once

#include "knx/platform.h"

class LinuxPlatform: public Platform
{
public:
    LinuxPlatform();

    // ip stuff
    uint32_t currentIpAddress();
    uint32_t currentSubnetMask();
    uint32_t currentDefaultGateway();
    void macAddress(uint8_t* addr);

    // basic stuff
    uint32_t millis();
    void mdelay(uint32_t millis);
    void restart();
    void fatalError();

    //multicast
    void setupMultiCast(uint32_t addr, uint16_t port);
    void closeMultiCast();
    bool sendBytes(uint8_t* buffer, uint16_t len);
    int readBytes(uint8_t* buffer, uint16_t maxLen);

    //memory
    uint8_t* getEepromBuffer(uint16_t size);
    void commitToEeprom();
private:
    uint32_t _multicastAddr;
    uint16_t _port;
    int _socketFd = -1;
    void doMemoryMapping();
    uint8_t* _mappedFile;
    int _fd;
};