#include<windows.h>
#include<stdio.h>
#include "../EmberhukShared.h"
#include <stdbool.h>
#include<fltUser.h>


#define MAX_EVENT_SIZE 4096
#define EVENT_BUF_SIZE 4096

#pragma comment(lib, "fltlib.lib")


void PrintUnicodeStringOffset(PUCHAR payload, USHORT offset, USHORT length) {
    if (offset == 0 || length == 0) {
        wprintf(L"NONE");

        return;
    }

    PWSTR strBuffer = (PWSTR)(payload + offset);
    USHORT wcharCount = length / sizeof(WCHAR);

    wprintf(L"%.*ls", wcharCount, strBuffer);

}

DWORD WINAPI MinifilterWorkerThread(LPVOID lpParam) {
    HANDLE hPort = INVALID_HANDLE_VALUE;
    HRESULT hr = FilterConnectCommunicationPort(EMBERHUK_PORT_NAME,
        0,NULL,0,NULL,&hPort);

    if (FAILED(hr)) {
        printf("[EMBERHUK] Failed to connect to communication port.\n");
        return 1;

    }

    printf("[EMBERHUK] Connected to communication port.\n");

    EMBERHUK_PORT_MESSAGE msg;

    while (true) {
        RtlZeroMemory(&msg, sizeof(msg));

        hr = FilterGetMessage(hPort, &msg.Header, sizeof(msg), NULL);
        if (SUCCEEDED(hr)) {
            printf("[FILE CREATE] PID: %5lu | Disp: 0x%02X | Path: %ws\n",
                msg.Event.ProcessId,
                msg.Event.CreateDisposition,
                msg.Event.FilePath);
        }
        else {
            if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)) {
                printf("[EMBERHUK] Communication port handle closed.\n");
            }
            else {
                printf("[EMBERHUK] FilterGetMessage failed.\n");
            }
            break;
        }
    }

    CloseHandle(hPort);
    return 0;

}

