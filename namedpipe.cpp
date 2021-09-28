#include "namedpipe.h"
//---------------------------------------------------------------------------
#define XYNAMEDPIPE_BUFFERSIZE												4096
//---------------------------------------------------------------------------
#define XYNAMEDPIPE_FLAG_READ												0x01
#define XYNAMEDPIPE_FLAG_ACCEPTED											0x02
//---------------------------------------------------------------------------
struct xynamedpipe_context
{
	// receive
	OVERLAPPED o0;
	// send
	OVERLAPPED o1;
	void *hpipe;

	void *context;

	unsigned char *buffer0;
};

struct namedpipe_client_thread_parameters
{
	struct xynamedpipe *pnp;

	t_namedpipe_procedure p_procedure;

	// only for client
	HANDLE hpipe;

	HANDLE hevent;

	void *context;

	unsigned int timeout;
};
struct namedpipe_server_thread_parameters
{
	struct xynamedpipe *pnp;

	t_namedpipe_procedure p_procedure;

	HANDLE hevent;

	const WCHAR *pipename;

	unsigned int maximum;

	unsigned int timeout;

	// only for server
	unsigned int openmode;
	unsigned int pipemode;
};
//---------------------------------------------------------------------------
int namedpipe_context_initialize(struct xynamedpipe_context *pcontext)
{
	LPOVERLAPPED po0 = &pcontext->o0;
	LPOVERLAPPED po1 = &pcontext->o1;

	pcontext->context = NULL;
	pcontext->buffer0 = NULL;

	pcontext->hpipe = INVALID_HANDLE_VALUE;

	po0->Offset = 0;
	po0->OffsetHigh = 0;
	po0->Internal = WAIT_OBJECT_0;
	po0->InternalHigh = 0;
	po0->Pointer = NULL;
	// Create an event object for this instance. 
	po0->hEvent = CreateEvent(
		NULL,					// default security attribute 
		TRUE,					// manual-reset event 
		FALSE,					// initial state = signaled 
		NULL);					// unnamed event object 

	po1->Offset = 0;
	po1->OffsetHigh = 0;
	po1->Internal = WAIT_OBJECT_0;
	po1->InternalHigh = 0;
	po1->Pointer = NULL;
	// Create an event object for this instance. 
	po1->hEvent = CreateEvent(
		NULL,					// default security attribute 
		TRUE,					// manual-reset event 
		FALSE,					// initial state = signaled 
		NULL);					// unnamed event object 

	return(po0->hEvent != NULL && po1->hEvent != NULL);
}
int namedpipe_context_uninitialize(struct xynamedpipe_context *pcontext, int disconnected)
{
	LPOVERLAPPED po0 = &pcontext->o0;
	LPOVERLAPPED po1 = &pcontext->o1;
	int result = 0;

	pcontext->context = NULL;

	if (pcontext->hpipe != INVALID_HANDLE_VALUE)
	{
		if (!disconnected)
		{
			DisconnectNamedPipe(pcontext->hpipe);
		}
		CloseHandle(pcontext->hpipe);
	}

	if (pcontext->buffer0 != NULL)
	{
		FREE(pcontext->buffer0);

		result++;
	}

	if (po0->hEvent != NULL)
	{
		CloseHandle(po0->hEvent);

		result++;
	}
	if (po1->hEvent != NULL)
	{
		CloseHandle(po1->hEvent);

		result++;
	}
	return(result);
}

