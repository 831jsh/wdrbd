﻿
#include "drbd_windows.h"
#include "wsk2.h"

extern bool drbd_stream_send_timed_out(struct drbd_transport *transport, enum drbd_stream stream);

WSK_REGISTRATION			g_WskRegistration;
static WSK_PROVIDER_NPI		g_WskProvider;
static WSK_CLIENT_DISPATCH	g_WskDispatch = { MAKE_WSK_VERSION(1, 0), 0, NULL };
LONG						g_SocketsState = DEINITIALIZED;

//#define WSK_ASYNCCOMPL	1

NTSTATUS
NTAPI CompletionRoutine(
	__in PDEVICE_OBJECT	DeviceObject,
	__in PIRP			Irp,
	__in PKEVENT		CompletionEvent
)
{
	ASSERT(CompletionEvent);
	
	KeSetEvent(CompletionEvent, IO_NO_INCREMENT, FALSE);

	return STATUS_MORE_PROCESSING_REQUIRED;
}

#ifdef _WIN32_NOWAIT_CLOSESOCKET
NTSTATUS
NTAPI CloseCompletionRoutine(
	__in PDEVICE_OBJECT	DeviceObject,
	__in PIRP			Irp,
	__in PVOID		Context
)
{
	IoFreeIrp(Irp);
	return STATUS_MORE_PROCESSING_REQUIRED;
}
#endif

#if WSK_ASYNCCOMPL
NTSTATUS
NTAPI CompletionRoutineAsync(
	__in PDEVICE_OBJECT	DeviceObject,
	__in PIRP			Irp,
	__in PVOID			Context
)
{
	if (Irp->IoStatus.Status == STATUS_SUCCESS) {
		// Get the pointer to the socket context
		// Perform any cleanup and/or deallocation of the socket context
	} else { // Error status
		// Handle error
	}
	// Free the IRP
	IoFreeIrp(Irp);

	return STATUS_MORE_PROCESSING_REQUIRED;
}
#endif

#ifdef _WIN32_CANCEL_ROUTINE
// DW-1398: Implementing a cancel routine.
VOID CancelRoutine(
	IN PDEVICE_OBJECT pDeviceObject,
	IN PIRP Irp)
{
	IoReleaseCancelSpinLock(Irp->CancelIrql);
	Irp->IoStatus.Status = STATUS_CANCELLED;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
}
#endif

