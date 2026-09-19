


#include <ntifs.h>
#include <wdmsec.h>
#include<initguid.h>
#include<fwpsk.h>
#include<fwpmk.h>


#include "EmberhukShared.h"
#include "EmberhukRing.h"




void EmberhukUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS EmberhukCreateClose(PDEVICE_OBJECT dev, PIRP irp);
NTSTATUS EmberhukDeviceControl(PDEVICE_OBJECT dev, PIRP irp);
void OnProcessNotify(PEPROCESS Process, HANDLE Pid, PPS_CREATE_NOTIFY_INFO Info);
void OnThreadNotify(HANDLE pid, HANDLE tid, BOOLEAN CREATE);
void OnImageLoad(PUNICODE_STRING FullImageName, HANDLE pid, PIMAGE_INFO ImageInfo);
EX_CALLBACK_FUNCTION OnRegistryNotify;
VOID OnPostOperationCallback(PVOID RegsitrationContext, POB_POST_OPERATION_INFORMATION OperationInformation);
NTSTATUS RegisterHandleMonitor(PDRIVER_OBJECT ObjectDriver);
VOID UnregisterHandleMonitor(VOID);
NTSTATUS RegisterWfpMonitor(PDRIVER_OBJECT DriverObject);
VOID UnregisterWfpMonitor(VOID);
void NTAPI WfpClassifyCallback(const FWPS_INCOMING_VALUES0* inFixedValues, const FWPS_INCOMING_METADATA_VALUES0* inMetaValues, void* layerData, 
	const FWPS_FILTER0* filter,
	UINT64 flowContext,
	FWPS_CLASSIFY_OUT0* classifyOut);
NTSTATUS NTAPI WfpNotifyCallback(FWPS_CALLOUT_NOTIFY_TYPE notifyType, const GUID* filterKey, const FWPS_FILTER0* filter);
void NTAPI WfpFlowDeleteCallback(UINT16 layerId, UINT32 calloutId, UINT64 flowContext);

static BOOLEAN processNotifyRegistered = FALSE;
static BOOLEAN threadNotifyRegistered = FALSE;
static BOOLEAN imageNotifyRegistered = FALSE;

LARGE_INTEGER g_CmCookie = { 0 };
static PVOID g_ObRegistrationHandle = NULL;

DEFINE_GUID(GUID_EMBERHUK_ALE_CALLOUT_V4,
	0x3a1e2b3c, 0x4d5e, 0x6f70, 0x81, 0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7, 0xf8);

DEFINE_GUID(GUID_EMBERHUK_SUBLAYER,
	0x1b2c3d4e, 0x5f60, 0x7182, 0x93, 0xa4, 0xb5, 0xc6, 0xd7, 0xe8, 0xf9, 0xa0);