// for server
int namedpipe_accept(void *hpipe, LPOVERLAPPED po, int *disconnected)
{
	int errorcode = ERROR_CANCELLED;

	if (*disconnected || DisconnectNamedPipe(hpipe))
	{
		*disconnected = TRUE;

		// Call a subroutine to connect to the new client. 
		if (ConnectNamedPipe(hpipe, po) == 0)
		{
			errorcode = GetLastError();

			//_tprintf(_T("ConnectNamedPipe GetLastError %d\r\n"), errorcode);

			switch (errorcode)
			{
			case ERROR_IO_PENDING:
				// The overlapped connection in progress. 
				break;
			case ERROR_PIPE_CONNECTED:
				// If an error occurs during the connect operation... 
				break;
			default:
				//printf("ConnectNamedPipe failed with %d.\n", GetLastError());
				break;
			}
		}
	}
	return(errorcode);
}
int namedpipe_wait_routine(struct xynamedpipe *pnp, struct xynamedpipe_context *contexts, unsigned int *count, unsigned int *index, unsigned int buffersize, int server, t_namedpipe_procedure procedure)
{
	struct xynamedpipe_context *pcontext = &contexts[*index];
	LPOVERLAPPED po = &pcontext->o0;
	unsigned int transferred;
	unsigned int flags;
	int errorcode;
	int disconnected;

	do
	{
		flags = 0;

		disconnected = !server;

		if (GetOverlappedResult(
			pcontext->hpipe,			// handle to pipe 
			po,					// OVERLAPPED structure 
			(LPDWORD)&transferred,		// bytes transferred 
			FALSE				// do not wait 
			))
		{
			flags |= XYNAMEDPIPE_FLAG_READ;
		}

		if (flags & XYNAMEDPIPE_FLAG_READ)
		{
			// 刚连接上的时候, transferred 是 0
			errorcode = procedure((void *)pnp, &pcontext->context, &pcontext->o1, pcontext->hpipe, XYNAMEDPIPE_READ, pcontext->buffer0, transferred);
			if (errorcode != 0)
			{
				flags = 0;
			}
		}
		else
		{
			errorcode = GetLastError();
			if (errorcode == 0)
			{
				errorcode = ERROR_UNHANDLED_EXCEPTION;
			}
		}

		if (flags == 0)
		{
			errorcode = procedure((void *)pnp, &pcontext->context, &pcontext->o1, pcontext->hpipe, XYNAMEDPIPE_CLOSE, NULL, errorcode);

			if (server)
			{
				pcontext->context = NULL;

				errorcode = namedpipe_accept(pcontext->hpipe, po, &disconnected);
				if (errorcode == 0 || errorcode == ERROR_IO_PENDING || errorcode == ERROR_PIPE_CONNECTED)
				{
					errorcode = 0;

					flags |= XYNAMEDPIPE_FLAG_ACCEPTED;
				}

				// don't need to setevent
			}
			else
			{
				// 客户端的话直接令线程退出
			}
		}

		if (flags & XYNAMEDPIPE_FLAG_READ)
		{
			if (ReadFile(
				pcontext->hpipe,
				pcontext->buffer0,
				buffersize,
				(LPDWORD)&transferred,
				po))
			{
				//
			}
			else
			{
				errorcode = GetLastError();

				if (errorcode == 0 || errorcode == ERROR_IO_PENDING)
				{
					errorcode = 0;
				}
				else
				{
					// 0 表示没有一个标志成功, 取消读成功标志
					flags = 0;
				}
			}
		}
	} while (server && errorcode == ERROR_BROKEN_PIPE);

	if (flags == 0)
	{
		namedpipe_context_uninitialize(pcontext, disconnected);

		(*count)--;
		if (*count > 0)
		{
			if (*index != *count)
			{
				memcpy(&contexts[*index], &contexts[*count], sizeof(struct xynamedpipe_context));
			}
		}
		else
		{
			errorcode = ERROR_UNHANDLED_EXCEPTION;
		}
	}
	else
	{
		(*index)++;
	}
	return(errorcode);
}
unsigned int namedpipe_endure_receive(struct xynamedpipe *pnp, struct xynamedpipe_context *contexts, unsigned int count, void **hevents, void *hquit, unsigned int buffersize, unsigned int timeout, int server, t_namedpipe_procedure p_procedure)
{
	unsigned int i, j;
	unsigned int index;
	unsigned int returnvalue;
	LPOVERLAPPED po;
	struct xynamedpipe_context *pcontext;
	int errorcode = 0;

	if (count > 0)
	{
		j = 0;
		if (hquit != NULL)
		{
			hevents[j++] = hquit;
		}

		errorcode = 0;
		while (errorcode == 0)
		{
			i = j;

			pcontext = &contexts[0];
			while (i < j + count)
			{
				po = &pcontext->o0;

				hevents[i++] = po->hEvent;

				pcontext++;
			}

			// Wait for the event object to be signaled, indicating 
			// completion of an overlapped read, write, or 
			// connect operation. 
			returnvalue = WaitForMultipleObjects(
				i,				// number of event objects 
				hevents,		// array of event objects 
				FALSE,			// does not wait for all 
				timeout);		// waits indefinitely 
			if (returnvalue >= WAIT_OBJECT_0 && returnvalue < WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS)
			{
				switch (returnvalue)
				{
				case WAIT_OBJECT_0 + 0:
					if (j != 0)
					{
						errorcode = ERROR_CANCELLED;

						break;
					}
				default:
					index = returnvalue - WAIT_OBJECT_0;
					index -= j;

					do
					{
						errorcode = namedpipe_wait_routine(pnp, contexts, &count, &index, buffersize, server, p_procedure);
						if (errorcode == 0 && index < count)
						{
							returnvalue = WaitForMultipleObjects(
								count - index,				// number of event objects 
								hevents + j + index,		// array of event objects 
								FALSE,						// does not wait for all 
								0);							// waits indefinitely 
							if (returnvalue >= WAIT_OBJECT_0 && returnvalue < WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS)
							{
								index += returnvalue - WAIT_OBJECT_0;
							}
							else
							{
								switch (returnvalue)
								{
								case WAIT_TIMEOUT:
									// 没有可用的
									index = count;
									break;
								default:
									errorcode = ERROR_CANCELLED;
									break;
								}
							}
						}
					} while (errorcode == 0 && index < count);
					break;
				}
			}
			else
			{
				switch (returnvalue)
				{
				case WAIT_IO_COMPLETION:
					continue;
				case WAIT_TIMEOUT:
					pcontext = &contexts[0];
					for (i = 0; i < count; i++)
					{
						p_procedure((void *)pnp, &pcontext->context, &pcontext->o1, pcontext->hpipe, XYNAMEDPIPE_TIMEOUT, NULL, 0);

						pcontext++;
					}

					if (!pnp->working)
					{
						errorcode = ERROR_TIMEOUT;
					}
					break;
				default:
					errorcode = ERROR_CANCELLED;
					break;
				}
			}
		}

		pcontext = &contexts[0];
		for (i = 0; i < count; i++)
		{
			p_procedure((LPVOID)pnp, &pcontext->context, &pcontext->o1, pcontext->hpipe, XYNAMEDPIPE_CLOSE, NULL, 0);

			namedpipe_context_uninitialize(pcontext, !server);

			pcontext++;
		}
	}
	return(count);
}

