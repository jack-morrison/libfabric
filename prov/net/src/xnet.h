/*
 * Copyright (c) 2017-2022 Intel Corporation, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <rdma/fabric.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_trigger.h>

#include <ofi.h>
#include <ofi_enosys.h>
#include <ofi_rbuf.h>
#include <ofi_list.h>
#include <ofi_signal.h>
#include <ofi_util.h>
#include <ofi_proto.h>
#include <ofi_net.h>

#include "xnet_proto.h"

#ifndef _XNET_H_
#define _XNET_H_


#define XNET_RDM_VERSION	0
#define XNET_MAX_INJECT		128
#define XNET_MAX_EVENTS		1024
#define XNET_MIN_MULTI_RECV	16384
#define XNET_PORT_MAX_RANGE	(USHRT_MAX)

extern struct fi_provider	xnet_prov;
extern struct util_prov		xnet_util_prov;
extern struct fi_fabric_attr	xnet_fabric_attr;
extern struct fi_info		xnet_srx_info;
extern struct xnet_port_range	xnet_ports;

extern int xnet_nodelay;
extern int xnet_staging_sbuf_size;
extern int xnet_prefetch_rbuf_size;
extern size_t xnet_default_tx_size;
extern size_t xnet_default_rx_size;
extern size_t xnet_zerocopy_size;

struct xnet_xfer_entry;
struct xnet_ep;
struct xnet_progress;
struct xnet_domain;


/* Lock ordering:
 * progress->list_lock - protects against rdm destruction
 * rdm->lock - protects rdm_conn lookup and access
 * progress->lock - serializes ep connection, transfers, destruction
 * cq->lock or eq->lock - protects event queues
 * TODO: simplify locking now that progress locks are available
 */

enum xnet_state {
	XNET_IDLE,
	XNET_CONNECTING,
	XNET_ACCEPTING,
	XNET_REQ_SENT,
	XNET_CONNECTED,
	XNET_DISCONNECTED,
	XNET_LISTENING,
};

#define OFI_PROV_SPECIFIC_TCP (0x7cb << 16)
enum {
	XNET_CLASS_CM = OFI_PROV_SPECIFIC_TCP,
	XNET_CLASS_PROGRESS,
};

struct xnet_port_range {
	int high;
	int low;
};

struct xnet_conn_handle {
	struct fid		fid;
	struct xnet_pep		*pep;
	SOCKET			sock;
	bool			endian_match;
};

struct xnet_pep {
	struct util_pep 	util_pep;
	struct fi_info		*info;
	struct xnet_progress	*progress;
	SOCKET			sock;
	enum xnet_state		state;
};

int xnet_listen(struct xnet_pep *pep, struct xnet_progress *progress);
void xnet_accept_sock(struct xnet_pep *pep);
void xnet_connect_done(struct xnet_ep *ep);
void xnet_req_done(struct xnet_ep *ep);
int xnet_send_cm_msg(struct xnet_ep *ep);

struct xnet_cur_rx {
	union {
		struct xnet_base_hdr	base_hdr;
		struct xnet_cq_data_hdr cq_data_hdr;
		struct xnet_tag_data_hdr tag_data_hdr;
		struct xnet_tag_hdr	tag_hdr;
		uint8_t			max_hdr[XNET_MAX_HDR];
	} hdr;
	size_t			hdr_len;
	size_t			hdr_done;
	size_t			data_left;
	struct xnet_xfer_entry	*entry;
	ssize_t			(*handler)(struct xnet_ep *ep);
};

struct xnet_cur_tx {
	size_t			data_left;
	struct xnet_xfer_entry	*entry;
};

struct xnet_srx {
	struct fid_ep		rx_fid;
	struct xnet_domain	*domain;
	struct xnet_cq		*cq;
	struct slist		rx_queue;
	struct slist		tag_queue;
	struct xnet_xfer_entry	*(*match_tag_rx)(struct xnet_srx *srx,
						 struct xnet_ep *ep,
						 uint64_t tag);

	struct ofi_bufpool	*buf_pool;
	uint64_t		op_flags;
	size_t			min_multi_recv_size;
};

int xnet_srx_context(struct fid_domain *domain, struct fi_rx_attr *attr,
		     struct fid_ep **rx_ep, void *context);

struct xnet_ep {
	struct util_ep		util_ep;
	struct ofi_bsock	bsock;
	struct xnet_cur_rx	cur_rx;
	struct xnet_cur_tx	cur_tx;
	OFI_DBG_VAR(uint8_t, tx_id)
	OFI_DBG_VAR(uint8_t, rx_id)