NTSTATUS
InitWskData(
	__out PIRP*		pIrp,
	__out PKEVENT	CompletionEvent,
	__in  BOOLEAN	bRawIrp
)
{
#ifdef _WIN32_CANCEL_ROUTINE
	KIRQL irql;
#endif

	ASSERT(pIrp);
	ASSERT(CompletionEvent);

	// DW-1316 use raw irp.
	if (bRawIrp) {
		*pIrp = ExAllocatePoolWithTag(NonPagedPool, IoSizeOfIrp(1), 'FFDW');
		if (!*pIrp) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		IoInitializeIrp(*pIrp, IoSizeOfIrp(1), 1);
	}
	else {
		*pIrp = IoAllocateIrp(1, FALSE);
	}

	if (!*pIrp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	KeInitializeEvent(CompletionEvent, SynchronizationEvent, FALSE);
	IoSetCompletionRoutine(*pIrp, CompletionRoutine, CompletionEvent, TRUE, TRUE, TRUE);

#ifdef _WIN32_CANCEL_ROUTINE
	// DW-1398: set cancel routine
	IoAcquireCancelSpinLock(&irql);
	IoSetCancelRoutine(*pIrp, CancelRoutine);
	IoReleaseCancelSpinLock(irql);
#endif

	return STATUS_SUCCESS;
}

#ifdef _WIN32_NOWAIT_CLOSESOCKET
NTSTATUS
InitWskCloseData(
	__out PIRP*		pIrp,
	__in  BOOLEAN	bRawIrp
)
{
	ASSERT(pIrp);

	// DW-1316 use raw irp.
	if (bRawIrp) {
		*pIrp = ExAllocatePoolWithTag(NonPagedPool, IoSizeOfIrp(1), 'FFDW');
		if (!*pIrp) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		IoInitializeIrp(*pIrp, IoSizeOfIrp(1), 1);
	}
	else {
		*pIrp = IoAllocateIrp(1, FALSE);
	}

	if (!*pIrp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	IoSetCompletionRoutine(*pIrp, CloseCompletionRoutine, NULL, TRUE, TRUE, TRUE);

	return STATUS_SUCCESS;
}
#endif


#if WSK_ASYNCCOMPL
NTSTATUS
InitWskDataAsync(
	__out PIRP*		pIrp,
	__in  BOOLEAN	bRawIrp
	)
{
#ifdef _WIN32_CANCEL_ROUTINE
	KIRQL irql;
#endif

	ASSERT(pIrp);
	ASSERT(CompletionEvent);

	if (bRawIrp) {
		*pIrp = ExAllocatePoolWithTag(NonPagedPool, IoSizeOfIrp(1), 'FFDW');
		if (!*pIrp) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		IoInitializeIrp(*pIrp, IoSizeOfIrp(1), 1);
	}
	else {
		*pIrp = IoAllocateIrp(1, FALSE);
	}

	if (!*pIrp) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	//KeInitializeEvent(CompletionEvent, SynchronizationEvent, FALSE);
	IoSetCompletionRoutine(*pIrp, CompletionRoutineAsync, NULL, TRUE, TRUE, TRUE);

#ifdef _WIN32_CANCEL_ROUTINE
	// DW-1398: set cancel routine
	IoAcquireCancelSpinLock(&irql);
	IoSetCancelRoutine(*pIrp, CancelRoutine);
	IoReleaseCancelSpinLock(irql);
#endif

	return STATUS_SUCCESS;
}
#endif

VOID
ReInitWskData(
__out PIRP*		pIrp,
__out PKEVENT	CompletionEvent
)
{
#ifdef _WIN32_CANCEL_ROUTINE
	KIRQL irql;
#endif

	ASSERT(pIrp);
	ASSERT(CompletionEvent);

	KeResetEvent(CompletionEvent);
	IoReuseIrp(*pIrp, STATUS_UNSUCCESSFUL);
	IoSetCompletionRoutine(*pIrp, CompletionRoutine, CompletionEvent, TRUE, TRUE, TRUE);

#ifdef _WIN32_CANCEL_ROUTINE
	// DW-1398: set cancel routine
	IoAcquireCancelSpinLock(&irql);
	IoSetCancelRoutine(*pIrp, CancelRoutine);
	IoReleaseCancelSpinLock(irql);
#endif

	return;
}

NTSTATUS
InitWskBuffer(
	__in  PVOID		Buffer,
	__in  ULONG		BufferSize,
	__out PWSK_BUF	WskBuffer,
	__in  BOOLEAN	bWriteAccess
)
{
	NTSTATUS Status = STATUS_SUCCESS;

	ASSERT(Buffer);
	ASSERT(BufferSize);
	ASSERT(WskBuffer);

	WskBuffer->Offset = 0;
	WskBuffer->Length = BufferSize;

	WskBuffer->Mdl = IoAllocateMdl(Buffer, BufferSize, FALSE, FALSE, NULL);
	if (!WskBuffer->Mdl) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

    try {
		// DW-1223: Locking with 'IoWriteAccess' affects buffer, which causes infinite I/O from ntfs when the buffer is from mdl of write IRP.
		// we need write access for receiver, since buffer will be filled.
		
		MmProbeAndLockPages(WskBuffer->Mdl, KernelMode, bWriteAccess?IoWriteAccess:IoReadAccess);
    } except(EXCEPTION_EXECUTE_HANDLER) {
        if (WskBuffer->Mdl != NULL) {
            IoFreeMdl(WskBuffer->Mdl);
        }
        WDRBD_ERROR("MmProbeAndLockPages failed. exception code=0x%x\n", GetExceptionCode());
        return STATUS_INSUFFICIENT_RESOURCES;
    }
	return Status;
}

VOID
FreeWskBuffer(
__in PWSK_BUF WskBuffer
)
{
	ASSERT(WskBuffer);

	MmUnlockPages(WskBuffer->Mdl);
	IoFreeMdl(WskBuffer->Mdl);
}

VOID
FreeWskData(
__in PIRP pIrp
)
{
	if (pIrp)
		IoFreeIrp(pIrp);
}

//
// Library initialization routine
//

NTSTATUS NTAPI SocketsInit()
{
	WSK_CLIENT_NPI	WskClient = { 0 };
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;

	if (InterlockedCompareExchange(&g_SocketsState, INITIALIZING, DEINITIALIZED) != DEINITIALIZED)
		return STATUS_ALREADY_REGISTERED;

	WskClient.ClientContext = NULL;
	WskClient.Dispatch = &g_WskDispatch;

	Status = WskRegister(&WskClient, &g_WskRegistration);
	if (!NT_SUCCESS(Status)) {
		InterlockedExchange(&g_SocketsState, DEINITIALIZED);
		return Status;
	}

	WDRBD_INFO("WskCaptureProviderNPI start.\n");
	Status = WskCaptureProviderNPI(&g_WskRegistration, WSK_INFINITE_WAIT, &g_WskProvider);
	WDRBD_INFO("WskCaptureProviderNPI done.\n"); // takes long time! msg out after MVL loaded.

	if (!NT_SUCCESS(Status)) {
		WDRBD_ERROR("WskCaptureProviderNPI() failed with status 0x%08X\n", Status);
		WskDeregister(&g_WskRegistration);
		InterlockedExchange(&g_SocketsState, DEINITIALIZED);
		return Status;
	}

	InterlockedExchange(&g_SocketsState, INITIALIZED);
	return STATUS_SUCCESS;
}

//
// Library deinitialization routine
//

VOID NTAPI SocketsDeinit()
{
	if (InterlockedCompareExchange(&g_SocketsState, INITIALIZED, DEINITIALIZING) != INITIALIZED)
		return;
	WskReleaseProviderNPI(&g_WskRegistration);
	WskDeregister(&g_WskRegistration);

	InterlockedExchange(&g_SocketsState, DEINITIALIZED);
}

PWSK_SOCKET
NTAPI
CreateSocket(
	__in ADDRESS_FAMILY	AddressFamily,
	__in USHORT			SocketType,
	__in ULONG			Protocol,
    __in PVOID          *SocketContext,
    __in PWSK_CLIENT_LISTEN_DISPATCH Dispatch,
	__in ULONG			Flags
)
{
	KEVENT			CompletionEvent = { 0 };
	PIRP			Irp = NULL;
	PWSK_SOCKET		WskSocket = NULL;
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED)
	{
		return NULL;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return NULL;
	}

	Status = g_WskProvider.Dispatch->WskSocket(
				g_WskProvider.Client,
				AddressFamily,
				SocketType,
				Protocol,
				Flags,
				SocketContext,
				Dispatch,
				NULL,
				NULL,
				NULL,
				Irp);

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	WskSocket = NT_SUCCESS(Status) ? (PWSK_SOCKET) Irp->IoStatus.Information : NULL;
	IoFreeIrp(Irp);

	return (PWSK_SOCKET) WskSocket;
}

NTSTATUS
NTAPI
CloseSocketLocal(
	__in PWSK_SOCKET WskSocket
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket)
		return STATUS_INVALID_PARAMETER;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	if(gbShutdown) {
		IoFreeIrp(Irp);
		return STATUS_UNSUCCESSFUL;
	}
	Status = ((PWSK_PROVIDER_BASIC_DISPATCH) WskSocket->Dispatch)->WskCloseSocket(WskSocket, Irp);
	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}
	IoFreeIrp(Irp);
	return Status;
}


#ifdef _WIN32_NOWAIT_CLOSESOCKET
NTSTATUS
NTAPI
CloseSocket(
	__in PWSK_SOCKET WskSocket
)
{
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket){
		return STATUS_INVALID_PARAMETER;
	}

	Status = InitWskCloseData(&Irp, TRUE);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}
	Status = ((PWSK_PROVIDER_BASIC_DISPATCH) WskSocket->Dispatch)->WskCloseSocket(WskSocket, Irp);

	return STATUS_SUCCESS;
}
#else
NTSTATUS
NTAPI
CloseSocket(
	__in PWSK_SOCKET WskSocket
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;
	LARGE_INTEGER	nWaitTime;
	nWaitTime.QuadPart = (-1 * 1000 * 10000);   // wait 1000ms relative 

	if (g_SocketsState != INITIALIZED || !WskSocket){
		return STATUS_INVALID_PARAMETER;
	}
#if WSK_ASYNCCOMPL
	Status = InitWskDataAsync(&Irp, TRUE);
#else
	Status = InitWskData(&Irp, &CompletionEvent, TRUE);
#endif
	if (!NT_SUCCESS(Status)) {
		return Status;
	}
	Status = ((PWSK_PROVIDER_BASIC_DISPATCH) WskSocket->Dispatch)->WskCloseSocket(WskSocket, Irp);
#if WSK_ASYNCCOMPL	
	// DW-1316 replace Waiting-WskCloseSocket method with Async-completion method
#else
	if (Status == STATUS_PENDING) {
		Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime);
		if (STATUS_TIMEOUT == Status) { // DW-1316 detour WskCloseSocket hang in Win7/x86.
			WDRBD_WARN("Timeout... Cancel WskCloseSocket:%p. maybe required to patch WSK Kernel. (irp:%p)\n", WskSocket, Irp);
			IoCancelIrp(Irp);
			// DW-1388: canceling must be completed before freeing the irp.
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		}
		Status = Irp->IoStatus.Status;
	}
	IoFreeIrp(Irp);
#endif
	return Status;
}
#endif