DWORD WINAPI namedpipe_client_proc(LPVOID parameter)
{
	struct namedpipe_client_thread_parameters *pnpctps = (struct namedpipe_client_thread_parameters *)parameter;
	struct xynamedpipe *pnp;
	HANDLE hquit;
	t_namedpipe_procedure p_procedure;
	// only for client
	HANDLE hpipe;
	void *context;
	// client count always 1
	const unsigned int maximum = 1;	// number of instances 
	unsigned int timeout;
	unsigned int count;
	unsigned int i;
	LPOVERLAPPED po0;
	LPOVERLAPPED po1;
	struct xynamedpipe_context *pcontext;
	struct xynamedpipe_context contexts[maximum];
	HANDLE hevents[maximum + 1];
	unsigned int transferred;
	int errorcode;

	pnp = pnpctps->pnp;

	hquit = pnp->hquit;

	p_procedure = pnpctps->p_procedure;

	hpipe = pnpctps->hpipe;
	context = pnpctps->context;

	count = maximum;
	timeout = pnpctps->timeout;

	// The initial loop creates several instances of a named pipe 
	// along with an event object for each instance.  An 
	// overlapped ConnectNamedPipe operation is started for 
	// each instance. 

	pcontext = &contexts[0];
	for (i = 0; i < count; i++)
	{
		po0 = &pcontext->o0;
		po1 = &pcontext->o1;

		if (namedpipe_context_initialize(pcontext))
		{
			pcontext->buffer0 = (unsigned char *)MALLOC(XYNAMEDPIPE_BUFFERSIZE);
			if (pcontext->buffer0 != NULL)
			{
				pcontext->hpipe = hpipe;
				pcontext->context = context;

				//
				SetEvent(pnpctps->hevent);
				//

				// 客户端触发连接事件
				p_procedure((LPVOID)pnp, &pcontext->context, po1, pcontext->hpipe, XYNAMEDPIPE_READ, NULL, 0);

				if (ReadFile(
					pcontext->hpipe,
					pcontext->buffer0,
					XYNAMEDPIPE_BUFFERSIZE,
					(LPDWORD)&transferred,
					po0))
				{
					errorcode = 0;
				}
				else
				{
					errorcode = GetLastError();
				}
				if (errorcode == 0 || errorcode == ERROR_IO_PENDING)
				{
					//
				}
				else
				{
					// 客户端触发关闭事件
					p_procedure((LPVOID)pnp, &pcontext->context, po1, pcontext->hpipe, XYNAMEDPIPE_CLOSE, NULL, 0);

					CloseHandle(pcontext->hpipe);
					pcontext->hpipe = INVALID_HANDLE_VALUE;

					hpipe = INVALID_HANDLE_VALUE;
				}
			}
		}

		if (pcontext->hpipe == INVALID_HANDLE_VALUE)
		{
			namedpipe_context_uninitialize(pcontext, 1);

			break;
		}

		pcontext++;
	}

	if (i == 0)
	{
		SetEvent(pnpctps->hevent);
	}

	errorcode = namedpipe_endure_receive(pnp, contexts, i, hevents, hquit, XYNAMEDPIPE_BUFFERSIZE, timeout, 0, p_procedure);

	return(errorcode);
}