	struct dlist_entry	active_entry; /* protected by progress->lock */
	struct slist		rx_queue;
	struct slist		tx_queue;
	struct slist		priority_queue;
	struct slist		need_ack_queue;
	struct slist		async_queue;
	struct slist		rma_read_queue;
	int			rx_avail;
	struct xnet_srx		*srx;

	enum xnet_state		state;
	struct util_peer_addr	*peer;
	struct xnet_conn_handle *conn;
	struct xnet_cm_msg	*cm_msg;

	void (*hdr_bswap)(struct xnet_base_hdr *hdr);
	void (*report_success)(struct xnet_ep *ep, struct util_cq *cq,
			       struct xnet_xfer_entry *xfer_entry);
	bool			pollout_set;
};

struct xnet_event {
	struct slist_entry list_entry;
	struct xnet_rdm *rdm;
	uint32_t event;
	struct fi_eq_cm_entry cm_entry;
};

enum {
	XNET_CONN_INDEXED = BIT(0),
};

struct xnet_conn {
	struct xnet_ep		*ep;
	struct xnet_rdm		*rdm;
	struct util_peer_addr	*peer;
	uint32_t		remote_pid;
	int			flags;
	struct dlist_entry	loopback_entry;
};

struct xnet_rdm {
	struct util_ep		util_ep;

	struct xnet_pep		*pep;
	struct xnet_srx		*srx;

	struct index_map	conn_idx_map;
	struct dlist_entry	loopback_list;
	union ofi_sock_ip	addr;
};

int xnet_rdm_ep(struct fid_domain *domain, struct fi_info *info,
		struct fid_ep **ep_fid, void *context);
ssize_t xnet_get_conn(struct xnet_rdm *rdm, fi_addr_t dest_addr,
		      struct xnet_conn **conn);
void xnet_freeall_conns(struct xnet_rdm *rdm);

struct xnet_progress {
	struct fid		fid;
	struct ofi_genlock	lock;
	struct ofi_genlock	rdm_lock;
	struct ofi_genlock	*active_lock;

	struct dlist_entry	active_wait_list;
	struct fd_signal	signal;

	struct slist		event_list;
	struct ofi_bufpool	*xfer_pool;

	/* epoll works better for apps that wait on the fd,
	 * but tests show that poll performs better
	 */
	bool			use_epoll;
	union {
		struct ofi_pollfds *pollfds;
		ofi_epoll_t	epoll;
	};

	int (*poll_wait)(struct xnet_progress *progress,
			struct ofi_epollfds_event *events, int max_events,
			int timeout);
	int (*poll_add)(struct xnet_progress *progress, int fd, uint32_t events,
			void *context);
	void (*poll_mod)(struct xnet_progress *progress, int fd, uint32_t events,
			void *context);
	int (*poll_del)(struct xnet_progress *progress, int fd);
	void (*poll_close)(struct xnet_progress *progress);

	pthread_t		thread;
	bool			auto_progress;
};

int xnet_init_progress(struct xnet_progress *progress, struct fi_info *info);
void xnet_close_progress(struct xnet_progress *progress);
int xnet_start_progress(struct xnet_progress *progress);
void xnet_stop_progress(struct xnet_progress *progress);

void xnet_progress(struct xnet_progress *progress, bool internal);
void xnet_run_progress(struct xnet_progress *progress, bool internal);
void xnet_run_conn(struct xnet_conn_handle *conn, bool pin, bool pout, bool perr);
void xnet_handle_events(struct xnet_progress *progress);

int xnet_trywait(struct fid_fabric *fid_fabric, struct fid **fids, int count);
void xnet_update_poll(struct xnet_ep *ep);
int xnet_monitor_sock(struct xnet_progress *progress, SOCKET sock,
		      uint32_t events, struct fid *fid);
void xnet_halt_sock(struct xnet_progress *progress, SOCKET sock);

static inline int xnet_progress_locked(struct xnet_progress *progress)
{
	return ofi_genlock_held(progress->active_lock);
}


struct xnet_fabric {
	struct util_fabric	util_fabric;
	struct xnet_progress	progress;
};

int xnet_start_all(struct xnet_fabric *fabric);
void xnet_progress_all(struct xnet_fabric *fabric);

static inline void xnet_signal_progress(struct xnet_progress *progress)
{
	if (progress->auto_progress)
		fd_signal_set(&progress->signal);
}


/* xnet_xfer_entry::ctrl_flags */
#define XNET_NEED_RESP		BIT(1)
#define XNET_NEED_ACK		BIT(2)
#define XNET_INTERNAL_XFER	BIT(3)
#define XNET_NEED_DYN_RBUF 	BIT(4)
#define XNET_ASYNC		BIT(5)
#define XNET_INJECT_OP		BIT(6)
#define XNET_MULTI_RECV		FI_MULTI_RECV /* BIT(16) */

