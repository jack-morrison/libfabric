#ifndef EFA_UNIT_TESTS_H
#define EFA_UNIT_TESTS_H

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "stdio.h"
#include "efa.h"
#include "rxr.h"
#include "efa_unit_test_mocks.h"

struct efa_resource {
	struct fi_info *hints;
	struct fi_info *info;
	struct fid_fabric *fabric;
	struct fid_domain *domain;
	struct fid_ep *ep;
	struct fid_eq *eq;
	struct fid_av *av;
	struct fid_cq *cq;
};

int efa_unit_test_resource_construct(struct efa_resource *resource, enum fi_ep_type ep_type);

void efa_unit_test_resource_destruct(struct efa_resource *resource);

struct efa_unit_test_buff {
	uint8_t *buff;
	size_t  size;
	struct fid_mr *mr;
};

void efa_unit_test_buff_construct(struct efa_unit_test_buff *buff, struct efa_resource *resource, size_t buff_size);

void efa_unit_test_buff_destruct(struct efa_unit_test_buff *buff);

/* test cases */
void test_av_insert_duplicate_raw_addr();
void test_av_insert_duplicate_gid();
void test_efa_device_construct_error_handling();
void test_rxr_ep_pkt_pool_flags();
void test_rxr_ep_pkt_pool_page_alignment();
void test_rxr_ep_dc_atomic_error_handling();
void test_dgram_cq_read_empty_cq();
void test_dgram_cq_read_bad_wc_status();
void test_rdm_cq_read_empty_cq();
void test_rdm_cq_read_failed_poll();
void test_rdm_cq_read_bad_send_status();
void test_rdm_cq_read_bad_recv_status();

#endif