DWORD WINAPI namedpipe_server_proc(LPVOID parameter)
{
	struct namedpipe_server_thread_parameters *pnpstps = (struct namedpipe_server_thread_parameters *)parameter;
	struct xynamedpipe *pnp;
	const TCHAR *pipename;
	HANDLE hquit;
	t_namedpipe_procedure p_procedure;
	const unsigned int maximum = 63;	// number of instances 
	// only for server
	DWORD openmode;
	DWORD pipemode;
	DWORD timeout;
	DWORD count;
	DWORD i;
	LPOVERLAPPED po0;
	LPOVERLAPPED po1;
	struct xynamedpipe_context *pcontext;
	struct xynamedpipe_context contexts[maximum];
	HANDLE hevents[maximum + 1];
	int errorcode;
	BOOL disconected;

	pnp = pnpstps->pnp;
	pipename = pnpstps->pipename;

	hquit = pnp->hquit;

	p_procedure = pnpstps->p_procedure;

	count = pnpstps->maximum;
	timeout = pnpstps->timeout;

	// only for server
	openmode = pnpstps->openmode;
	pipemode = pnpstps->pipemode;

	SetEvent(pnpstps->hevent);

	if (count == 0)
	{
		count = 1;
	}
	if (count > maximum)
	{
		count = maximum;
	}

	// The initial loop creates several instances of a named pipe 
	// along with an event object for each instance.  An 
	// overlapped ConnectNamedPipe operation is started for 
	// each instance. 

	pcontext = &contexts[0];
	for (i = 0; i < count; i++)
	{
		po0 = &pcontext->o0;
		po1 = &pcontext->o1;

		disconected = TRUE;

		if (namedpipe_context_initialize(pcontext))
		{
			pcontext->buffer0 = (BYTE *)MALLOC(XYNAMEDPIPE_BUFFERSIZE);
			if (pcontext->buffer0 != NULL)
			{
				SECURITY_DESCRIPTOR sd;
				SECURITY_ATTRIBUTES sa;
				InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);

				SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
				sa.nLength = sizeof(SECURITY_ATTRIBUTES);
				sa.lpSecurityDescriptor = &sd;
				sa.bInheritHandle = TRUE;

				// pipemode
				//PIPE_WAIT,               // blocking mode 
				pcontext->hpipe = CreateNamedPipe(
					pipename,				// pipe name 
					openmode |				// read/write access 
					FILE_FLAG_OVERLAPPED,   // overlapped mode 
					pipemode |
					PIPE_READMODE_BYTE,
					count,					// number of instances 
					XYNAMEDPIPE_BUFFERSIZE,	// output buffer size 
					XYNAMEDPIPE_BUFFERSIZE,	// input buffer size 
					timeout,				// client time-out 
					&sa);					// default security attributes 
				if (pcontext->hpipe != INVALID_HANDLE_VALUE)
				{
					errorcode = namedpipe_accept(pcontext->hpipe, po0, &disconected);
					if (errorcode == 0 || errorcode == ERROR_IO_PENDING || errorcode == ERROR_PIPE_CONNECTED)
					{
						if (errorcode == ERROR_PIPE_CONNECTED)
						{
							SetEvent(po0->hEvent);
						}
					}
					else
					{
						CloseHandle(pcontext->hpipe);
						pcontext->hpipe = INVALID_HANDLE_VALUE;
					}
				}
			}
		}

		if (pcontext->hpipe == INVALID_HANDLE_VALUE)
		{
			namedpipe_context_uninitialize(pcontext, disconected);

			break;
		}

		pcontext++;
	}

	errorcode = namedpipe_endure_receive(pnp, contexts, i, hevents, hquit, XYNAMEDPIPE_BUFFERSIZE, timeout, 1, p_procedure);

	return(errorcode);
}

