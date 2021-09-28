#ifndef NAMEDPIPE_H
#define NAMEDPIPE_H
//---------------------------------------------------------------------------
#include <windows.h>
//---------------------------------------------------------------------------
#define XYNAMEDPIPE_OPEN													0
#define XYNAMEDPIPE_CLOSE													1
#define XYNAMEDPIPE_TIMEOUT													2
#define XYNAMEDPIPE_READ													3
//---------------------------------------------------------------------------
typedef int (*t_namedpipe_procedure)(void *, void **, void *, void *, unsigned char, const unsigned char *, unsigned int);
//---------------------------------------------------------------------------
struct xynamedpipe
{
	void *parameter;

	void *hquit;

	void *hthread;
	int working;
};
//---------------------------------------------------------------------------
void namedpipe_initialize(struct xynamedpipe *pnp, void *parameter);

// openmode PIPE_ACCESS_DUPLEX, PIPE_ACCESS_INBOUND, PIPE_ACCESS_OUTBOUND
// pipemode PIPE_TYPE_BYTE
// 0
// server
int namedpipe_listen(struct xynamedpipe *pnp, const WCHAR *pipename, unsigned int maximum, unsigned int timeout, unsigned int openmode, unsigned int pipemode, t_namedpipe_procedure p_procedure);
// desiredaccess	GENERIC_READ, GENERIC_WRITE
void *namedpipe_connect(struct xynamedpipe *pnp, void *context, const WCHAR *pipename, unsigned int desiredaccess, unsigned int timeout, t_namedpipe_procedure p_procedure);
void namedpipe_stop(struct xynamedpipe *pnp);

int namedpipe_send(struct xynamedpipe *pnp, void *_po, void *hpipe, const unsigned char *buffer, unsigned int bufferlength, unsigned int timeout);
//---------------------------------------------------------------------------
#endif