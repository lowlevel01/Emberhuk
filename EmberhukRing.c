
#include "EmberhukRing.h"
#include <ntddk.h>
#define POOL_TAG_RING 'rgEH'

EMBERHUK_RING_BUFFER g_RingBuffer = { 0 };

NTSTATUS RingBufferInitialize(PEMBERHUK_RING_BUFFER Ring, ULONG Capacity) {
	RtlZeroMemory(Ring, sizeof(EMBERHUK_RING_BUFFER));
	KeInitializeSpinLock(&Ring->ProducerLock);

	SIZE_T totalAllocationSize = sizeof(EMBERHUK_RING_HEADER) + Capacity;

	Ring->Header = (PEMBERHUK_RING_HEADER)ExAllocatePool2(POOL_FLAG_NON_PAGED,
		totalAllocationSize,
		POOL_TAG_RING);

	if (!Ring->Header) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(Ring->Header, totalAllocationSize);
	Ring->Header->Capacity = Capacity;
	Ring->BufferData = (PUCHAR)(Ring->Header + 1);

	// creating notification event 
	UNICODE_STRING eventName = RTL_CONSTANT_STRING(L"\\BaseNamedObjects\\EmberhukEvent");
	Ring->NotificationEvent = IoCreateNotificationEvent(&eventName, &Ring->EventHandle);

	if (!Ring->NotificationEvent) {
		ExFreePoolWithTag(Ring->Header, POOL_TAG_RING);
		return STATUS_UNSUCCESSFUL;
	}

	KeClearEvent(Ring->NotificationEvent);
	return STATUS_SUCCESS;

}

NTSTATUS RingBufferMapToUserSpace(PEMBERHUK_RING_BUFFER Ring, PEPROCESS TargetProcess, PVOID* UserAddress) {
	UNREFERENCED_PARAMETER(TargetProcess);
	SIZE_T totalAllocationSize = sizeof(EMBERHUK_RING_HEADER) + Ring->Header->Capacity;

	Ring->Mdl = IoAllocateMdl(Ring->Header, (ULONG)totalAllocationSize, FALSE, FALSE, NULL);

	if (!Ring->Mdl) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	MmBuildMdlForNonPagedPool(Ring->Mdl);

	__try {
		Ring->UserSpaceAddress = MmMapLockedPagesSpecifyCache(Ring->Mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority |MdlMappingNoExecute);

	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		IoFreeMdl(Ring->Mdl);
		Ring->Mdl = NULL;
		return GetExceptionCode();
	}

	*UserAddress = Ring->UserSpaceAddress;
	return STATUS_SUCCESS;
}

NTSTATUS RingBufferWrite(PEMBERHUK_RING_BUFFER Ring, PVOID EventData, USHORT DataSize) {
	KIRQL oldIrql;
	KeAcquireSpinLock(&Ring->ProducerLock, &oldIrql);

	ULONG64 head = Ring->Header->Head;
	ULONG64 tail = Ring->Header->Tail;
	ULONG64 capacity = Ring->Header->Capacity;

	ULONG64 usedSpace = tail - head;
	ULONG64 freeSpace = capacity - usedSpace;

	if (freeSpace < DataSize) {
		InterlockedIncrement((volatile LONG*)&Ring->Header->DropCount);
		KeReleaseSpinLock(&Ring->ProducerLock, oldIrql);
		return STATUS_BUFFER_TOO_SMALL; // data can't fit inside the free space.
	}

	ULONG64 tailOffset = tail % capacity;
	ULONG64 bytesToEnd = capacity - tailOffset;

	if (DataSize <= bytesToEnd) {
		// direct copy
		RtlCopyMemory(Ring->BufferData + tailOffset, EventData, DataSize);
	}
	else {
		// split because free space = free space in the end + free space in the beginning
		RtlCopyMemory(Ring->BufferData + tailOffset, EventData, bytesToEnd);
		RtlCopyMemory(Ring->BufferData, (PUCHAR)EventData + bytesToEnd, DataSize - bytesToEnd);

	}

	InterlockedExchange64((volatile LONG64*)&Ring->Header->Tail, tail + DataSize);
	KeReleaseSpinLock(&Ring->ProducerLock, oldIrql);

	KeSetEvent(Ring->NotificationEvent, IO_NO_INCREMENT, FALSE);
	return STATUS_SUCCESS;
}

VOID RingBufferUninitialize(PEMBERHUK_RING_BUFFER Ring) {
	if (Ring->Mdl && Ring->UserSpaceAddress) {
		MmUnmapLockedPages(Ring->UserSpaceAddress, Ring->Mdl);
		IoFreeMdl(Ring->Mdl);
	}

	if (Ring->EventHandle) {
		ZwClose(Ring->EventHandle);
	}

	if (Ring->Header) {
		ExFreePoolWithTag(Ring->Header, POOL_TAG_RING);
	}
}