void namedpipe_initialize(struct xynamedpipe *pnp, void *parameter)
{
	pnp->parameter = parameter;

	pnp->hquit = NULL;

	pnp->hthread = NULL;
	pnp->working = FALSE;
}

int namedpipe_listen(struct xynamedpipe *pnp, const WCHAR *pipename, unsigned int maximum, unsigned int timeout, unsigned int openmode, unsigned int pipemode, t_namedpipe_procedure p_procedure)
{
	struct namedpipe_server_thread_parameters pnpstps[1];
	int result = 0;

	pnpstps->pnp = pnp;

	pnpstps->p_procedure = p_procedure;

	pnpstps->pipename = pipename;

	pnpstps->maximum = maximum;
	pnpstps->timeout = timeout;

	pnpstps->openmode = openmode;
	pnpstps->pipemode = pipemode;

	pnpstps->hevent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (pnpstps->hevent != NULL)
	{
		pnp->working = TRUE;
		pnp->hthread = CreateThread(NULL, 0, namedpipe_server_proc, (void *)pnpstps, 0, NULL);
		if (pnp->hthread != NULL)
		{
			WaitForSingleObject(pnpstps->hevent, INFINITE);

			result = 1;
		}

		CloseHandle(pnpstps->hevent);
	}
	
	return(result);
}
void *namedpipe_connect(struct xynamedpipe *pnp, void *context, const WCHAR *pipename, unsigned int desiredaccess, unsigned int timeout, t_namedpipe_procedure p_procedure)
{
	struct namedpipe_client_thread_parameters pnpctps[1];
	HANDLE result = INVALID_HANDLE_VALUE;

	if (WaitNamedPipe(pipename, timeout))
	{
		result = CreateFile(pipename,
			desiredaccess,
			0,              // no sharing 
			NULL,           // default security attributes
			OPEN_EXISTING,  // opens existing pipe 
			FILE_FLAG_OVERLAPPED,              // overlapped mode 
			NULL);          // no template file 
		if (result != INVALID_HANDLE_VALUE)
		{
			pnpctps->pnp = pnp;

			pnpctps->p_procedure = p_procedure;

			pnpctps->timeout = timeout;

			pnpctps->hpipe = result;
			pnpctps->context = context;

			pnpctps->hevent = CreateEvent(NULL, TRUE, FALSE, NULL);
			if (pnpctps->hevent != NULL)
			{
				pnp->working = TRUE;
				pnp->hthread = CreateThread(NULL, 0, namedpipe_client_proc, (LPVOID)pnpctps, 0, NULL);
				if (pnp->hthread != NULL)
				{
					WaitForSingleObject(pnpctps->hevent, INFINITE);

					result = pnpctps->hpipe;
				}
				else
				{
					pnp->working = FALSE;
				}

				CloseHandle(pnpctps->hevent);
			}
			
			if (!pnp->working)
			{
				CloseHandle(result);
				result = INVALID_HANDLE_VALUE;
			}
		}
	}

	return(result);
}
void namedpipe_stop(struct xynamedpipe *pnp)
{
	pnp->working = FALSE;
	if (pnp->hthread != NULL)
	{
		WaitForSingleObject(pnp->hthread, INFINITE);
		CloseHandle(pnp->hthread);
		pnp->hthread = NULL;
	}
}