NTSTATUS
NTAPI
Connect(
	__in PWSK_SOCKET	WskSocket,
	__in PSOCKADDR		RemoteAddress
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !RemoteAddress)
		return STATUS_INVALID_PARAMETER;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskConnect(
		WskSocket,
		RemoteAddress,
		0,
		Irp);

	if (Status == STATUS_PENDING) {
		LARGE_INTEGER	nWaitTime;
		nWaitTime = RtlConvertLongToLargeInteger(-1 * 1000 * 1000 * 10);
		if ((Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime)) == STATUS_TIMEOUT)
		{
			IoCancelIrp(Irp);
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		}
	}

	if (Status == STATUS_SUCCESS)
	{
		Status = Irp->IoStatus.Status;
	}

	IoFreeIrp(Irp);
	return Status;
}

NTSTATUS NTAPI
Disconnect(
	__in PWSK_SOCKET	WskSocket
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;
	LARGE_INTEGER	nWaitTime;
	nWaitTime.QuadPart = (-1 * 1000 * 10000);   // wait 1000ms relative 
	
	if (g_SocketsState != INITIALIZED || !WskSocket)
		return STATUS_INVALID_PARAMETER;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}
	
	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskDisconnect(
		WskSocket,
		NULL,
		0,//WSK_FLAG_ABORTIVE,=> when disconnecting, ABORTIVE was going to standalone, and then we removed ABORTIVE
		Irp);

	if (Status == STATUS_PENDING) {
		Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime);
		if(STATUS_TIMEOUT == Status) { // DW-712 timeout process for fast closesocket in congestion mode, instead of WSK_FLAG_ABORTIVE
			WDRBD_INFO("Timeout... Cancel Disconnect socket:%p\n",WskSocket);
			IoCancelIrp(Irp);
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		} 

		Status = Irp->IoStatus.Status;
	}

	IoFreeIrp(Irp);
	return Status;
}

#ifdef _WSK_DISCONNECT_EVENT
PWSK_SOCKET
NTAPI
SocketConnect(
	__in USHORT		SocketType,
	__in ULONG		Protocol,
	__in PSOCKADDR	LocalAddress, // address family desc. required
	__in PSOCKADDR	RemoteAddress, // address family desc. required
	__inout  NTSTATUS* pStatus,
	__in PWSK_CLIENT_CONNECTION_DISPATCH dispatch,
	__in PVOID socketContext
	)
#else 
PWSK_SOCKET
NTAPI
SocketConnect(
__in USHORT		SocketType,
__in ULONG		Protocol,
__in PSOCKADDR	LocalAddress, // address family desc. required
__in PSOCKADDR	RemoteAddress, // address family desc. required
__inout  NTSTATUS* pStatus
)
#endif
{
	KEVENT			CompletionEvent = { 0 };
	PIRP			Irp = NULL;
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;
	PWSK_SOCKET		WskSocket = NULL;

	if (g_SocketsState != INITIALIZED || !RemoteAddress || !LocalAddress || !pStatus)
		return NULL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return NULL;
	}
#ifdef _WSK_DISCONNECT_EVENT
	Status = g_WskProvider.Dispatch->WskSocketConnect(
				g_WskProvider.Client,
				SocketType,
				Protocol,
				LocalAddress,
				RemoteAddress,
				0,
				socketContext,
				dispatch,
				NULL,
				NULL,
				NULL,
				Irp);
#else 
	Status = g_WskProvider.Dispatch->WskSocketConnect(
		g_WskProvider.Client,
		SocketType,
		Protocol,
		LocalAddress,
		RemoteAddress,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		Irp);
#endif 
	if (Status == STATUS_PENDING) {
		LARGE_INTEGER nWaitTime = { 0, };
		nWaitTime = RtlConvertLongToLargeInteger(-3 * 1000 * 1000 * 10);	// 3s
		if ((Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, &nWaitTime)) == STATUS_TIMEOUT)
		{
			IoCancelIrp(Irp);
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);			
			*pStatus = STATUS_TIMEOUT;
		}
		else
			*pStatus = Status = Irp->IoStatus.Status;
	}
	
	WskSocket = Status == STATUS_SUCCESS ? (PWSK_SOCKET) Irp->IoStatus.Information : NULL;
	IoFreeIrp(Irp);
	return WskSocket;
}

char *GetSockErrorString(NTSTATUS status)
{
	char *ErrorString;
	switch (status)
	{
		case STATUS_CONNECTION_RESET:
			ErrorString = "CONNECTION_RESET";
			break;
		case STATUS_CONNECTION_DISCONNECTED:
			ErrorString = "CONNECTION_DISCONNECTED";
			break;
		case STATUS_CONNECTION_ABORTED:
			ErrorString = "CONNECTION_ABORTED";
			break;
		case STATUS_IO_TIMEOUT:
			ErrorString = "IO_TIMEOUT";
			break;
		case STATUS_INVALID_DEVICE_STATE:
			ErrorString = "INVALID_DEVICE_STATE";
			break;
		default:
			ErrorString = "SOCKET_IO_ERROR";
			break;
	}
	return ErrorString;
}

#ifdef _WSK_IRP_REUSE
// for Reusing IRP, first, create IRP outside, and input SendEx's parameter. Irp can be freed in finalize point.
LONG
NTAPI
SendEx(
__in PIRP           pIrp,
__in PWSK_SOCKET	WskSocket,
__in PVOID			Buffer,
__in ULONG			BufferSize,
__in ULONG			Flags,
__in ULONG			Timeout,
__in KEVENT			*send_buf_kill_event
)
{
	KEVENT		CompletionEvent = { 0 };
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesSent = SOCKET_ERROR;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || !pIrp || ((int)BufferSize <= 0))
		return SOCKET_ERROR;
		

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, FALSE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	IoReuseIrp(pIrp, STATUS_UNSUCCESSFUL);
	KeInitializeEvent(&CompletionEvent, SynchronizationEvent, FALSE);
	IoSetCompletionRoutine(pIrp, CompletionRoutine, &CompletionEvent, TRUE, TRUE, TRUE);
	

	Flags |= WSK_FLAG_NODELAY;

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH)WskSocket->Dispatch)->WskSend(
		WskSocket,
		&WskBuffer,
		Flags,
		pIrp);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		PVOID       waitObjects[2];
		int retry_count = 0;

		nWaitTime = RtlConvertLongToLargeInteger(-1 * Timeout * 1000 * 10);
		waitObjects[0] = (PVOID) &CompletionEvent;
		waitObjects[1] = send_buf_kill_event;

	retry:
		Status = KeWaitForMultipleObjects(2, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, &nWaitTime, NULL);
		switch (Status)
		{
			case STATUS_TIMEOUT:
				if (!(retry_count++ % 5))
				{
					WDRBD_WARN("sendbuffing: tx timeout(%d ms). retry.\n", Timeout);// for trace
				}
				// TCP session is no problem. peer does not receive this data yet. he may be busy. So, just retry forever. 
				// the real tx timeout will be occured by upper level sender thread decreasing ko_count at drbd_stream_send_timed_out.
				goto retry;

			case STATUS_WAIT_0:
				if (NT_SUCCESS(pIrp->IoStatus.Status))
				{
					BytesSent = (LONG) pIrp->IoStatus.Information;
				}
				else
				{
					WDRBD_ERROR("sendbuffing: tx error(%s) wsk(0x%p)\n", GetSockErrorString(pIrp->IoStatus.Status), WskSocket);
					switch (pIrp->IoStatus.Status)
					{
					case STATUS_IO_TIMEOUT:
						BytesSent = -EAGAIN;
						break;
					case STATUS_INVALID_DEVICE_STATE:
						BytesSent = -EAGAIN;
						break;
					default:
						BytesSent = -ECONNRESET;
						break;
					}
				}
				break;

			case STATUS_WAIT_1: // send_buffering thread's kill signal
				BytesSent = -EINTR;
				break;

			default:
				WDRBD_ERROR("Wait failed. status 0x%x\n", Status);
				BytesSent = SOCKET_ERROR;
		}
	}
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesSent = (LONG) pIrp->IoStatus.Information;
		}
		else
		{
			WDRBD_ERROR("sendbuffing: WskSend error(0x%x)\n", Status);
			BytesSent = SOCKET_ERROR;
		}
	}

	FreeWskBuffer(&WskBuffer);
	return BytesSent;
}
#endif


