/*
*
* @author     Alex
* @co-author  Github
*
* @version    0.01.07
* @copyright  GNU General Public License
*
* @reopsitory https://github.com/
*
*/

#include "wol.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <log.h>

#define MAC_FILE "/root/maccura/app/pc_mac_file"

mac_addr_t *nextAddrFromStr(char *argument, int length)
{
    mac_addr_t *currentMacAddr = (mac_addr_t *)malloc(sizeof(mac_addr_t));

    if (currentMacAddr == NULL) {
        LOG("Cannot allocate memory: %s ...!\n", strerror(errno));
        return NULL;
    }

    if (packMacAddr(argument, currentMacAddr) < 0) {
        LOG("MAC Address ist not valid: %s ...!\n", argument);
        return NULL;
    }

    return currentMacAddr;
}

int wol_message_send(void)
{
    int sock;
    mac_addr_t *(*funcp)(char *arg, int length) = nextAddrFromFile;
    wol_header_t *currentWOLHeader = (wol_header_t *)malloc(sizeof(wol_header_t));

    strncpy(currentWOLHeader->remote_addr, REMOTE_ADDR, ADDR_LEN);

    if ((sock = startupSocket()) < 0) {
        return -1; // Log is done in startupSocket()
    }

    while ((currentWOLHeader->mac_addr = funcp(MAC_FILE, 1)) != NULL) {
        if (sendWOL(currentWOLHeader, sock) < 0) {
            LOG("Error occured during sending the WOL magic packet for mac address: %s ...!\n", currentWOLHeader->mac_addr->mac_addr_str);
        }
        free(currentWOLHeader->mac_addr);
        break;
    }

    if (currentWOLHeader) {
        free(currentWOLHeader);
    }

    close(sock);
    return 0;
}

int wol_message_send_with_mac(char *mac_str)
{
    int sock;
    wol_header_t *currentWOLHeader = (wol_header_t *)malloc(sizeof(wol_header_t));

    strncpy(currentWOLHeader->remote_addr, REMOTE_ADDR, ADDR_LEN);

    if ((sock = startupSocket()) < 0) {
        return -1; // Log is done in startupSocket()
    }

    while ((currentWOLHeader->mac_addr = nextAddrFromStr(mac_str, 1)) != NULL) {
        if (sendWOL(currentWOLHeader, sock) < 0) {
            LOG("Error occured during sending the WOL magic packet for mac address: %s ...!\n", currentWOLHeader->mac_addr->mac_addr_str);
        }
        free(currentWOLHeader->mac_addr);
        break;
    }

    if (currentWOLHeader) {
        free(currentWOLHeader);
    }

    close(sock);
    return 0;
}


mac_addr_t *nextAddrFromArg(char **argument, int length)
{
    static int i = 0;
    mac_addr_t *currentMacAddr = (mac_addr_t *)malloc(sizeof(mac_addr_t));

    if (currentMacAddr == NULL) {
        LOG("Cannot allocate memory: %s ...!\n", strerror(errno));
        return NULL;
    }

    while (i < length) {
        if (packMacAddr(argument[i], currentMacAddr) < 0) {
            LOG("MAC Address ist not valid: %s ...!\n", argument[i]);
            i++;
            continue;
        }
        i++;
        return currentMacAddr;
    }

    return NULL;
}

mac_addr_t *nextAddrFromFile(char *filenames, int length)
{
    static FILE *fp = NULL;
    mac_addr_t *currentMacAddr = (mac_addr_t *)malloc(sizeof(mac_addr_t));
    char *currentInputMacAddr = (char *)malloc(MAC_ADDR_STR_MAX * sizeof(char));

    if (currentMacAddr == NULL || currentInputMacAddr == NULL) {
        LOG("Cannot allocate memory: %s ...!\n", strerror(errno));
        return NULL;
    }

    if ((fp = fopen(filenames, "r")) == NULL) {
        LOG("Cannot open file %s: %s ...!\n", filenames, strerror(errno));
        return NULL;
    }
    LOG("Read from file %s:\n", filenames);
    
    if (fgets(currentInputMacAddr, MAC_ADDR_STR_MAX, fp) != NULL) {
        if (currentInputMacAddr[0] == '#') {
            goto end;
        }
        currentInputMacAddr[strlen(currentInputMacAddr) - 1] = '\0';
        if (packMacAddr(currentInputMacAddr, currentMacAddr) < 0) {
            LOG("MAC Address ist not valid: %s ...!\n", currentInputMacAddr);
            goto end;
        }
        fclose(fp);
        fp = NULL;
        return currentMacAddr;
    } else {
        goto end;
    }
end:
    fclose(fp);
    fp = NULL;
    return NULL;
}

int packMacAddr(const char *mac, mac_addr_t *packedMac)
{
    char *tmpMac = (char *)malloc(strlen(mac) * sizeof(char));
    char *delimiter = (char *) ":";
    char *tok;
    int i;

    if (tmpMac == NULL) {
        LOG("Cannot allocate memory for mac address: %s ...!\n", strerror(errno));
        return -1;
    }

    strncpy(tmpMac, mac, strlen(mac));
    tok = strtok(tmpMac, delimiter);

    for (i=0; i<MAC_ADDR_MAX; i++) {
        if(tok == NULL) {
            return -1;
        }
        packedMac->mac_addr[i] = (unsigned char)strtol(tok, NULL, CONVERT_BASE);
        tok = strtok(NULL, delimiter);
    }

    strncpy(packedMac->mac_addr_str, mac, MAC_ADDR_STR_MAX);

    if (tmpMac) {
        free(tmpMac);
    }

    return 0;
}

int startupSocket()
{
    int sock;
    int optval = 1;

    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        LOG("Cannot open socket: %s ...!\n", strerror(errno));
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&optval, sizeof(optval)) < 0) {
        LOG("Cannot set socket options: %s ...!\n", strerror(errno));
        return -1;
    }

    return sock;
}

int sendWOL(const wol_header_t *wol_header, const int sock)
{
    struct sockaddr_in addr;
    unsigned char packet[PACKET_BUF];
    int i, j;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(REMOTE_PORT);
    if (inet_aton(wol_header->remote_addr, &addr.sin_addr ) == 0) {
        LOG("Invalid remote ip address given: %s ...!\n", wol_header->remote_addr);
        return -1;
    }

    for (i=0; i<6; i++) {
        packet[i] = 0xFF;
    }

    for (i=1; i<=16; i++) {
        for (j=0; j<6; j++) {
            packet[i*6+j] = wol_header->mac_addr->mac_addr[j];
        }
    }

    if (sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG("Cannot send data: %s ...!\n", strerror(errno));
        return -1;
    }

    LOG("Successful sent WOL magic packet to %s ...!\n", wol_header->mac_addr->mac_addr_str);
    return 0;
}
