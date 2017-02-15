#include "pch.h"

PDEVICE_OBJECT g_DeviceObject;
UNICODE_STRING g_usDeviceName;
UNICODE_STRING g_usSymlinkName;

NTSTATUS
drbdlockCreateControlDeviceObject(
	IN PDRIVER_OBJECT pDrvObj
	)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	ULONG i;

	RtlInitUnicodeString(&g_usDeviceName, DRBDLOCK_DEVICE_OBJECT_NAME);
	status = IoCreateDevice(pDrvObj, 0, &g_usDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &g_DeviceObject);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	RtlInitUnicodeString(&g_usSymlinkName, DRBDLOCK_SYMLINK_NAME);
	status = IoCreateSymbolicLink(&g_usSymlinkName, &g_usDeviceName);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
		pDrvObj->MajorFunction[i] = DefaultIrpDispatch;		

	pDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceIoControlDispatch;

	return status;
}

VOID
drbdlockDeleteControlDeviceObject(
	VOID
	)
{
	IoDeleteSymbolicLink(&g_usSymlinkName);

	if (g_DeviceObject != NULL)
		IoDeleteDevice(g_DeviceObject);
}

NTSTATUS
DefaultIrpDispatch(
	IN PDEVICE_OBJECT pDeviceObject,
	IN PIRP pIrp
	)
{
	UNREFERENCED_PARAMETER(pDeviceObject);

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;

	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS
DeviceIoControlDispatch(
	IN PDEVICE_OBJECT pDeviceObject,
	IN PIRP pIrp
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION pIrpStack = NULL;

	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);

	switch (pIrpStack->Parameters.DeviceIoControl.IoControlCode)
	{	
		default:
		{
			break;
		}
	}

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	UNREFERENCED_PARAMETER(pDeviceObject);

	return status;
}