LONG
NTAPI
Send(
	__in PWSK_SOCKET	WskSocket,
	__in PVOID			Buffer,
	__in ULONG			BufferSize,
	__in ULONG			Flags,
	__in ULONG			Timeout,
	__in KEVENT			*send_buf_kill_event,
	__in struct			drbd_transport *transport,
	__in enum			drbd_stream stream
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesSent = SOCKET_ERROR; // DRBC_CHECK_WSK: SOCKET_ERROR be mixed EINVAL?
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || ((int)BufferSize <= 0)){
		return SOCKET_ERROR;
	}

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, FALSE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Flags |= WSK_FLAG_NODELAY;

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskSend(
		WskSocket,
		&WskBuffer,
		Flags,
		Irp);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		LARGE_INTEGER	*pTime;

	retry:
		if (Timeout <= 0 || Timeout == MAX_SCHEDULE_TIMEOUT)
		{
			pTime = NULL;
		}
		else
		{
			nWaitTime = RtlConvertLongToLargeInteger(-1 * Timeout * 1000 * 10);
			pTime = &nWaitTime;
		}
		{
			struct      task_struct *thread = current;
			PVOID       waitObjects[2];
			int         wObjCount = 1;

			waitObjects[0] = (PVOID) &CompletionEvent;
#ifndef _WIN32_SEND_BUFFING 
			// in send-buffering, Netlink , call_usermodehelper are distinguished to SendLocal
			// KILL event is only used in send-buffering thread while send block. 
			// WIN32_V9_REFACTO_:required to refactoring that input param, log message are simplized.
#else

#endif
			
			Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);
			switch (Status)
			{
			case STATUS_TIMEOUT:
				// DW-988 refactoring about retry_count. retry_count is removed.
				if (transport != NULL) {
					if (!drbd_stream_send_timed_out(transport, stream)) {
						goto retry;
					}
				}

				IoCancelIrp(Irp);
				KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				BytesSent = -EAGAIN;
				break;

			case STATUS_WAIT_0:
				if (NT_SUCCESS(Irp->IoStatus.Status))
				{
					BytesSent = (LONG)Irp->IoStatus.Information;
				}
				else
				{
					WDRBD_WARN("tx error(%s) wsk(0x%p)\n", GetSockErrorString(Irp->IoStatus.Status), WskSocket);
					switch (Irp->IoStatus.Status)
					{
						case STATUS_IO_TIMEOUT:
							BytesSent = -EAGAIN;
							break;
						case STATUS_INVALID_DEVICE_STATE:
							BytesSent = -EAGAIN;
							break;
						default:
							BytesSent = -ECONNRESET;
							break;
					}
				}
				break;

			//case STATUS_WAIT_1: // common: sender or send_bufferinf thread's kill signal
			//	IoCancelIrp(Irp);
			//	KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
			//	BytesSent = -EINTR;
			//	break;

			default:
				WDRBD_ERROR("Wait failed. status 0x%x\n", Status);
				BytesSent = SOCKET_ERROR;
			}
		}
	}
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesSent = (LONG) Irp->IoStatus.Information;
			WDRBD_WARN("(%s) WskSend No pending: but sent(%d)!\n", current->comm, BytesSent);
		}
		else
		{
			WDRBD_WARN("(%s) WskSend error(0x%x)\n", current->comm, Status);
			BytesSent = SOCKET_ERROR;
		}
	}

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);

	return BytesSent;
}


LONG
NTAPI
SendAsync(
	__in PWSK_SOCKET	WskSocket,
	__in PVOID			Buffer,
	__in ULONG			BufferSize,
	__in ULONG			Flags,
	__in ULONG			Timeout,
	__in struct			drbd_transport *transport,
	__in enum			drbd_stream stream
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesSent = SOCKET_ERROR; // DRBC_CHECK_WSK: SOCKET_ERROR be mixed EINVAL?
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || ((int) BufferSize <= 0))
		return SOCKET_ERROR;

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, FALSE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Flags |= WSK_FLAG_NODELAY;

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskSend(
		WskSocket,
		&WskBuffer,
		Flags,
		Irp);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		LARGE_INTEGER	*pTime;

		if (Timeout <= 0 || Timeout == MAX_SCHEDULE_TIMEOUT)
		{
			pTime = NULL;
		}
		else
		{
			nWaitTime = RtlConvertLongToLargeInteger(-1 * Timeout * 1000 * 10);
			pTime = &nWaitTime;
		}
		{
			//struct      task_struct *thread = current;
			int 		retry_count = 0;
$retry:			
			// DW-1173: do not wait for send_buf_kill_event, we need to send all items queued before cleaning up.
			Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, pTime);
			switch (Status) {
			case STATUS_TIMEOUT:
				// DW-1095 adjust retry_count logic 
				if (!(++retry_count % 5)) {
					WDRBD_WARN("send buffering: tx timeout(%d ms). retry.\n", Timeout);// for trace
					// DW-1524 fix infinite send retry on low-bandwith
					IoCancelIrp(Irp);
					KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
					BytesSent = -EAGAIN;
					break;
				} 

				goto $retry;
				
				//IoCancelIrp(Irp);
				//KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				//BytesSent = -EAGAIN;
				break;

			case STATUS_SUCCESS:
				if (NT_SUCCESS(Irp->IoStatus.Status))
				{
					BytesSent = (LONG)Irp->IoStatus.Information;
				}
				else
				{
					WDRBD_WARN("tx error(%s) wsk(0x%p)\n", GetSockErrorString(Irp->IoStatus.Status), WskSocket);
					switch (Irp->IoStatus.Status)
					{
						case STATUS_IO_TIMEOUT:
							BytesSent = -EAGAIN;
							break;
						case STATUS_INVALID_DEVICE_STATE:
							BytesSent = -EAGAIN;
							break;
						default:
							BytesSent = -ECONNRESET;
							break;
					}
				}
				break;

			default:
				WDRBD_ERROR("Wait failed. status 0x%x\n", Status);
				BytesSent = SOCKET_ERROR;
			}
		}
	}
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesSent = (LONG) Irp->IoStatus.Information;
			WDRBD_WARN("(%s) WskSend No pending: but sent(%d)!\n", current->comm, BytesSent);
		}
		else
		{
			WDRBD_WARN("(%s) WskSend error(0x%x)\n", current->comm, Status);
			BytesSent = SOCKET_ERROR;
		}
	}

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);

	return BytesSent;
}