int namedpipe_send(struct xynamedpipe *pnp, void *_po, void *hpipe, const unsigned char *buffer, unsigned int bufferlength, unsigned int timeout)
{
	LPOVERLAPPED po = (LPOVERLAPPED)_po;
	HANDLE hevents[2];
	DWORD numberofbytes;
	DWORD returnvalue;
	DWORD i, j;
	int errorcode = 0;

	if (po)
	{
		po->Offset = 0;
		po->OffsetHigh = 0;
		po->Internal = 0;
		po->InternalHigh = 0;
		po->Pointer = NULL;
	}

	ERROR_IO_INCOMPLETE;
	if (WriteFile(hpipe, buffer, bufferlength, &numberofbytes, po))
	{
		//errorcode = 0;
	}
	else
	{
		errorcode = GetLastError();
		// The pipe is being closed.
		if (errorcode == ERROR_NO_DATA)
		{
		}
		else
		{
			// 没有必要调用 GetOverlappedResult
			if (!GetOverlappedResult(hpipe, po, &numberofbytes, FALSE))
			{
				errorcode = GetLastError();
				if (errorcode == ERROR_IO_PENDING)
				{
					errorcode = 0;
				}
			}

			if (errorcode == ERROR_IO_INCOMPLETE)
			{
				if (po != NULL)
				{
					j = 0;
					if (pnp->hquit != NULL)
					{
						hevents[j++] = pnp->hquit;
					}

					i = j;

					hevents[i++] = po->hEvent;

					// Wait for the event object to be signaled, indicating 
					// completion of an overlapped read, write, or 
					// connect operation. 
					returnvalue = WaitForMultipleObjects(
						i,				// number of event objects 
						hevents,		// array of event objects 
						FALSE,			// does not wait for all 
						timeout);		// waits indefinitely 
					if (returnvalue >= WAIT_OBJECT_0 && returnvalue < WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS)
					{
						switch (returnvalue)
						{
						case WAIT_OBJECT_0 + 0:
							if (j != 0)
							{
								errorcode = ERROR_CANCELLED;

								break;
							}
						default:
							break;
						}
					}
					else
					{
						switch (returnvalue)
						{
						case WAIT_IO_COMPLETION:
							break;
						case WAIT_TIMEOUT:
							errorcode = ERROR_TIMEOUT;
							break;
						default:
							errorcode = ERROR_CANCELLED;
							break;
						}
					}
				}
			}
		}
	}

	return(errorcode);
}
//---------------------------------------------------------------------------