#pragma once


#include "EmberhukShared.h"

typedef struct _EMBERHUK_RING_BUFFER {
	PEMBERHUK_RING_HEADER Header;
	PUCHAR BufferData;
	PMDL Mdl;
	PVOID UserSpaceAddress;
	PKEVENT NotificationEvent;
	HANDLE EventHandle;
	KSPIN_LOCK ProducerLock;
} EMBERHUK_RING_BUFFER, *PEMBERHUK_RING_BUFFER;

NTSTATUS RingBufferInitialize(PEMBERHUK_RING_BUFFER Ring, ULONG Capacity);
VOID RingBufferUninitialize(PEMBERHUK_RING_BUFFER Ring);
NTSTATUS RingBufferMapToUserSpace(PEMBERHUK_RING_BUFFER Ring, PEPROCESS TargetProcess, PVOID* UserAdress);
NTSTATUS RingBufferWrite(PEMBERHUK_RING_BUFFER Ring, PVOID EventData, USHORT DataSize);

extern EMBERHUK_RING_BUFFER g_RingBuffer;