HANDLE g_WfpEngineHandle = NULL;
UINT32 g_WfpCalloutId = 0;
UINT64 g_WfpFilterId = 0;
PDEVICE_OBJECT g_WfpDeviceObject = NULL;

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
					_In_ PUNICODE_STRING RegistryPath) {

	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = EmberhukUnload;
	
	NTSTATUS status = RingBufferInitialize(&g_RingBuffer, EMBERHUK_RING_CAPACITY);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Failed to init ring buffer: 0x%X\n", status);
		return status;
	}

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Emberhuk");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Emberhuk");
	PDEVICE_OBJECT devObj = NULL;
	//NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN,
		//0, FALSE, &devObj);

	status = IoCreateDeviceSecure(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN,
		0, FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL, NULL, &devObj);

	if (!NT_SUCCESS(status)){
		
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Failed to create device object.\n");
		return status; }

	status = IoCreateSymbolicLink(&symLink, &devName);

	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: failed to create symbolic link\n");
		IoDeleteDevice(devObj);
		return status;
	}


	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: loaded\n");


	DriverObject->MajorFunction[IRP_MJ_CREATE] = EmberhukCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = EmberhukCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = EmberhukDeviceControl;

	status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
	if (!NT_SUCCESS(status)) {
		goto error;
	}
	processNotifyRegistered = TRUE;
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Process Monitor ON.\n");

	status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
	if (!NT_SUCCESS(status)) {
		goto error;
	}
	threadNotifyRegistered = TRUE;
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Thread Monitor ON.\n");


	status = PsSetLoadImageNotifyRoutine(OnImageLoad);
	if (!NT_SUCCESS(status)) {
		goto error;
	}
	imageNotifyRegistered = TRUE;
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Image Monitor ON.\n");

	UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"360000");
	status = CmRegisterCallbackEx(OnRegistryNotify,
		&altitude,
		DriverObject,
		NULL,
		&g_CmCookie,
		NULL);

	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: failed to register registry callback.\n");
		goto error;
	}
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Registry Moinitor ON.\n");

	status = RegisterHandleMonitor(DriverObject);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Failed to register handle callback\n");
		goto error;
	}
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Handle Moinitor ON.\n");

	status = RegisterWfpMonitor(DriverObject);
	if (!NT_SUCCESS(status)) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: failed to register the WFP Monitor.\n");
		goto error;
	}

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: WFP network Moinitor ON.\n");

	return STATUS_SUCCESS;

error:

	UnregisterWfpMonitor();

	UnregisterHandleMonitor();

	if (g_CmCookie.QuadPart != 0) {
		CmUnRegisterCallback(g_CmCookie);
		g_CmCookie.QuadPart = 0;

	}


	if (imageNotifyRegistered) {
		PsRemoveLoadImageNotifyRoutine(OnImageLoad);
		imageNotifyRegistered = FALSE;
	}

	if (threadNotifyRegistered) {
		PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
		threadNotifyRegistered = FALSE;
	}

	if (processNotifyRegistered) {
		PsSetCreateProcessNotifyRoutineEx(OnProcessNotify,TRUE);

		processNotifyRegistered = FALSE;
	}
	IoDeleteSymbolicLink(&symLink);
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: symlink deleted\n");

	if (DriverObject->DeviceObject) {
		IoDeleteDevice(DriverObject->DeviceObject);
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: device object deleted\n");
	}

	RingBufferUninitialize(&g_RingBuffer);
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Ring buffer freed.\n");

	return status;
}

void EmberhukUnload(_In_ PDRIVER_OBJECT DriverObject) {

	UNREFERENCED_PARAMETER(DriverObject);

	// BOOLEAN processNotifyRegistered = FALSE;
	// BOOLEAN threadNotifyRegistered = FALSE;
	// BOOLEAN imageNotifyRegistered = FALSE;


	UnregisterHandleMonitor();

	if (g_CmCookie.QuadPart != 0) {
		CmUnRegisterCallback(g_CmCookie);
		g_CmCookie.QuadPart = 0;

	}
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Emberhuk");

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: unloaded\n");

	if (processNotifyRegistered) {
		PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
		processNotifyRegistered = FALSE;
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Process Monitor OFF.\n");
	}

	if (threadNotifyRegistered) {
		PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
		threadNotifyRegistered = FALSE;
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Thread Monitor OFF.\n");
	}

	if (imageNotifyRegistered) {
		PsRemoveLoadImageNotifyRoutine(OnImageLoad);
		imageNotifyRegistered = FALSE;
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Image Monitor OFF.\n");
	}

	IoDeleteSymbolicLink(&symLink);
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: symlink deleted\n");

	if(DriverObject->DeviceObject){
		IoDeleteDevice(DriverObject->DeviceObject);
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: device object deleted\n");
	}
	
	RingBufferUninitialize(&g_RingBuffer);
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: Ring buffer freed.\n");
	
}


