#ifndef EMBERHUK_SHARED_H
#define EMBERHUK_SHARED_H

#ifdef _KERNEL_MODE
#include <fltKernel.h>
#else
#include <windows.h>
#include <fltUser.h>
#endif

// Control codes
#define EMBERHUK_DEVICE 0x8000
#define IOCTL_EMBERHUK_GET_VERSION CTL_CODE(EMBERHUK_DEVICE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_EMBERHUK_MAP_RING_BUFFER CTL_CODE(EMBERHUK_DEVICE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// ring buffer information

#define EMBERHUK_RING_CAPACITY (1024* 1024)

#define EMBERHUK_PORT_NAME L"\\EmberhukPort"


typedef enum _EMBERHUK_EVENT_TYPE {
	EventTypeProcessCreate = 1,
	EventTypeProcessExit,
	EventTypeThreadCreate,
	EventTypeThreadExit,
	EventTypeImageLoad,
	EventTypeRegistryWrite,
	EventTypeHandleCreate,
	EventTypeNetConnect,

} EMBERHUK_EVENT_TYPE;


#pragma pack(push, 1)

typedef struct _EMBERHUK_EVENT_HEADER {
	USHORT EventType;
	ULONG Size;
	LARGE_INTEGER Timestamp;

 } EMBERHUK_EVENT_HEADER, *PEMBERHUK_EVENT_HEADER;

typedef struct _EMBERHUK_RING_HEADER {
	volatile ULONG64 Head;
	volatile ULONG64 Tail;
	ULONG64 Capacity;
	ULONG DropCount;
}EMBERHUK_RING_HEADER, * PEMBERHUK_RING_HEADER;



typedef struct _EVENT_PROCESS_CREATE {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT ImageNameOffset;
	USHORT ImageNameLength;
	USHORT CommandLineOffset;
	USHORT CommandLineLength;
} EVENT_PROCESS_CREATE, *PEVENT_PROCESS_CREATE;

typedef struct _EVENT_PROCESS_EXIT {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
} EVENT_PROCESS_EXIT, * PEVENT_PROCESS_EXIT;

typedef struct _EVENT_THREAD_CREATE {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	ULONG ThreadId;
} EVENT_THREAD_CREATE, * PEVENT_THREAD_CREATE;

typedef struct _EVENT_THREAD_EXIT {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	ULONG ThreadId;
} EVENT_THREAD_EXIT, *PEVENT_THREAD_EXIT;

typedef struct _EVENT_IMAGE_LOAD {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	ULONG64 ImageBase;
	ULONG64 ImageSize;
	USHORT ImagePathOffset;
	USHORT ImagePathLength;
} EVENT_IMAGE_LOAD, * PEVENT_IMAGE_LOAD;

typedef struct _EVENT_REGISTRY_WRITE {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	USHORT PathOffset;
	USHORT PathLength;
	USHORT ValueNameOffset;
	USHORT ValueNameLength;
} EVENT_REGISTRY_WRITE, *PEVENT_REGISTRY_WRITE;


typedef struct _EVENT_HANDLE_CREATE {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	ULONG TargetProcessId;
	//ULONG DesiredAccess; desired access is only in the pre callback
	ULONG GrantedAccess;
	USHORT ObjectType;
	USHORT ProcessNameOffset;
	USHORT ProcessNameLength;
	USHORT TargetProcessNameOffset;
	USHORT TargetProcessNameLength;
}EVENT_HANDLE_CREATE, *PEVENT_HANDLE_CREATE;


// for the filter driver, no header because the filter driver has a single event

typedef struct _MFILTER_EVENT_DATA {
	ULONG ProcessId;
	WCHAR FilePath[260];
	ULONG CreateDisposition;
} MFILTER_EVENT_DATA, * PMFILTER_EVENT_DATA;

typedef struct _EMBERHUK_PORT_MESSAGE {
	FILTER_MESSAGE_HEADER Header;
	MFILTER_EVENT_DATA Event;
} EMBERHUK_PORT_MESSAGE, * PEMBERHUK_PORT_MESSAGE;


typedef struct _EVENT_NET_CONNECT {
	EMBERHUK_EVENT_HEADER Header;
	ULONG ProcessId;
	ULONG LocalAddrV4;  
	ULONG RemoteAddrV4;
	USHORT LocalPort;
	USHORT RemotePort;
	UCHAR Protocol; 
} EVENT_NET_CONNECT, * PEVENT_NET_CONNECT;

#pragma pack(pop)

#endif