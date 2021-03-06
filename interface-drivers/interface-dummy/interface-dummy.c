#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "mrbfs-module.h"
#include "mrbfs-pktqueue.h"

// ~85 bytes per packet, and hold 512
#define RX_PKT_BUFFER_SZ  (83 * 512)  

typedef struct
{
	UINT32 pktsReceived;
	MRBFSFileNode* file_pktCounter;
	MRBFSFileNode* file_pktLog;
	char pktLogStr[RX_PKT_BUFFER_SZ];
	MRBusPacketQueue txq;
} NodeLocalStorage;

int mrbfsInterfaceDriverVersionCheck(int ifaceVersion)
{
	if (ifaceVersion != MRBFS_INTERFACE_DRIVER_VERSION)
		return(0);
	return(1);
}


void mrbfsInterfaceDriverInit(MRBFSInterfaceDriver* mrbfsInterfaceDriver)
{
	NodeLocalStorage* nodeLocalStorage = calloc(1, sizeof(NodeLocalStorage));
	mrbfsInterfaceDriver->nodeLocalStorage = (void*)nodeLocalStorage;

	mrbusPacketQueueInitialize(&nodeLocalStorage->txq);
}


void mrbfsInterfaceDriverRun(MRBFSInterfaceDriver* mrbfsInterfaceDriver)
{
	UINT8 buffer[256];
	UINT8 *bufptr;      // Current char in buffer 
   UINT8 pktBuf[256];
   UINT8 incomingByte[2];
	const char* device="/dev/ttyUSB0";
	NodeLocalStorage* nodeLocalStorage = (NodeLocalStorage*)mrbfsInterfaceDriver->nodeLocalStorage;	
	struct termios options;
	struct timeval timeout;

	int fd = -1, nbytes=0, i=0;	

	(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_INFO, "Interface driver [%s] confirms startup", mrbfsInterfaceDriver->interfaceName);

   memset(buffer, 0, sizeof(buffer));
   bufptr = buffer;

   while(!mrbfsInterfaceDriver->terminate)
   {
      usleep(1000);
		i++;
		if (i>1000)
		{
			MRBusPacket rxPkt;
			UINT8* buffer = "P:FF1207279E53000";
			UINT8* bufptr = buffer + strlen(buffer)-1;
			UINT8* ptr = buffer+2;
			memset(&rxPkt, 0, sizeof(MRBusPacket));
			rxPkt.bus = mrbfsInterfaceDriver->bus;
			rxPkt.len = (bufptr-1 - ptr)/2;
			for(i=0; i<rxPkt.len; i++, ptr+=2)
			{
				char hexByte[3];
				hexByte[2] = 0;
				memcpy(hexByte, ptr, 2);
				rxPkt.pkt[i] = strtol(hexByte, NULL, 16);
				(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_DEBUG, "Hex set [%2.2s] became [0x%02X]", hexByte, rxPkt.pkt[i]);
			}
			(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_DEBUG, "Interface driver [%s] got packet [%s]", mrbfsInterfaceDriver->interfaceName, buffer+2);
			(*mrbfsInterfaceDriver->mrbfsPacketReceive)(&rxPkt);
			i=0;
		}
		
		
		if (!mrbusPacketQueueDepth(&nodeLocalStorage->txq) )
		{
			MRBusPacket txPkt;
			UINT8 txPktBuffer[32];
			UINT8 txPktBufferLen=0;
			mrbusPacketQueuePop(&nodeLocalStorage->txq, &txPkt);
			sprintf(txPktBuffer, ":%02X->%02X %02X", txPkt.pkt[MRBUS_PKT_SRC], txPkt.pkt[MRBUS_PKT_DEST], txPkt.pkt[MRBUS_PKT_TYPE]);
			for (i=MRBUS_PKT_DATA; i<txPkt.pkt[MRBUS_PKT_LEN]; i++)
				sprintf(txPktBuffer + strlen(txPktBuffer), " %02X", txPkt.pkt[i]);
			sprintf(txPktBuffer + strlen(txPktBuffer), ";\x0D");
			(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_INFO, "Interface driver [%s] transmitting %d bytes", mrbfsInterfaceDriver->interfaceName, strlen(txPktBuffer));
			(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_INFO, "Interface driver [%s]:  ", mrbfsInterfaceDriver->interfaceName, txPktBuffer);
		}		

	}

	(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_INFO, "Interface driver [%s] terminating", mrbfsInterfaceDriver->interfaceName);   
	phtread_exit(NULL);
}

void mrbfsInterfacePacketTransmit(MRBFSInterfaceDriver* mrbfsInterfaceDriver, MRBusPacket* txPkt)
{
	NodeLocalStorage* nodeLocalStorage = (NodeLocalStorage*)mrbfsInterfaceDriver->nodeLocalStorage;
	// This will be called from the main process, not the interface thread
	// This thing probably should just enqueue the packet and let the main loop take care of it.
	(*mrbfsInterfaceDriver->mrbfsLogMessage)(MRBFS_LOG_DEBUG, "Interface [%s] enqueuing pkt for transmit (src=%02X)", mrbfsInterfaceDriver->interfaceName, mrbfsInterfaceDriver->addr);
	mrbusPacketQueuePush(&nodeLocalStorage->txq, txPkt, mrbfsInterfaceDriver->addr);
}