NTSTATUS EmberhukCreateClose(PDEVICE_OBJECT dev, PIRP irp) {
	UNREFERENCED_PARAMETER(dev);
	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS EmberhukDeviceControl(PDEVICE_OBJECT dev, PIRP irp) {

	UNREFERENCED_PARAMETER(dev);

	PIO_STACK_LOCATION irpSp;
	NTSTATUS ntStatus = STATUS_SUCCESS;
	ULONG outBufLength;
	PCHAR inBuf, outBuf;
	const CHAR data[] = "Emberhuk driver v0.1";
	const size_t datalen = sizeof(data) - 1;
	
	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;

	irpSp = IoGetCurrentIrpStackLocation(irp);

	outBufLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

	

	switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_EMBERHUK_GET_VERSION:
	{


		DbgPrintEx(DPFLTR_IHVDRIVER_ID,
			DPFLTR_ERROR_LEVEL, "Emberhuk: called IOCTL_Emberhuk_GET_VERSION");

		inBuf = irp->AssociatedIrp.SystemBuffer;
		outBuf = irp->AssociatedIrp.SystemBuffer;

		if (outBufLength < datalen) {
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			goto End;
		}

		RtlCopyBytes(outBuf, data, datalen);

		irp->IoStatus.Information = datalen;

		break;
	}

	case IOCTL_EMBERHUK_MAP_RING_BUFFER:
	{
		if (outBufLength < sizeof(PVOID)) {
			ntStatus = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		PVOID userVa = NULL;
		ntStatus = RingBufferMapToUserSpace(&g_RingBuffer, PsGetCurrentProcess(), &userVa);
		if (NT_SUCCESS(ntStatus)) {
			
				*(PVOID*)irp->AssociatedIrp.SystemBuffer = userVa;
				irp->IoStatus.Information = sizeof(PVOID);
			
		}
		break;
	}

	default:
		ntStatus = STATUS_INVALID_DEVICE_REQUEST;
		break;

	}

End:
	irp->IoStatus.Status = ntStatus;

	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return ntStatus;
}


void OnProcessNotify(PEPROCESS Process, HANDLE Pid, PPS_CREATE_NOTIFY_INFO Info) {
	UNREFERENCED_PARAMETER(Process);

	if (Info) {

		USHORT imgLen = (Info->ImageFileName && Info->ImageFileName->Buffer) ? Info->ImageFileName->Length : 0;
		USHORT cmdLen = (Info->CommandLine && Info->CommandLine->Buffer) ? Info->CommandLine->Length : 0;
		USHORT totalSize = sizeof(EVENT_PROCESS_CREATE) + imgLen + cmdLen;

		UCHAR buffer[sizeof(EVENT_PROCESS_CREATE) + 1024];
		if (totalSize > sizeof(buffer)) return;

		RtlZeroMemory(buffer, totalSize);

		PEVENT_PROCESS_CREATE evt = (PEVENT_PROCESS_CREATE)buffer;

		// alignment forced to 1 byte to we copy timestamp by value

		LARGE_INTEGER localTime;
		KeQuerySystemTimePrecise(&localTime);
		evt->Header.Timestamp = localTime;
		evt->Header.EventType = EventTypeProcessCreate;
		evt->Header.Size = totalSize;
		evt->ProcessId = HandleToULong(Pid);
		evt->ParentProcessId = HandleToULong(Info->ParentProcessId);

		USHORT currentOffset = sizeof(EVENT_PROCESS_CREATE);

		if (imgLen > 0) {
			evt->ImageNameOffset = currentOffset;
			evt->ImageNameLength = imgLen;
			RtlCopyMemory(buffer + currentOffset, Info->ImageFileName->Buffer, imgLen);
			currentOffset += imgLen;
		}
		else {
			evt->ImageNameOffset = 0;
			evt->ImageNameLength = 0;
		}

		if (cmdLen > 0) {
			evt->CommandLineOffset = currentOffset;
			evt->CommandLineLength = cmdLen;
			RtlCopyMemory(buffer + currentOffset, Info->CommandLine->Buffer, cmdLen);
		}
		else {
			evt->CommandLineOffset = 0;
			evt->CommandLineLength = 0;
		}


		RingBufferWrite(&g_RingBuffer, evt, totalSize);
	}
	else {
		EVENT_PROCESS_EXIT evt;
		RtlZeroMemory(&evt, sizeof(evt));

		LARGE_INTEGER localTime;
		KeQuerySystemTimePrecise(&localTime);
		evt.Header.Timestamp = localTime;
		evt.Header.EventType = EventTypeProcessExit;
		evt.Header.Size = sizeof(EVENT_PROCESS_EXIT);
		evt.ProcessId = HandleToULong(Pid);

		RingBufferWrite(&g_RingBuffer, &evt, sizeof(evt));
	}
}

void OnThreadNotify(HANDLE pid, HANDLE tid, BOOLEAN CREATED) {

	LARGE_INTEGER systemTime;
	KeQuerySystemTimePrecise(&systemTime);

	if(CREATED){
		EVENT_THREAD_CREATE evt;
		RtlZeroMemory(&evt, sizeof(evt));

		evt.Header.Timestamp = systemTime;
		evt.Header.EventType = EventTypeThreadCreate;
		evt.Header.Size = sizeof(EVENT_THREAD_CREATE);
		evt.ProcessId = HandleToULong(pid);
		evt.ThreadId = HandleToULong(tid);

		RingBufferWrite(&g_RingBuffer, &evt, sizeof(evt));
	}
	else {
		EVENT_THREAD_EXIT evt;
		RtlZeroMemory(&evt, sizeof(evt));

		evt.Header.Timestamp = systemTime;
		evt.Header.EventType = EventTypeThreadExit;
		evt.Header.Size = sizeof(EVENT_THREAD_EXIT);
		evt.ProcessId = HandleToULong(pid);
		evt.ThreadId = HandleToULong(tid);
		RingBufferWrite(&g_RingBuffer, &evt, sizeof(evt));

	}

}

void OnImageLoad(PUNICODE_STRING FullImageName, HANDLE pid, PIMAGE_INFO ImageInfo) {

	if (!ImageInfo) return;

	USHORT pathLen = (FullImageName && FullImageName->Buffer) ? FullImageName->Length : 0;
	USHORT totalSize = sizeof(EVENT_IMAGE_LOAD) + pathLen;

	UCHAR buffer[sizeof(EVENT_IMAGE_LOAD) + 512];
	if (totalSize > sizeof(buffer)) return;

	RtlZeroMemory(buffer, totalSize);

	PEVENT_IMAGE_LOAD evt = (PEVENT_IMAGE_LOAD)buffer;

	LARGE_INTEGER systemTime;
	KeQuerySystemTimePrecise(&systemTime);
	evt->Header.Timestamp = systemTime;
	evt->Header.EventType = EventTypeImageLoad;
	evt->Header.Size = totalSize;

	evt->ProcessId = HandleToULong(pid);
	evt->ImageBase = (ULONG64)(ULONG_PTR)ImageInfo->ImageBase;
	evt->ImageSize = (ULONG64)ImageInfo->ImageSize;

	if (pathLen > 0) {
		evt->ImagePathOffset = sizeof(EVENT_IMAGE_LOAD);
		evt->ImagePathLength = pathLen;
		RtlCopyMemory(buffer + sizeof(EVENT_IMAGE_LOAD), FullImageName->Buffer, pathLen);

	}
	else {
		evt->ImagePathOffset = 0;
		evt->ImagePathLength = 0;
	}

	RingBufferWrite(&g_RingBuffer, evt, totalSize);

}


// the registry callback is incomplete it skips data and deals only with registry writes. This is intentional. It could be fixed later.
NTSTATUS OnRegistryNotify(PVOID CallbackContext,
	PVOID Argument1,
	PVOID Argument2) {
	UNREFERENCED_PARAMETER(CallbackContext);

	REG_NOTIFY_CLASS notifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;

	// we consider only registry writes as an example

	if (notifyClass == RegNtPostSetValueKey) {
		PREG_POST_OPERATION_INFORMATION postInfo = (PREG_POST_OPERATION_INFORMATION)Argument2;

		if (!postInfo || !NT_SUCCESS(postInfo->Status)) {
			return STATUS_SUCCESS;
		}

		PREG_SET_VALUE_KEY_INFORMATION setValInfo = (PREG_SET_VALUE_KEY_INFORMATION)postInfo->PreInformation;
		if (!setValInfo) {
			return STATUS_SUCCESS;
		}


		PCUNICODE_STRING keyPath = NULL;
		NTSTATUS status = CmCallbackGetKeyObjectIDEx(&g_CmCookie, setValInfo->Object, NULL, &keyPath, 0);

		USHORT pathLength = (NT_SUCCESS(status) && keyPath && keyPath->Buffer) ? keyPath->Length : 0;
		USHORT valLen = (setValInfo->ValueName && setValInfo->ValueName->Buffer) ? setValInfo->ValueName->Length : 0;
		USHORT totalSize = sizeof(EVENT_REGISTRY_WRITE) + pathLength + valLen;

		PEVENT_REGISTRY_WRITE evt = (PEVENT_REGISTRY_WRITE)ExAllocatePoolWithTag(NonPagedPoolNx, totalSize, 'gRmh');

		if (!evt) {
			if (NT_SUCCESS(status) && keyPath) {
				CmCallbackReleaseKeyObjectIDEx(keyPath);

			}
			return STATUS_SUCCESS;
		}

		RtlZeroMemory(evt, totalSize);

		LARGE_INTEGER systemTime;
		KeQuerySystemTimePrecise(&systemTime);

		evt->Header.Timestamp = systemTime;
		evt->Header.EventType = EventTypeRegistryWrite;
		evt->Header.Size = totalSize;
		evt->ProcessId = HandleToULong(PsGetCurrentProcessId());

		USHORT currentOffset = sizeof(EVENT_REGISTRY_WRITE);
		PUCHAR bufferBase = (PUCHAR)evt;

		if (pathLength > 0) {
			evt->PathOffset = currentOffset;
			evt->PathLength = pathLength;
			RtlCopyMemory(bufferBase + currentOffset, keyPath->Buffer, pathLength);
			currentOffset += pathLength;

		}

		if (valLen > 0) {
			evt->ValueNameLength = valLen;
			evt->ValueNameOffset = currentOffset;
			RtlCopyMemory(bufferBase + currentOffset, setValInfo->ValueName->Buffer, valLen);


		}

		RingBufferWrite(&g_RingBuffer, evt, totalSize);

		ExFreePoolWithTag(evt, 'gRmh');
		if (NT_SUCCESS(status) && keyPath) {
			CmCallbackReleaseKeyObjectIDEx(keyPath);
		}

		

	}

	return STATUS_SUCCESS;


}


NTSTATUS GetProcessImageName(PEPROCESS Process, PUNICODE_STRING* ImageName) {
	return SeLocateProcessImageName(Process, ImageName);
}



VOID OnPostOperationCallback(PVOID RegistrationContext, POB_POST_OPERATION_INFORMATION OperationInformation) {
	UNREFERENCED_PARAMETER(RegistrationContext);

	if (OperationInformation->KernelHandle == 1 || !NT_SUCCESS(OperationInformation->ReturnStatus)) {
		return;
	}

	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return;
	}

	ULONG targetPid = 0;
	USHORT objectType = 0;
	PEPROCESS targetProcess = NULL;

	if (OperationInformation->ObjectType == *PsProcessType) {
		targetProcess = (PEPROCESS)OperationInformation->Object;
		targetPid = HandleToULong(PsGetProcessId(targetProcess));
		objectType = 1;
	}
	else if (OperationInformation->ObjectType == *PsThreadType) {
		PETHREAD targetThread = (PETHREAD)OperationInformation->Object;
		targetProcess = IoThreadToProcess(targetThread);
		if (targetProcess) {
			targetPid = HandleToULong(PsGetProcessId(targetProcess));
		}
		objectType = 2;
	}
	else {
		return;
	}

	PEPROCESS currentProcess = PsGetCurrentProcess();
	//ULONG currentPid = HandleToULong(PsGetProcessId(currentProcess));

	PUNICODE_STRING currentImageName = NULL;
	PUNICODE_STRING targetImageName = NULL;

	GetProcessImageName(currentProcess, &currentImageName);
	if (targetProcess) {
		GetProcessImageName(targetProcess, &targetImageName);
	}

	USHORT currentNameLen = (currentImageName && currentImageName->Buffer) ? currentImageName->Length : 0;
	USHORT targetNameLen = (targetImageName && targetImageName->Buffer) ? targetImageName->Length : 0;

	USHORT alignedCurrentLen = (currentNameLen + 1) & ~1;
	USHORT alignedTargetLen = (targetNameLen + 1) & ~1;

	USHORT totalSize = sizeof(EVENT_HANDLE_CREATE) + alignedCurrentLen + alignedTargetLen;



	PEVENT_HANDLE_CREATE evt = (PEVENT_HANDLE_CREATE) ExAllocatePoolWithTag(NonPagedPoolNx, totalSize ,'hRmh');

	if (!evt) {
		if (currentImageName) ExFreePool(currentImageName);
		if (targetImageName) ExFreePool(targetImageName);
		return;
	}

	RtlZeroMemory(evt, totalSize);

	LARGE_INTEGER systemTime;
	KeQuerySystemTimePrecise(&systemTime);

	evt->Header.Timestamp = systemTime;
	evt->Header.EventType = EventTypeHandleCreate;
	evt->Header.Size = totalSize;

	evt->ProcessId = HandleToULong(PsGetCurrentProcessId());

	evt->TargetProcessId = targetPid;
	evt->ObjectType = objectType;

	if (OperationInformation->Operation == OB_OPERATION_HANDLE_CREATE) {
		evt->GrantedAccess = OperationInformation->Parameters->CreateHandleInformation.GrantedAccess;
	}
	else if (OperationInformation->Operation == OB_OPERATION_HANDLE_DUPLICATE) {
		evt->GrantedAccess = OperationInformation->Parameters->DuplicateHandleInformation.GrantedAccess;
	}

	USHORT currentOffset = sizeof(EVENT_HANDLE_CREATE);
	PUCHAR bufferBase = (PUCHAR)evt;

	if (currentNameLen>0) {
		evt->ProcessNameOffset = currentOffset;
		evt->ProcessNameLength = currentNameLen;
		RtlCopyMemory(bufferBase + currentOffset, currentImageName->Buffer, currentNameLen);
		currentOffset += currentNameLen;
	}


	if (targetNameLen > 0) {
		evt->TargetProcessNameOffset = currentOffset;
		evt->TargetProcessNameLength = targetNameLen;
		RtlCopyMemory(bufferBase + currentOffset, targetImageName->Buffer, targetNameLen);
	}
	
	RingBufferWrite(&g_RingBuffer, evt, totalSize);



	ExFreePoolWithTag(evt, 'hRmh');

	if (currentImageName) ExFreePool(currentImageName);
	if (targetImageName) ExFreePool(targetImageName);

}