LONG
NTAPI
SendLocal(
	__in PWSK_SOCKET	WskSocket,
	__in PVOID			Buffer,
	__in ULONG			BufferSize,
	__in ULONG			Flags,
	__in ULONG			Timeout
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesSent = SOCKET_ERROR;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || ((int) BufferSize <= 0))
		return SOCKET_ERROR;

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, FALSE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Flags |= WSK_FLAG_NODELAY;

	if(gbShutdown) { // DW-1015 fix crash. WskSocket->Dispatch)->WskSend is NULL while machine is shutdowning
		IoFreeIrp(Irp);
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	if (!WskSocket->Dispatch) { // DW-1029 to prevent possible contingency, check if dispatch table is valid.
		IoFreeIrp(Irp);
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskSend(
		WskSocket,
		&WskBuffer,
		Flags,
		Irp);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		LARGE_INTEGER	*pTime;

		if (Timeout <= 0 || Timeout == MAX_SCHEDULE_TIMEOUT)
		{
			pTime = NULL;
		}
		else
		{
			nWaitTime = RtlConvertLongToLargeInteger(-1 * Timeout * 1000 * 10);
			pTime = &nWaitTime;
		}
		Status = KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, pTime);

		switch (Status)
		{
		case STATUS_TIMEOUT:
			IoCancelIrp(Irp);
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
			BytesSent = -EAGAIN;
			break;

		case STATUS_WAIT_0:
			if (NT_SUCCESS(Irp->IoStatus.Status))
			{
				BytesSent = (LONG) Irp->IoStatus.Information;
			}
			else
			{
				WDRBD_WARN("(%s) sent error(%s)\n", current->comm, GetSockErrorString(Irp->IoStatus.Status));
				switch (Irp->IoStatus.Status)
				{
				case STATUS_IO_TIMEOUT:
					BytesSent = -EAGAIN;
					break;
				case STATUS_INVALID_DEVICE_STATE:
					BytesSent = -EAGAIN;
					break;
				default:
					BytesSent = -ECONNRESET;
					break;
				}
			}
			break;

		default:
			WDRBD_ERROR("KeWaitForSingleObject failed. status 0x%x\n", Status);
			BytesSent = SOCKET_ERROR;
		}
	}
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesSent = (LONG) Irp->IoStatus.Information;
			WDRBD_WARN("(%s) WskSend No pending: but sent(%d)!\n", current->comm, BytesSent);
		}
		else
		{
			WDRBD_WARN("(%s) WskSend error(0x%x)\n", current->comm, Status);
			BytesSent = SOCKET_ERROR;
		}
	}

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);

	return BytesSent;
}

LONG
NTAPI
SendTo(
	__in PWSK_SOCKET	WskSocket,
	__in PVOID			Buffer,
	__in ULONG			BufferSize,
	__in_opt PSOCKADDR	RemoteAddress
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesSent = SOCKET_ERROR;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || !BufferSize)
		return SOCKET_ERROR;

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, FALSE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_DATAGRAM_DISPATCH) WskSocket->Dispatch)->WskSendTo(
		WskSocket,
		&WskBuffer,
		0,
		RemoteAddress,
		0,
		NULL,
		Irp);
	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	BytesSent = NT_SUCCESS(Status) ? (LONG) Irp->IoStatus.Information : SOCKET_ERROR;

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);
	return BytesSent;
}

LONG NTAPI ReceiveLocal(
	__in  PWSK_SOCKET	WskSocket,
	__out PVOID			Buffer,
	__in  ULONG			BufferSize,
	__in  ULONG			Flags,
	__in ULONG			Timeout
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesReceived = SOCKET_ERROR;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

    struct      task_struct *thread = current;
    PVOID       waitObjects[2];
    int         wObjCount = 1;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || !BufferSize)
		return SOCKET_ERROR;

	if ((int) BufferSize <= 0)
	{
		return SOCKET_ERROR;
	}

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, TRUE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);

	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	if(gbShutdown) {
		IoFreeIrp(Irp);
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskReceive(
				WskSocket,
				&WskBuffer,
				Flags,
				Irp);

    if (Status == STATUS_PENDING)
    {
        LARGE_INTEGER	nWaitTime;
        LARGE_INTEGER	*pTime;

        if (Timeout <= 0 || Timeout == MAX_SCHEDULE_TIMEOUT)
        {
            pTime = 0;
        }
        else
        {
            nWaitTime = RtlConvertLongToLargeInteger(-1 * Timeout * 1000 * 10);
            pTime = &nWaitTime;
        }

        waitObjects[0] = (PVOID) &CompletionEvent;
        if (thread->has_sig_event)
        {
            waitObjects[1] = (PVOID) &thread->sig_event;
            wObjCount = 2;
        }
        Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);
        switch (Status)
        {
        case STATUS_WAIT_0: // waitObjects[0] CompletionEvent
            if (Irp->IoStatus.Status == STATUS_SUCCESS)
            {
                BytesReceived = (LONG) Irp->IoStatus.Information;
            }
            else
            {
				WDRBD_INFO("RECV(%s) wsk(0x%p) multiWait err(0x%x:%s)\n", thread->comm, WskSocket, Irp->IoStatus.Status, GetSockErrorString(Irp->IoStatus.Status));
				if(Irp->IoStatus.Status)
                {
                    BytesReceived = -ECONNRESET;
                }
            }
            break;

        case STATUS_WAIT_1:
            BytesReceived = -EINTR;
            break;

        case STATUS_TIMEOUT:
            BytesReceived = -EAGAIN;
            break;

        default:
            BytesReceived = SOCKET_ERROR;
            break;
        }
    }
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesReceived = (LONG) Irp->IoStatus.Information;
			WDRBD_INFO("(%s) Rx No pending and data(%d) is avail\n", current->comm, BytesReceived);
		}
		else
		{
			WDRBD_TRACE("WskReceive Error Status=0x%x\n", Status); // EVENT_LOG!
		}
	}

	if (BytesReceived == -EINTR || BytesReceived == -EAGAIN)
	{
		// cancel irp in wsk subsystem
		IoCancelIrp(Irp);
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		if (Irp->IoStatus.Information > 0)
		{
			//WDRBD_INFO("rx canceled but rx data(%d) avaliable.\n", Irp->IoStatus.Information);
			BytesReceived = Irp->IoStatus.Information;
		}
	}

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);

	return BytesReceived;
}


