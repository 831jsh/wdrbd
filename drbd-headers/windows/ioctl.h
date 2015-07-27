#ifndef __MVF_IOCTL_H__
#define __MVF_IOCTL_H__


#define	MVOL_DEVICE		"\\\\.\\mvolCntl"

//
// IOCTL
//
#define	MVOL_TYPE		0x9800

#define	IOCTL_MVOL_GET_VOLUME_COUNT			CTL_CODE(MVOL_TYPE, 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_GET_VOLUMES_INFO			CTL_CODE(MVOL_TYPE, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_GET_VOLUME_INFO			CTL_CODE(MVOL_TYPE, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_INIT_VOLUME_THREAD			CTL_CODE(MVOL_TYPE, 5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_CLOSE_VOLUME_THREAD			CTL_CODE(MVOL_TYPE, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_VOLUME_START				CTL_CODE(MVOL_TYPE, 10, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_STOP				CTL_CODE(MVOL_TYPE, 11, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_MOUNT_VOLUME             CTL_CODE(MVOL_TYPE, 15, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_GET_VOLUME_SIZE			CTL_CODE(MVOL_TYPE, 21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_READ_OFF			CTL_CODE(MVOL_TYPE, 22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_READ_ON			CTL_CODE(MVOL_TYPE, 23, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_WRITE_OFF			CTL_CODE(MVOL_TYPE, 24, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define	IOCTL_MVOL_VOLUME_WRITE_ON			CTL_CODE(MVOL_TYPE, 25, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_GET_COUNT_INFO			CTL_CODE(MVOL_TYPE, 30, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define	IOCTL_MVOL_GET_PROC_DRBD			CTL_CODE(MVOL_TYPE, 38, METHOD_BUFFERED, FILE_ANY_ACCESS)


#define	MAXDEVICENAME			256     // kmpak 1024 -> 256
#define MAX_PROC_BUF			2048		

//
// Structure
//
typedef struct _MVOL_VOLUME_INFO
{
	BOOLEAN				Active;
	WCHAR				PhysicalDeviceName[MAXDEVICENAME];		// src device
	ULONG				PeerIp;
	USHORT				PeerPort;
	CHAR				Seq[MAX_PROC_BUF]; // DRBD_CHECK_DW130: check enough? and chaneg to dynamically
} MVOL_VOLUME_INFO, *PMVOL_VOLUME_INFO;

typedef struct _MVOL_COUNT_INFO
{
	LARGE_INTEGER			WriteCount;
	ULONG				IrpCount;
} MVOL_COUNT_INFO, *PMVOL_COUNT_INFO;

typedef struct _MVOL_SYNC_REQ
{
	WCHAR				PhysicalDeviceName[MAXDEVICENAME];
	LARGE_INTEGER			Offset;
	ULONG				BlockSize;
	ULONG				Count;
} MVOL_SYNC_REQ, *PMVOL_SYNC_REQ;

#endif __MVF_IOCTL_H__