NTSTATUS RegisterHandleMonitor(PDRIVER_OBJECT DriverObject) {

	UNREFERENCED_PARAMETER(DriverObject);
	OB_OPERATION_REGISTRATION opReg[2] = { 0 };

	// process case
	opReg[0].ObjectType = PsProcessType;
	opReg[0].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
	opReg[0].PostOperation = OnPostOperationCallback;
	

	// threads case
	opReg[1].ObjectType = PsThreadType;
	opReg[1].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
	opReg[1].PostOperation = OnPostOperationCallback;


	UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"360001");

	OB_CALLBACK_REGISTRATION cbReg = { 0 };
	cbReg.Version = OB_FLT_REGISTRATION_VERSION;
	cbReg.OperationRegistrationCount = 2;
	cbReg.RegistrationContext = NULL;
	cbReg.Altitude = altitude;
	cbReg.OperationRegistration = opReg;

	return ObRegisterCallbacks(&cbReg, &g_ObRegistrationHandle);
}

VOID UnregisterHandleMonitor(VOID) {
	if (g_ObRegistrationHandle) {
		ObUnRegisterCallbacks(g_ObRegistrationHandle);
		g_ObRegistrationHandle = NULL;
		
	}
}


NTSTATUS RegisterWfpMonitor(PDRIVER_OBJECT DriverObject) {
	NTSTATUS status;
	UNICODE_STRING wfpDevName = RTL_CONSTANT_STRING(L"\\Device\\EmberhukWfpDev");

	status = IoCreateDevice(DriverObject,
		0,
		&wfpDevName,
		FILE_DEVICE_UNKNOWN,
		0,
		FALSE,
		&g_WfpDeviceObject);

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: wfp device created\n");

	if (!NT_SUCCESS(status)) {
		return status;
	}

	FWPM_SESSION0 session = { 0 };
	session.flags = FWPM_SESSION_FLAG_DYNAMIC;

	status = FwpmEngineOpen0(NULL, RPC_C_AUTHN_DEFAULT, NULL, &session, &g_WfpEngineHandle);

	if (!NT_SUCCESS(status)) {
		goto cleanup;
	}

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: wfp FwpmEngineOpen0 called\n");

	FWPM_SUBLAYER0 sublayer = { 0 };
	sublayer.subLayerKey = GUID_EMBERHUK_SUBLAYER;
	sublayer.displayData.name = L"Emberhuk Monitor sublayer";
	sublayer.weight = 0x100;

	status = FwpmSubLayerAdd0(g_WfpEngineHandle, &sublayer, NULL);
	if (!NT_SUCCESS(status)) {
		goto cleanup;
	}

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: wfp FwpmSubLayerAdd0 called\n");

	FWPS_CALLOUT0 sCallout = { 0 };
	sCallout.calloutKey = GUID_EMBERHUK_ALE_CALLOUT_V4;
	sCallout.classifyFn = WfpClassifyCallback;
	sCallout.notifyFn = WfpNotifyCallback;
	sCallout.flowDeleteFn = WfpFlowDeleteCallback;

	status = FwpsCalloutRegister0(g_WfpDeviceObject, &sCallout, &g_WfpCalloutId);
	if (!NT_SUCCESS(status)) { 	
		goto cleanup; 
	}

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: wfp FwpsCalloutRegister0 called\n");

	FWPM_CALLOUT0 mCallout = { 0 };
	mCallout.calloutKey = GUID_EMBERHUK_ALE_CALLOUT_V4;
	mCallout.displayData.name = L"Emberhuk ALE Outbound Callout";
	mCallout.applicableLayer = FWPM_LAYER_ALE_AUTH_CONNECT_V4;

	status = FwpmCalloutAdd0(g_WfpEngineHandle, &mCallout, NULL, NULL);
	

	if (!NT_SUCCESS(status)) {
		goto cleanup;
	}
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: wfp FwpmCalloutAdd0 called\n");



	FWPM_FILTER0 filter = { 0 };
	filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
	filter.displayData.name = L"Emberhuk ALE Connect Filter";
	filter.action.type = FWP_ACTION_CALLOUT_UNKNOWN;
	filter.action.calloutKey = GUID_EMBERHUK_ALE_CALLOUT_V4;
	filter.subLayerKey = GUID_EMBERHUK_SUBLAYER;
	filter.weight.type = FWP_EMPTY;

	status = FwpmFilterAdd0(g_WfpEngineHandle, &filter, NULL, &g_WfpFilterId);
	if (!NT_SUCCESS(status)) {
		goto cleanup;
	}

	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "Emberhuk: wfp FwpmFilterAdd0 called\n");

	return STATUS_SUCCESS;