void ProcessRingEvents(PEMBERHUK_RING_HEADER pRing) {
    PUCHAR pBufferData = (PUCHAR)pRing + sizeof(EMBERHUK_RING_HEADER);
    ULONG64 capacity = pRing->Capacity;

    printf("[+] Listening for kernel operations...\n");

    while (1) {
        Sleep(10);

        ULONG64 head = pRing->Head;
        ULONG64 tail = pRing->Tail;

        while (head < tail) {
            ULONG64 offset = head % capacity;
            ULONG64 bytesToEnd = capacity - offset;


            EMBERHUK_EVENT_HEADER header;

            if (sizeof(EMBERHUK_EVENT_HEADER) <= bytesToEnd) {
                RtlCopyMemory(&header, pBufferData + offset, sizeof(EMBERHUK_EVENT_HEADER));
            }else{
                RtlCopyMemory(&header, pBufferData + offset, bytesToEnd);
                RtlCopyMemory((PUCHAR)&header + bytesToEnd, pBufferData, sizeof(EMBERHUK_EVENT_HEADER) - bytesToEnd);
            }

            if (header.Size == 0 || header.Size > 4096) {
                pRing->Head = pRing->Tail;
                break;
            }

            UCHAR eventBuffer[EVENT_BUF_SIZE];
            if (header.Size <= sizeof(eventBuffer)) {
                if (header.Size <= bytesToEnd) {
                    RtlCopyMemory(eventBuffer, pBufferData + offset, header.Size);

                }
                else {
                    RtlCopyMemory(eventBuffer, pBufferData + offset, bytesToEnd);

                    RtlCopyMemory(eventBuffer + bytesToEnd, pBufferData , header.Size - bytesToEnd);
                }

                switch (header.EventType) {
                    case EventTypeProcessCreate: {
                        PEVENT_PROCESS_CREATE evt = (PEVENT_PROCESS_CREATE)eventBuffer;
                        printf("[PROCESS CREATE] PID: %5lu | PPID: %5lu | Image: ", evt->ProcessId, evt->ParentProcessId);
                        PrintUnicodeStringOffset(eventBuffer, evt->ImageNameOffset, evt->ImageNameLength);
                        printf(" | Cmd: ");
                        PrintUnicodeStringOffset(eventBuffer, evt->CommandLineOffset, evt->CommandLineLength);
                        printf("\n");
                        break;
                    }
                    case EventTypeProcessExit: {
                        PEVENT_PROCESS_EXIT evt = (PEVENT_PROCESS_EXIT)eventBuffer;
                        printf("[PROCESS EXITED]   PID: %5lu\n", evt->ProcessId);
                        break;
                    }
                    case EventTypeThreadCreate: {
                        PEVENT_THREAD_CREATE evt = (PEVENT_THREAD_CREATE)eventBuffer;
                        printf("[THREAD CREATED]  PID: %5lu | TID: %5lu\n", evt->ProcessId, evt->ThreadId);
                        break;
                    }
                    case EventTypeThreadExit: {
                        PEVENT_THREAD_EXIT evt = (PEVENT_THREAD_EXIT)eventBuffer;
                        printf("[THREAD EXTTED]  PID: %5lu | TID: %5lu\n", evt->ProcessId, evt->ThreadId);
                        break;
                    }
                    case EventTypeImageLoad: {
                        PEVENT_IMAGE_LOAD evt = (PEVENT_IMAGE_LOAD)eventBuffer;
                        printf("[IMAGE LOADED]  PID: %5lu | Base: 0x%016llX | Size: 0x%08llX | Path: ",
                            evt->ProcessId, evt->ImageBase, evt->ImageSize);
                        PrintUnicodeStringOffset(eventBuffer, evt->ImagePathOffset, evt->ImagePathLength);
                        printf("\n");
                        break;
                    }

                    case EventTypeRegistryWrite: {
                        PEVENT_REGISTRY_WRITE evt = (PEVENT_REGISTRY_WRITE)eventBuffer;
                        printf("[REGISTRY WRITE] PID: %5lu | Path: ", evt->ProcessId);
                        PrintUnicodeStringOffset(eventBuffer, evt->PathOffset, evt->PathLength);
                        printf(" | Value: ");
                        PrintUnicodeStringOffset(eventBuffer, evt->ValueNameOffset, evt->ValueNameLength);
                        printf("\n");
                        break;
                    }

                    case EventTypeHandleCreate: {
                        PEVENT_HANDLE_CREATE evt = (PEVENT_HANDLE_CREATE)eventBuffer;

                        PCWSTR processName = L"NONE";

                        USHORT processNameLen = 4;

                        PCWSTR targetProcessName = L"NONE";
                        USHORT targetProcessNameLen = 4;



                        if (evt->ProcessNameOffset > 0 && evt->ProcessNameLength > 0) {
                            processName = (PCWSTR)((PUCHAR)evt + evt->ProcessNameOffset);
                            processNameLen = evt->ProcessNameLength / sizeof(WCHAR);
                        }

                        if (evt->TargetProcessNameOffset > 0 && evt->TargetProcessNameLength > 0) {
                            targetProcessName = (PCWSTR)((PUCHAR)evt + evt->TargetProcessNameOffset);
                            targetProcessNameLen = evt->TargetProcessNameLength / sizeof(WCHAR);
                        }

                        printf("[HANDLE CREATE] %.*ls (%lu) -> %.*ls (%lu) | Type: %s | GrantedAccess: 0x%08X\n",
                            processNameLen, processName,
                            evt->ProcessId,
                            targetProcessNameLen, targetProcessName,
                            evt->TargetProcessId,
                            (evt->ObjectType == 1) ? "Process" : "Thread ",
                            evt->GrantedAccess);

                        break;
                    }
                    case EventTypeNetConnect: {
                        PEVENT_NET_CONNECT evt = (PEVENT_NET_CONNECT)eventBuffer;

                        ULONG srcIp = evt->LocalAddrV4;
                        ULONG dstIp = evt->RemoteAddrV4;

                        const char* protoName = "OTHER";
                        if (evt->Protocol == 6)       protoName = "TCP";
                        else if (evt->Protocol == 17) protoName = "UDP";

                        printf("[NET CONNECT]   PID: %5lu | %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u [%s]\n",
                            evt->ProcessId,
                            (srcIp >> 24) & 0xFF, (srcIp >> 16) & 0xFF, (srcIp >> 8) & 0xFF, srcIp & 0xFF,
                            evt->LocalPort,
                            (dstIp >> 24) & 0xFF, (dstIp >> 16) & 0xFF, (dstIp >> 8) & 0xFF, dstIp & 0xFF,
                            evt->RemotePort,
                            protoName);

                        break;
                    }
                    default:
                        printf("[UNKNOWN]     Type: %u | Size: %u\n", header.EventType, header.Size);
                        break;
                    }


                
                }

            head += header.Size;
            pRing->Head = head;

            
                
                
            
            }

    }
}
   


void main() {


    HANDLE hDev = CreateFileW(L"\\\\.\\Emberhuk",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDev == INVALID_HANDLE_VALUE) {
        printf("[EMBERHUK] failed to open handle to Emberhuk device.\n");
        return;
    }
    else {
        printf("[EMBERHUK] Emberhuk handle opened success!\n");

    }

    char outputBuffer[100] = { 0 };
    ULONG bytesReturned = 0;

    BOOL ioret = DeviceIoControl(
        hDev,
        IOCTL_EMBERHUK_GET_VERSION,
        NULL,
        0,
        outputBuffer,
        sizeof(outputBuffer) - 1,
        &bytesReturned,
        NULL
    );

    if (!ioret) {
        printf("[EMBERHUK] Couldn't get Driver version.\n");
    }

    printf("[EMBERHUK] Driver Version: %s\n", outputBuffer);

    PVOID userVa = NULL;
    ioret = DeviceIoControl(hDev,
        IOCTL_EMBERHUK_MAP_RING_BUFFER,
        NULL,
        0,
        &userVa,
        sizeof(PVOID),
        &bytesReturned,
        NULL);

    if (!ioret || !userVa) {
        printf("[EMBERHUK] Failed to map the ring buffer to user space.\n");
        return;
    }

    PEMBERHUK_RING_HEADER pRing = (PEMBERHUK_RING_HEADER)userVa;

    printf("[EMBERHUK] Mapped the Ring buffer to user VA: 0x%p Capacity: %llu bytes. ", pRing, pRing->Capacity);


    HANDLE hThread = CreateThread(NULL, 0, MinifilterWorkerThread, NULL, 0, NULL);
    if (hThread == NULL) {
        printf("[EMBERHUK] failed to create Minifilter worker thread\n");
    }
    else {
        CloseHandle(hThread);
    }
    
    ProcessRingEvents(pRing);

    CloseHandle(hDev);
    return;
}