LONG NTAPI Receive(
	__in  PWSK_SOCKET	WskSocket,
	__out PVOID			Buffer,
	__in  ULONG			BufferSize,
	__in  ULONG			Flags,
	__in ULONG			Timeout
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesReceived = SOCKET_ERROR;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

    struct      task_struct *thread = current;
    PVOID       waitObjects[2];
    int         wObjCount = 1;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || !BufferSize)
		return SOCKET_ERROR;

	if ((int) BufferSize <= 0)
	{
		return SOCKET_ERROR;
	}

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, TRUE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);

	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskReceive(
				WskSocket,
				&WskBuffer,
				Flags,
				Irp);

    if (Status == STATUS_PENDING)
    {
        LARGE_INTEGER	nWaitTime;
        LARGE_INTEGER	*pTime;

        if (Timeout <= 0 || Timeout == MAX_SCHEDULE_TIMEOUT)
        {
            pTime = 0;
        }
        else
        {
            nWaitTime = RtlConvertLongToLargeInteger(-1 * Timeout * 1000 * 10);
            pTime = &nWaitTime;
        }

        waitObjects[0] = (PVOID) &CompletionEvent;
        if (thread->has_sig_event)
        {
            waitObjects[1] = (PVOID) &thread->sig_event;
            wObjCount = 2;
        }
        Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);
        switch (Status)
        {
        case STATUS_WAIT_0: // waitObjects[0] CompletionEvent
            if (Irp->IoStatus.Status == STATUS_SUCCESS)
            {
                BytesReceived = (LONG) Irp->IoStatus.Information;
            }
            else
            {
				WDRBD_INFO("RECV(%s) wsk(0x%p) multiWait err(0x%x:%s)\n", thread->comm, WskSocket, Irp->IoStatus.Status, GetSockErrorString(Irp->IoStatus.Status));
				if(Irp->IoStatus.Status)
                {
                    BytesReceived = -ECONNRESET;
                }
            }
            break;

        case STATUS_WAIT_1:
            BytesReceived = -EINTR;
            break;

        case STATUS_TIMEOUT:
            BytesReceived = -EAGAIN;
            break;

        default:
            BytesReceived = SOCKET_ERROR;
            break;
        }
    }
	else
	{
		if (Status == STATUS_SUCCESS)
		{
			BytesReceived = (LONG) Irp->IoStatus.Information;
			WDRBD_INFO("(%s) Rx No pending and data(%d) is avail\n", current->comm, BytesReceived);
		}
		else
		{
			WDRBD_TRACE("WskReceive Error Status=0x%x\n", Status); // EVENT_LOG!
		}
	}

	if (BytesReceived == -EINTR || BytesReceived == -EAGAIN)
	{
		// cancel irp in wsk subsystem
		IoCancelIrp(Irp);
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		if (Irp->IoStatus.Information > 0)
		{
			//WDRBD_INFO("rx canceled but rx data(%d) avaliable.\n", Irp->IoStatus.Information);
			BytesReceived = Irp->IoStatus.Information;
		}
	}

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);

	return BytesReceived;
}

LONG
NTAPI
ReceiveFrom(
	__in  PWSK_SOCKET	WskSocket,
	__out PVOID			Buffer,
	__in  ULONG			BufferSize,
	__out_opt PSOCKADDR	RemoteAddress,
	__out_opt PULONG	ControlFlags
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	WSK_BUF		WskBuffer = { 0 };
	LONG		BytesReceived = SOCKET_ERROR;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !Buffer || !BufferSize)
		return SOCKET_ERROR;

	Status = InitWskBuffer(Buffer, BufferSize, &WskBuffer, TRUE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		FreeWskBuffer(&WskBuffer);
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_DATAGRAM_DISPATCH) WskSocket->Dispatch)->WskReceiveFrom(
		WskSocket,
		&WskBuffer,
		0,
		RemoteAddress,
		0,
		NULL,
		ControlFlags,
		Irp);
	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	BytesReceived = NT_SUCCESS(Status) ? (LONG) Irp->IoStatus.Information : SOCKET_ERROR;

	IoFreeIrp(Irp);
	FreeWskBuffer(&WskBuffer);
	return BytesReceived;
}

NTSTATUS
NTAPI
Bind(
	__in PWSK_SOCKET	WskSocket,
	__in PSOCKADDR		LocalAddress
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket || !LocalAddress)
		return STATUS_INVALID_PARAMETER;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskBind(
		WskSocket,
		LocalAddress,
		0,
		Irp);

	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}
	IoFreeIrp(Irp);
	return Status;
}

PWSK_SOCKET
NTAPI
AcceptLocal(
	__in PWSK_SOCKET	WskSocket,
	__out_opt PSOCKADDR	LocalAddress,
	__out_opt PSOCKADDR	RemoteAddress,
	__out_opt NTSTATUS	*RetStaus,
	__in int			timeout
)
{
	KEVENT			CompletionEvent = { 0 };
	PIRP			Irp = NULL;
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;
	PWSK_SOCKET		AcceptedSocket = NULL;
    struct task_struct *thread = current;
    PVOID waitObjects[2];
    int wObjCount = 1;

	if (g_SocketsState != INITIALIZED || !WskSocket)
	{
		*RetStaus = SOCKET_ERROR;
		return NULL;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		*RetStaus = Status;
		return NULL;
	}

	if(gbShutdown) {
		*RetStaus = SOCKET_ERROR;
		IoFreeIrp(Irp);
		return NULL;	
	}
	Status = ((PWSK_PROVIDER_LISTEN_DISPATCH) WskSocket->Dispatch)->WskAccept(
		WskSocket,
		0,
		NULL,
		NULL,
		LocalAddress,
		RemoteAddress,
		Irp);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		LARGE_INTEGER	*pTime;

		if (timeout <= 0 || timeout == MAX_SCHEDULE_TIMEOUT)
		{
			pTime = 0;
		}
		else
		{
			nWaitTime = RtlConvertLongToLargeInteger(-1 * timeout * 10000000);
			pTime = &nWaitTime;
		}

        waitObjects[0] = (PVOID) &CompletionEvent;
        if (thread->has_sig_event)
        {
            waitObjects[1] = (PVOID) &thread->sig_event;
            wObjCount = 2;
        }

        Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);

		switch (Status)
		{
			case STATUS_WAIT_0:
				break;

			case STATUS_WAIT_0 + 1:
				IoCancelIrp(Irp);
				KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				*RetStaus = -EINTR;	
				break;

			case STATUS_TIMEOUT:
				IoCancelIrp(Irp);
				KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				*RetStaus = STATUS_TIMEOUT;
				break;

			default:
				WDRBD_ERROR("Unexpected Error Status=0x%x\n", Status);
				break;
		}
	}
	else
	{
		if (Status != STATUS_SUCCESS)
		{
			WDRBD_TRACE("Accept Error Status=0x%x\n", Status);
		}
	}

	AcceptedSocket = (Status == STATUS_SUCCESS) ? (PWSK_SOCKET) Irp->IoStatus.Information : NULL;
	IoFreeIrp(Irp);
	return AcceptedSocket;
}