struct xnet_xfer_entry {
	struct slist_entry	entry;
	union {
		struct xnet_base_hdr	base_hdr;
		struct xnet_cq_data_hdr cq_data_hdr;
		struct xnet_tag_data_hdr tag_data_hdr;
		struct xnet_tag_hdr	tag_hdr;
		uint8_t		       	max_hdr[XNET_MAX_HDR + XNET_MAX_INJECT];
	} hdr;
	size_t			iov_cnt;
	struct iovec		iov[XNET_IOV_LIMIT+1];
	struct xnet_ep		*ep;
	uint64_t		tag;
	uint64_t		ignore;
	fi_addr_t		src_addr;
	uint64_t		cq_flags;
	uint32_t		ctrl_flags;
	uint32_t		async_index;
	void			*context;
	// for RMA read requests, we need a way to track the request response
	// so that we don't propagate multiple completions for the same operation
	struct xnet_xfer_entry  *resp_entry;
};

struct xnet_domain {
	struct util_domain		util_domain;
	struct xnet_progress		progress;
};

static inline struct xnet_progress *xnet_ep2_progress(struct xnet_ep *ep)
{
	struct xnet_domain *domain;
	domain = container_of(ep->util_ep.domain, struct xnet_domain,
			      util_domain);
	return &domain->progress;
}

static inline struct xnet_progress *xnet_rdm2_progress(struct xnet_rdm *rdm)
{
	struct xnet_domain *domain;
	domain = container_of(rdm->util_ep.domain, struct xnet_domain,
			      util_domain);
	return &domain->progress;
}

static inline struct xnet_progress *xnet_srx2_progress(struct xnet_srx *srx)
{
	return &srx->domain->progress;
}

struct xnet_cq {
	struct util_cq		util_cq;
	struct ofi_bufpool	*xfer_pool;
};

static inline struct xnet_progress *xnet_cq2_progress(struct xnet_cq *cq)
{
	struct xnet_domain *domain;
	domain = container_of(cq->util_cq.domain, struct xnet_domain,
			      util_domain);
	return &domain->progress;
}

/* xnet_cntr maps directly to util_cntr */

static inline struct xnet_progress *xnet_cntr2_progress(struct util_cntr *cntr)
{
	struct xnet_domain *domain;
	domain = container_of(cntr->domain, struct xnet_domain, util_domain);
	return &domain->progress;
}

struct xnet_eq {
	struct util_eq		util_eq;
	/*
	  The following lock avoids race between ep close
	  and connection management code.
	 */
	ofi_mutex_t		close_lock;
};

static inline struct xnet_progress *xnet_eq2_progress(struct xnet_eq *eq)
{
	struct xnet_fabric *fabric ;
	fabric = container_of(eq->util_eq.fabric, struct xnet_fabric,
			      util_fabric);
	return &fabric->progress;
}

int xnet_eq_write(struct util_eq *eq, uint32_t event,
		  const void *buf, size_t len, uint64_t flags);

int xnet_create_fabric(struct fi_fabric_attr *attr,
		       struct fid_fabric **fabric,
		       void *context);

int xnet_passive_ep(struct fid_fabric *fabric, struct fi_info *info,
		    struct fid_pep **pep, void *context);

int xnet_set_port_range(void);

int xnet_domain_open(struct fid_fabric *fabric, struct fi_info *info,
		     struct fid_domain **domain, void *context);


int xnet_setup_socket(SOCKET sock, struct fi_info *info);
void xnet_set_zerocopy(SOCKET sock);

int xnet_endpoint(struct fid_domain *domain, struct fi_info *info,
		  struct fid_ep **ep_fid, void *context);
void xnet_ep_disable(struct xnet_ep *ep, int cm_err, void* err_data,
		     size_t err_data_size);


int xnet_cq_open(struct fid_domain *domain, struct fi_cq_attr *attr,
		 struct fid_cq **cq_fid, void *context);
void xnet_report_success(struct xnet_ep *ep, struct util_cq *cq,
			 struct xnet_xfer_entry *xfer_entry);
void xnet_cq_report_error(struct util_cq *cq,
			  struct xnet_xfer_entry *xfer_entry,
			  int err);
void xnet_get_cq_info(struct xnet_xfer_entry *entry, uint64_t *flags,
		      uint64_t *data, uint64_t *tag);
int xnet_cntr_open(struct fid_domain *fid_domain, struct fi_cntr_attr *attr,
		   struct fid_cntr **cntr_fid, void *context);