cleanup:
	UnregisterWfpMonitor();
	return status;
}


VOID UnregisterWfpMonitor(VOID) {

	if (g_WfpEngineHandle != NULL) {
		if (g_WfpFilterId != 0) {

			FwpmFilterDeleteById0(g_WfpEngineHandle, g_WfpFilterId);
			g_WfpFilterId = 0;
		}


		FwpmSubLayerDeleteByKey0(g_WfpEngineHandle, &GUID_EMBERHUK_SUBLAYER);
		FwpmEngineClose0(g_WfpEngineHandle);
		g_WfpEngineHandle = NULL;
	}

	if (g_WfpCalloutId != 0) {

		FwpsCalloutUnregisterById0(g_WfpCalloutId);
		g_WfpCalloutId = 0;
	}

	if (g_WfpDeviceObject != NULL) {
		IoDeleteDevice(g_WfpDeviceObject);
		g_WfpDeviceObject = NULL;
	}
}

NTSTATUS NTAPI WfpNotifyCallback(FWPS_CALLOUT_NOTIFY_TYPE notifyType, const GUID* filterKey, const FWPS_FILTER0* filter) {
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);
	return STATUS_SUCCESS;
}

void NTAPI WfpFlowDeleteCallback(UINT16 layerId, UINT32 calloutId, UINT64 flowContext) {
	UNREFERENCED_PARAMETER(layerId);
	UNREFERENCED_PARAMETER(calloutId);
	UNREFERENCED_PARAMETER(flowContext);
}