PWSK_SOCKET
NTAPI
Accept(
	__in PWSK_SOCKET	WskSocket,
	__out_opt PSOCKADDR	LocalAddress,
	__out_opt PSOCKADDR	RemoteAddress,
	__out_opt NTSTATUS	*RetStaus,
	__in int			timeout
)
{
	KEVENT			CompletionEvent = { 0 };
	PIRP			Irp = NULL;
	NTSTATUS		Status = STATUS_UNSUCCESSFUL;
	PWSK_SOCKET		AcceptedSocket = NULL;
    struct task_struct *thread = current;
    PVOID waitObjects[2];
    int wObjCount = 1;

	if (g_SocketsState != INITIALIZED || !WskSocket)
	{
		*RetStaus = SOCKET_ERROR;
		return NULL;
	}

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		*RetStaus = Status;
		return NULL;
	}

	Status = ((PWSK_PROVIDER_LISTEN_DISPATCH) WskSocket->Dispatch)->WskAccept(
		WskSocket,
		0,
		NULL,
		NULL,
		LocalAddress,
		RemoteAddress,
		Irp);

	if (Status == STATUS_PENDING)
	{
		LARGE_INTEGER	nWaitTime;
		LARGE_INTEGER	*pTime;

		if (timeout <= 0 || timeout == MAX_SCHEDULE_TIMEOUT)
		{
			pTime = 0;
		}
		else
		{
			nWaitTime = RtlConvertLongToLargeInteger(-1 * timeout * 10000000);
			pTime = &nWaitTime;
		}

        waitObjects[0] = (PVOID) &CompletionEvent;
        if (thread->has_sig_event)
        {
            waitObjects[1] = (PVOID) &thread->sig_event;
            wObjCount = 2;
        }

        Status = KeWaitForMultipleObjects(wObjCount, &waitObjects[0], WaitAny, Executive, KernelMode, FALSE, pTime, NULL);

		switch (Status)
		{
			case STATUS_WAIT_0:
				break;

			case STATUS_WAIT_0 + 1:
				IoCancelIrp(Irp);
				KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				*RetStaus = -EINTR;	
				break;

			case STATUS_TIMEOUT:
				IoCancelIrp(Irp);
				KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
				*RetStaus = STATUS_TIMEOUT;
				break;

			default:
				WDRBD_ERROR("Unexpected Error Status=0x%x\n", Status);
				break;
		}
	}
	else
	{
		if (Status != STATUS_SUCCESS)
		{
			WDRBD_TRACE("Accept Error Status=0x%x\n", Status);
		}
	}

	AcceptedSocket = (Status == STATUS_SUCCESS) ? (PWSK_SOCKET) Irp->IoStatus.Information : NULL;
	IoFreeIrp(Irp);
	return AcceptedSocket;
}

NTSTATUS
NTAPI
ControlSocket(
	__in PWSK_SOCKET	WskSocket,
	__in ULONG			RequestType,
	__in ULONG		    ControlCode,
	__in ULONG			Level,
	__in SIZE_T			InputSize,
	__in_opt PVOID		InputBuffer,
	__in SIZE_T			OutputSize,
	__out_opt PVOID		OutputBuffer,
	__out_opt SIZE_T	*OutputSizeReturned
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	if (g_SocketsState != INITIALIZED || !WskSocket)
		return SOCKET_ERROR;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		WDRBD_ERROR("InitWskData() failed with status 0x%08X\n", Status);
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskControlSocket(
				WskSocket,
				RequestType,		// WskSetOption, 
				ControlCode,		// SIO_WSK_QUERY_RECEIVE_BACKLOG, 
				Level,				// IPPROTO_IPV6,
				InputSize,			// sizeof(optionValue),
				InputBuffer,		// NULL, 
				OutputSize,			// sizeof(int), 
				OutputBuffer,		// &backlog, 
				OutputSizeReturned, // NULL,
				Irp);


	if (Status == STATUS_PENDING) {
		KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
		Status = Irp->IoStatus.Status;
	}

	IoFreeIrp(Irp);
	return Status;
}

NTSTATUS
NTAPI
GetRemoteAddress(
	__in PWSK_SOCKET	WskSocket,
	__out PSOCKADDR	pRemoteAddress
)
{
	KEVENT		CompletionEvent = { 0 };
	PIRP		Irp = NULL;
	NTSTATUS	Status = STATUS_UNSUCCESSFUL;

	Status = InitWskData(&Irp, &CompletionEvent, FALSE);
	if (!NT_SUCCESS(Status)) {
		return SOCKET_ERROR;
	}

	Status = ((PWSK_PROVIDER_CONNECTION_DISPATCH) WskSocket->Dispatch)->WskGetRemoteAddress(WskSocket, pRemoteAddress, Irp);
	if (Status != STATUS_SUCCESS)
	{
		if (Status == STATUS_PENDING) {
			KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
			Status = Irp->IoStatus.Status;
		}

		if (Status != STATUS_SUCCESS)
		{
			if (Status != STATUS_INVALID_DEVICE_STATE)
			{
				WDRBD_TRACE("STATUS_INVALID_DEVICE_STATE....\n");
			}
			else if (Status != STATUS_FILE_FORCED_CLOSED)
			{
				WDRBD_TRACE("STATUS_FILE_FORCED_CLOSED....\n");
			}
			else
			{
				WDRBD_TRACE("0x%x....\n", Status);
			}
		}
	}
	IoFreeIrp(Irp);
	return Status;
}

PWSK_SOCKET         netlink_server_socket = NULL;
#ifndef _WIN32_NETLINK_EX
WSK_REGISTRATION    gWskEventRegistration;
WSK_PROVIDER_NPI    gWskEventProviderNPI;


// Socket-level callback table for listening sockets
const WSK_CLIENT_LISTEN_DISPATCH ClientListenDispatch = {
    NetlinkAcceptEvent,
    NULL, // WskInspectEvent is required only if conditional-accept is used.
    NULL  // WskAbortEvent is required only if conditional-accept is used.
};

NTSTATUS
InitWskEvent()
{
    NTSTATUS status;
    WSK_CLIENT_NPI  wskClientNpi;

    wskClientNpi.ClientContext = NULL;
    wskClientNpi.Dispatch = &g_WskDispatch;
    
    status = WskRegister(&wskClientNpi, &gWskEventRegistration);
    if (!NT_SUCCESS(status))
    {
        WDRBD_ERROR("Failed to WskRegister(). status(0x%x)\n", status);
        return status;
    }

    status = WskCaptureProviderNPI(&gWskEventRegistration,
        WSK_INFINITE_WAIT, &gWskEventProviderNPI);
	
	if (!NT_SUCCESS(status))
    {
        WDRBD_ERROR("Failed to WskCaptureProviderNPI(). status(0x%x)\n", status);
        WskDeregister(&gWskEventRegistration);
        return status;
    }
	//WDRBD_INFO("WskProvider Version Major:%d Minor:%d\n",WSK_MAJOR_VERSION(gWskEventProviderNPI.Dispatch->Version),WSK_MINOR_VERSION(gWskEventProviderNPI.Dispatch->Version));
    return status;
}

