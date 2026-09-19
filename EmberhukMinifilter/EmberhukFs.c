

// rundll32.exe setupapi.dll,InstallHinfSection DefaultInstall 132 .\EmberhukMinifilter.inf
// fltmc load EmberhukMinifilter
// fltmc unload EmberhukMinifilter

#include<fltKernel.h>
#include "../EmberhukShared.h"

PFLT_FILTER gFilterHandle = NULL;
PFLT_PORT gServerPort = NULL;
PFLT_PORT gClientPort = NULL;

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverUnload(FLT_REGISTRATION_FLAGS Flags);
NTSTATUS FsMonitorRegisterFilter(PDRIVER_OBJECT DriverObject);

FLT_POSTOP_CALLBACK_STATUS FsMonitorPostCreate(PFLT_CALLBACK_DATA Data,
	PCFLT_RELATED_OBJECTS FltObjects,
	PVOID CompletionContext,
	FLT_POST_OPERATION_FLAGS Flags);

NTSTATUS PortConnectionNotiftCallback(PFLT_PORT ClientPort,
	PVOID ServerPortCookie,
	PVOID ConnectionContext,
	ULONG ConnectionContextLength,
	PVOID* ConnectionCookie);

VOID PortDisconnectNotiftCallback(PVOID ConnectionCookie);

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
	{IRP_MJ_CREATE,
	0,
	NULL,
	FsMonitorPostCreate,
	},
	{IRP_MJ_OPERATION_END}
};

CONST FLT_REGISTRATION FilterRegistration = {
	sizeof(FLT_REGISTRATION),
	FLT_REGISTRATION_VERSION,
	0,
	NULL,
	Callbacks,
	DriverUnload,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

NTSTATUS PortConnectionNotiftCallback(PFLT_PORT ClientPort,
	PVOID ServerPortCookie,
	PVOID ConnectionContext,
	ULONG ConnectionContextLength,
	PVOID* ConnectionCookie) {


	UNREFERENCED_PARAMETER(ServerPortCookie);
	UNREFERENCED_PARAMETER(ConnectionContext);
	UNREFERENCED_PARAMETER(ConnectionContextLength);
	UNREFERENCED_PARAMETER(ConnectionCookie);

	gClientPort = ClientPort;

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[EMBERHUK] user mode client connected to minifilter connection port\n");

	return STATUS_SUCCESS;
}

VOID PortDisconnectNotiftCallback(PVOID ConnectionCookie) {
	UNREFERENCED_PARAMETER(ConnectionCookie);
	
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[EMBERHUK] user mode client disconnected to minifilter connection port.\n");

	if (gClientPort != NULL) {
		FltCloseClientPort(gFilterHandle, &gClientPort);
		gClientPort = NULL;
	}
}


NTSTATUS DriverUnload( FLT_REGISTRATION_FLAGS Flags ) {
	UNREFERENCED_PARAMETER(Flags);
	NTSTATUS status = STATUS_SUCCESS;

	if (gServerPort != NULL) {
		FltCloseCommunicationPort(gServerPort);
		gServerPort = NULL;
	}

	if (gFilterHandle != NULL) {
		FltUnregisterFilter(gFilterHandle);
		gFilterHandle = NULL;
	}

	return status;
}

NTSTATUS FsMonitorRegisterFilter(PDRIVER_OBJECT DriverObject) {
	NTSTATUS status;
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING portName;
	PSECURITY_DESCRIPTOR sd = NULL;



	status = FltRegisterFilter(DriverObject, &FilterRegistration, &gFilterHandle);

	if (!NT_SUCCESS(status)) {
		return status;
	}


	status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&portName, EMBERHUK_PORT_NAME);
		InitializeObjectAttributes(&oa, &portName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, sd);
		

		status = FltCreateCommunicationPort(gFilterHandle,
			&gServerPort,
			&oa,
			NULL,
			PortConnectionNotiftCallback,
			PortDisconnectNotiftCallback,
			NULL,
			1);
		FltFreeSecurityDescriptor(sd);
	}

	if (!NT_SUCCESS(status)) {
		FltUnregisterFilter(gFilterHandle);
		gFilterHandle = NULL;
		return status;
	}

	status = FltStartFiltering(gFilterHandle);
	if (!NT_SUCCESS(status)) {
		FltCloseCommunicationPort(gServerPort);
		gServerPort = NULL;
		FltUnregisterFilter(gFilterHandle);
		gFilterHandle = NULL;
	}

	return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	return FsMonitorRegisterFilter(DriverObject);
}

FLT_POSTOP_CALLBACK_STATUS FsMonitorPostCreate(PFLT_CALLBACK_DATA Data,
	PCFLT_RELATED_OBJECTS FltObjects,
	PVOID CompletionContext,
	FLT_POST_OPERATION_FLAGS Flags) {


	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	if (FlagOn(Flags, FLTFL_POST_OPERATION_DRAINING)) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (!NT_SUCCESS(Data->IoStatus.Status) || Data->IoStatus.Status == STATUS_REPARSE) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
	NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);

	if (NT_SUCCESS(status)) {
		status = FltParseFileNameInformation(nameInfo);
		if (NT_SUCCESS(status)) {
			ULONG pid = HandleToULong(PsGetThreadProcessId(Data->Thread));

			ULONG createDisposition = (Data->Iopb->Parameters.Create.Options >> 24) && 0xFF; // the create file dipsositions (open existing, etc...)

			//DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
				//"[FS CREATE] PID: %5lu | Disp: 0x%02X | Path: %wZ\n",
			//	pid, createDisposition, &nameInfo->Name);

			if (gClientPort != NULL) {
				MFILTER_EVENT_DATA notification = { 0 };
				notification.ProcessId = pid;
				notification.CreateDisposition = createDisposition;

				USHORT copyBytes = nameInfo->Name.Length < sizeof(notification.FilePath) - sizeof(WCHAR) ? nameInfo->Name.Length : sizeof(notification.FilePath) - sizeof(WCHAR);

				RtlCopyMemory(notification.FilePath, nameInfo->Name.Buffer, copyBytes);
				notification.FilePath[copyBytes / sizeof(WCHAR)] = L'\0';

				LARGE_INTEGER timeout;
				timeout.QuadPart = -1000000LL;

				FltSendMessage(gFilterHandle, &gClientPort, &notification, sizeof(notification), NULL, 0, &timeout);
			}
		}

		FltReleaseFileNameInformation(nameInfo);
	}

	return FLT_POSTOP_FINISHED_PROCESSING;
}