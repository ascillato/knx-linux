#include "linux_platform.h"

#include <stdio.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <math.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include "knx/device_object.h"
#include "knx/address_table_object.h"
#include "knx/association_table_object.h"
#include "knx/group_object_table_object.h"
#include "knx/application_program_object.h"
#include "knx/ip_parameter_object.h"

LinuxPlatform::LinuxPlatform()
{
    doMemoryMapping();
}

uint32_t LinuxPlatform::currentIpAddress()
{
    return 0;
}

uint32_t LinuxPlatform::currentSubnetMask()
{
    return 0;
}

uint32_t LinuxPlatform::currentDefaultGateway()
{
    return 0;
}

uint32_t LinuxPlatform::millis()
{
    struct timespec spec;

    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec * 1000 + round(spec.tv_nsec / 1.0e6);
}

void LinuxPlatform::mdelay(uint32_t millis)
{
    struct timespec ts;
    ts.tv_sec = millis / 1000;
    ts.tv_nsec = (millis % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void LinuxPlatform::macAddress(uint8_t* data)
{
    // hardcode some address
    data[0] = 0x08;
    data[1] = 0x00;
    data[2] = 0x27;
    data[3] = 0x6c;
    data[4] = 0xa8;
    data[5] = 0x2a;
}

void LinuxPlatform::restart()
{
    // do nothing
}

void LinuxPlatform::fatalError()
{
    printf("A fatal error occured. Stopping.\n");
    while (true)
        sleep(1);
}

void LinuxPlatform::setupMultiCast(uint32_t addr, uint16_t port)
{
    _multicastAddr = addr;
    _port = port;

    struct ip_mreq command;
    uint32_t loop = 1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);

    _socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (_socketFd == -1) {
        perror("socket()");
        fatalError();
    }

    /* Mehr Prozessen erlauben, denselben Port zu nutzen */
    loop = 1;
    if (setsockopt(_socketFd, SOL_SOCKET, SO_REUSEADDR, &loop, sizeof(loop)) < 0)
    {
        perror("setsockopt:SO_REUSEADDR");
        fatalError();
    }

    if (bind(_socketFd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        perror("bind");
        fatalError();
    }

    /* Broadcast auf dieser Maschine zulassen */
    loop = 1;
    if (setsockopt(_socketFd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
    {
        perror("setsockopt:IP_MULTICAST_LOOP");
        fatalError();
    }

    /* Join the broadcast group: */
    command.imr_multiaddr.s_addr = htonl(addr);
    command.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(_socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &command, sizeof(command)) < 0)
    {
        perror("setsockopt:IP_ADD_MEMBERSHIP");
        fatalError();
    }

    uint32_t flags = fcntl(_socketFd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(_socketFd, F_SETFL, flags);
}

void LinuxPlatform::closeMultiCast()
{
    struct ip_mreq command;
    command.imr_multiaddr.s_addr = htonl(_multicastAddr);
    command.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(_socketFd,
        IPPROTO_IP,
        IP_DROP_MEMBERSHIP,
        &command, sizeof(command)) < 0) {
        perror("setsockopt:IP_DROP_MEMBERSHIP");
    }
    close(_socketFd);
}

bool LinuxPlatform::sendBytes(uint8_t* buffer, uint16_t len)
{
    struct sockaddr_in address = { 0 };
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(_multicastAddr);
    address.sin_port = htons(_port);

    ssize_t retVal = 0;
    do
    {
        retVal = sendto(_socketFd, buffer, len, 0, (struct sockaddr *) &address, sizeof(address));
        if (retVal == -1)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                return false;
        }
    } while (retVal == -1);
    return true;
}

int LinuxPlatform::readBytes(uint8_t * buffer, uint16_t maxLen)
{
    uint32_t sin_len;
    struct sockaddr_in sin;

    sin_len = sizeof(sin);
    ssize_t len = recvfrom(_socketFd, buffer, maxLen, 0, (struct sockaddr *) &sin, &sin_len);
    return len;
}

uint8_t * LinuxPlatform::getEepromBuffer(uint16_t size)
{
    return _mappedFile + 2;
}

void LinuxPlatform::commitToEeprom()
{
    fsync(_fd);
}

#define FLASHSIZE 0x10000
void LinuxPlatform::doMemoryMapping()
{
    _fd = open("flash.bin", O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
    if (_fd < 0)
    {
        perror("Error in file opening");
        //exit(-1);
    }

    struct stat st;
    uint32_t ret = fstat(_fd, &st);
    if (ret < 0)
    {
        perror("Error in fstat");
        //exit(-1);
    }

    size_t len_file = st.st_size;
    if (len_file < FLASHSIZE)
    {
        if (ftruncate(_fd, FLASHSIZE) != 0)
        {
            perror("Error extending file");
            //exit(-1);
        }
        len_file = FLASHSIZE;
    }
    unsigned char* addr = (unsigned char*)mmap(NULL, len_file, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (addr[0] != 0xAF || addr[1] != 0xFE)
    {
        memset(addr, 0, FLASHSIZE);
        addr[0] = 0xAF;
        addr[1] = 0xFE;
    }

    if (addr == MAP_FAILED)
    {
        perror("Error in mmap");
        //exit(-1);
    }
    _mappedFile = addr;
}