PWSK_SOCKET
CreateSocketEvent(
__in ADDRESS_FAMILY	AddressFamily,
__in USHORT			SocketType,
__in ULONG			Protocol,
__in ULONG			Flags
)
{
    KEVENT			CompletionEvent = {0};
    PIRP			irp = NULL;
    PWSK_SOCKET		socket = NULL;
    NTSTATUS		status;

    status = InitWskData(&irp, &CompletionEvent, FALSE);
    if (!NT_SUCCESS(status))
    {
        return NULL;
    }

    WSK_EVENT_CALLBACK_CONTROL callbackControl;

    callbackControl.NpiId = (PNPIID)&NPI_WSK_INTERFACE_ID;
    callbackControl.EventMask = WSK_EVENT_ACCEPT;

    status = gWskEventProviderNPI.Dispatch->WskControlClient(
        gWskEventProviderNPI.Client,
        WSK_SET_STATIC_EVENT_CALLBACKS,
        sizeof(callbackControl),
        &callbackControl,
        0,
        NULL,
        NULL,
        NULL);
    if (!NT_SUCCESS(status))
    {
        IoFreeIrp(irp);
        WDRBD_ERROR("Failed to WskControlClient(). status(0x%x)\n", status);
        return NULL;
    }

    status = gWskEventProviderNPI.Dispatch->WskSocket(
        gWskEventProviderNPI.Client,
        AddressFamily,
        SocketType,
        Protocol,
        Flags,
        NULL,
        &ClientListenDispatch,
        NULL,
        NULL,
        NULL,
        irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
        status = irp->IoStatus.Status;
    }

    if (NT_SUCCESS(status))
    {
        socket = (PWSK_SOCKET)irp->IoStatus.Information;
    }
    else
    {
        WDRBD_ERROR("Failed to WskSocket(). status(0x%x)\n", status);
    }

    IoFreeIrp(irp);

    return (PWSK_SOCKET)socket;
}

NTSTATUS
CloseWskEventSocket()
{
    if (!netlink_server_socket)
    {
        return STATUS_SUCCESS;
    }

    KEVENT		CompletionEvent = {0};
    PIRP		irp = NULL;

    NTSTATUS status = InitWskData(&irp, &CompletionEvent,FALSE);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = ((PWSK_PROVIDER_BASIC_DISPATCH)netlink_server_socket->Dispatch)->WskCloseSocket(netlink_server_socket, irp);
    if (STATUS_PENDING == status)
    {
        KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
        status = irp->IoStatus.Status;
    }

    IoFreeIrp(irp);

    WskDeregister(&gWskEventRegistration);

    return status;
}


void
ReleaseProviderNPI()
{
    WskReleaseProviderNPI(&gWskEventRegistration);
}
#endif

NTSTATUS
NTAPI
SetEventCallbacks(
__in PWSK_SOCKET Socket,
__in LONG			mask
)
{
    KEVENT			CompletionEvent = { 0 };
    PIRP			Irp = NULL;
    PWSK_SOCKET		WskSocket = NULL;
    NTSTATUS		Status = STATUS_UNSUCCESSFUL;

    if (g_SocketsState != INITIALIZED)
    {
        return Status;
    }

    Status = InitWskData(&Irp, &CompletionEvent,FALSE);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    WSK_EVENT_CALLBACK_CONTROL callbackControl;
    callbackControl.NpiId = &NPI_WSK_INTERFACE_ID;

    // Set the event flags for the event callback functions that
    // are to be enabled on the socket
    callbackControl.EventMask = mask;

    // Initiate the control operation on the socket
    Status =
        ((PWSK_PROVIDER_BASIC_DISPATCH)Socket->Dispatch)->WskControlSocket(
        Socket,
        WskSetOption,
        SO_WSK_EVENT_CALLBACK,
        SOL_SOCKET,
        sizeof(WSK_EVENT_CALLBACK_CONTROL),
        &callbackControl,
        0,
        NULL,
        NULL,
        Irp
        );

    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&CompletionEvent, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    IoFreeIrp(Irp);
    return Status;
}

/**
 * applies to whether conditional acceptance mode is enabled on a listening socket
 * @Mode: conditional accept mode
 *  0 - Disable
 *  1 - Enable
 */
NTSTATUS
NTAPI
SetConditionalAccept(
    __in PWSK_SOCKET ListeningSocket,
    __in ULONG       Mode
)
{
    return ControlSocket(
        ListeningSocket,
        WskSetOption,           // RequestType
        SO_CONDITIONAL_ACCEPT,  // ControlCode
        SOL_SOCKET,	            // level
        sizeof(ULONG),          // InputSize
        &Mode,                  // NULL
        0,                      // OutputSize
        NULL,                   // OutputBuffer
        NULL                    // OutputSizeReturned
    );
}

NTSTATUS WSKAPI
AcceptEvent(
_In_  PVOID         SocketContext,
_In_  ULONG         Flags,
_In_  PSOCKADDR     LocalAddress,
_In_  PSOCKADDR     RemoteAddress,
_In_opt_  PWSK_SOCKET AcceptSocket,
_Outptr_result_maybenull_ PVOID *AcceptSocketContext,
_Outptr_result_maybenull_ CONST WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch
)
{   
    UNREFERENCED_PARAMETER(Flags);
    // Check for a valid new socket
    if (AcceptSocket != NULL)
    {
        WDRBD_INFO("incoming connection on a listening socket.\n");
        struct accept_wait_data *ad = (struct accept_wait_data*)SocketContext;        
        ad->s_accept = kzalloc(sizeof(struct socket), 0, '89DW');
        if(!ad->s_accept) {
        	return STATUS_REQUEST_NOT_ACCEPTED;
        }
        ad->s_accept->sk = AcceptSocket;
        sprintf(ad->s_accept->name, "estab_sock");
        ad->s_accept->sk_linux_attr = kzalloc(sizeof(struct sock), 0, '92DW');
        if (!ad->s_accept->sk_linux_attr)
        {
            ExFreePool(ad->s_accept);
            return STATUS_REQUEST_NOT_ACCEPTED;
        }

        complete(&ad->door_bell);
        return STATUS_SUCCESS;
    }
    // Error with listening socket
    else
    {
        return STATUS_REQUEST_NOT_ACCEPTED;
    }
}


#ifdef _WSK_DISCONNECT_EVENT 
NTSTATUS WskDisconnectEvent(
	_In_opt_ PVOID SocketContext,
	_In_     ULONG Flags
	)
{
	UNREFERENCED_PARAMETER(Flags);
	
	WDRBD_CONN_TRACE("WskDisconnectEvent\n");
	struct socket *sock = (struct socket *)SocketContext; 
	WDRBD_CONN_TRACE("socket->sk = %p\n", sock->sk);
	sock->sk_state = TCP_DISCONNECTED;
	return STATUS_SUCCESS;
}
#endif