void xnet_report_cntr_success(struct xnet_ep *ep, struct util_cq *cq,
			      struct xnet_xfer_entry *xfer_entry);
void xnet_cntr_incerr(struct xnet_ep *ep, struct xnet_xfer_entry *xfer_entry);

void xnet_reset_rx(struct xnet_ep *ep);

void xnet_progress_rx(struct xnet_ep *ep);
void xnet_progress_async(struct xnet_ep *ep);

void xnet_hdr_none(struct xnet_base_hdr *hdr);
void xnet_hdr_bswap(struct xnet_base_hdr *hdr);

void xnet_tx_queue_insert(struct xnet_ep *ep,
			  struct xnet_xfer_entry *tx_entry);

int xnet_eq_create(struct fid_fabric *fabric_fid, struct fi_eq_attr *attr,
		   struct fid_eq **eq_fid, void *context);


static inline void
xnet_set_ack_flags(struct xnet_xfer_entry *xfer, uint64_t flags)
{
	if (flags & (FI_TRANSMIT_COMPLETE | FI_DELIVERY_COMPLETE)) {
		xfer->hdr.base_hdr.flags |= XNET_DELIVERY_COMPLETE;
		xfer->ctrl_flags |= XNET_NEED_ACK;
	}
}

static inline void
xnet_set_commit_flags(struct xnet_xfer_entry *xfer, uint64_t flags)
{
	xnet_set_ack_flags(xfer, flags);
	if (flags & FI_COMMIT_COMPLETE) {
		xfer->hdr.base_hdr.flags |= XNET_COMMIT_COMPLETE;
		xfer->ctrl_flags |= XNET_NEED_ACK;
	}
}

static inline uint64_t
xnet_tx_completion_flag(struct xnet_ep *ep, uint64_t op_flags)
{
	/* Generate a completion if op flags indicate or we generate
	 * completions by default
	 */
	return (ep->util_ep.tx_op_flags | op_flags) & FI_COMPLETION;
}

static inline uint64_t
xnet_rx_completion_flag(struct xnet_ep *ep, uint64_t op_flags)
{
	/* Generate a completion if op flags indicate or we generate
	 * completions by default
	 */
	return (ep->util_ep.rx_op_flags | op_flags) & FI_COMPLETION;
}

static inline struct xnet_xfer_entry *
xnet_alloc_xfer(struct xnet_progress *progress)
{
	assert(xnet_progress_locked(progress));
	return ofi_buf_alloc(progress->xfer_pool);
}

static inline void
xnet_free_xfer(struct xnet_ep *ep, struct xnet_xfer_entry *xfer)
{
	assert(xnet_progress_locked(xnet_ep2_progress(ep)));
	xfer->hdr.base_hdr.flags = 0;
	xfer->cq_flags = 0;
	xfer->ctrl_flags = 0;
	xfer->context = 0;
	ofi_buf_free(xfer);
}

static inline struct xnet_xfer_entry *
xnet_alloc_rx(struct xnet_ep *ep)
{
	struct xnet_xfer_entry *xfer;

	assert(xnet_progress_locked(xnet_ep2_progress(ep)));
	xfer = xnet_alloc_xfer(xnet_ep2_progress(ep));
	if (xfer)
		xfer->ep = ep;

	return xfer;
}

static inline struct xnet_xfer_entry *
xnet_alloc_tx(struct xnet_ep *ep)
{
	struct xnet_xfer_entry *xfer;

	assert(xnet_progress_locked(xnet_ep2_progress(ep)));
	xfer = xnet_alloc_xfer(xnet_ep2_progress(ep));
	if (xfer) {
		xfer->hdr.base_hdr.version = XNET_HDR_VERSION;
		xfer->hdr.base_hdr.op_data = 0;
		xfer->ep = ep;
	}

	return xfer;
}

/* If we've buffered receive data, it counts the same as if a POLLIN
 * event were set, and we need to process the data.
 * We also need to progress receives in the case where we're waiting
 * on the application to post a buffer to consume a receive
 * that we've already read from the kernel.  If the message is
 * of length 0, there's no additional data to read, so calling
 * poll without forcing progress can result in application hangs.
 */
static inline bool xnet_active_wait(struct xnet_ep *ep)
{
	assert(xnet_progress_locked(xnet_ep2_progress(ep)));
	return ofi_bsock_readable(&ep->bsock) ||
	       (ep->cur_rx.handler && !ep->cur_rx.entry);
}

#define XNET_WARN_ERR(subsystem, log_str, err) \
	FI_WARN(&xnet_prov, subsystem, log_str "%s (%d)\n", \
		fi_strerror((int) -(err)), (int) err)

#endif //_XNET_H_