void NTAPI WfpClassifyCallback(const FWPS_INCOMING_VALUES0* inFixedValues, const FWPS_INCOMING_METADATA_VALUES0* inMetaValues, void* layerData,
	const FWPS_FILTER0* filter,
	UINT64 flowContext,
	FWPS_CLASSIFY_OUT0* classifyOut) {


	UNREFERENCED_PARAMETER(layerData);

	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);

	UINT32 localIp = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_LOCAL_ADDRESS].value.uint32;
	UINT16 localPort = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_LOCAL_PORT].value.uint16;
	UINT32 remoteIp = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_REMOTE_ADDRESS].value.uint32;
	UINT16 remotePort = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_REMOTE_PORT].value.uint16;
	UCHAR  protocol = (UCHAR)inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_PROTOCOL].value.uint16;


	ULONG pid = (inMetaValues->currentMetadataValues & FWPS_METADATA_FIELD_PROCESS_ID) ?
		(ULONG)inMetaValues->processId : 0;

	EVENT_NET_CONNECT evt;
	RtlZeroMemory(&evt, sizeof(evt));

	LARGE_INTEGER localTime;
	KeQuerySystemTimePrecise(&localTime);

	evt.Header.Timestamp = localTime;
	evt.Header.EventType = EventTypeNetConnect;
	evt.Header.Size = sizeof(EVENT_NET_CONNECT);

	evt.ProcessId = pid;
	evt.LocalAddrV4 = localIp;
	evt.RemoteAddrV4 = remoteIp;
	evt.LocalPort = localPort;
	evt.RemotePort = remotePort;
	evt.Protocol = protocol;

	RingBufferWrite(&g_RingBuffer, &evt, sizeof(evt));

	//DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		//"[NET CONNECT] PID: %5lu | Src: %u.%u.%u.%u:%u -> Dst: %u.%u.%u.%u:%u\n",
		//pid,
	//	(localIp >> 24) & 0xFF, (localIp >> 16) & 0xFF, (localIp >> 8) & 0xFF, localIp & 0xFF, localPort,
		//(remoteIp >> 24) & 0xFF, (remoteIp >> 16) & 0xFF, (remoteIp >> 8) & 0xFF, remoteIp & 0xFF, remotePort
	//);

	classifyOut->actionType = FWP_ACTION_PERMIT;


}

