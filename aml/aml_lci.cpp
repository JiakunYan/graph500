#include "aml.h"
#include "lci.hpp"
#include <mpi.h>

struct config_t {
  bool enable_loopback = false; // Enable loopback LCI message for self-send
} g_config;

namespace detail {
uint64_t total_num_recv = 0;
std::vector<uint64_t> total_num_sends;
std::vector<uint64_t> alltoall_buffer;

class aml_handler_t : public lci::comp_impl_t {
public:
  aml_handler_t() : lci::comp_impl_t(lci::comp_attr_t()) {
    handlers.resize(8); // Initial size, can be resized later
  }
  void register_handler(void (*f)(int, void *, int), int n) {
    if (n < 0 || n >= handlers.size()) {
      handlers.resize(2 * n + 1);
    }
    handlers[n] = f;
  }
  void signal(lci::status_t status) override {
    int idx = status.tag;
    lci::buffer_t buffer = status.get_buffer();
    invoke(idx, status.rank, buffer.base, buffer.size);
    free(buffer.base);
    ++detail::total_num_recv;
  }
  void invoke(int idx, int rank, void *data, int length) {
    // fprintf(stderr, "aml_handler_t::invoke idx=%d rank=%d length=%d", idx,
    // rank, length); fprintf(stderr, " data=%lx\n", *(uint64_t*)data);
    handlers[idx](rank, data, length);
  }

private:
  std::vector<void (*)(int, void *, int)> handlers;
};

aml_handler_t aml_handler_impl;
lci::comp_t aml_handler;
lci::rcomp_t rcomp;
} // namespace detail

int aml_init(int *argc, char ***argv) {
  int r = MPI_Init(argc, argv);
  if (r != MPI_SUCCESS)
    return r;
  lci::g_runtime_init();

  detail::aml_handler.set_impl(&detail::aml_handler_impl);
  detail::rcomp = lci::register_rcomp(detail::aml_handler);

  detail::total_num_sends.resize(lci::get_rank_n(), 0);
  detail::alltoall_buffer.resize(lci::get_rank_n(), 0);
  return 0;
}

void aml_finalize(void) {
  lci::g_runtime_fina();
  MPI_Finalize();
  return;
}

void aml_barrier(void) {
  lci::alltoall(detail::total_num_sends.data(), detail::alltoall_buffer.data(),
                sizeof(uint64_t));
  uint64_t total_num_expected = 0;
  for (const auto &num : detail::alltoall_buffer) {
    total_num_expected += num;
  }
  // fprintf(stderr, "aml_barrier: total_num_expected=%lu,
  // total_num_recv=%lu\n", total_num_expected, detail::total_num_recv);
  while (total_num_expected < detail::total_num_recv) {
    lci::progress();
  }
  detail::total_num_recv = 0;
  for (auto &num : detail::total_num_sends) {
    num = 0;
  }
  // can be optimized away
  lci::barrier();
}

void aml_register_handler(void (*f)(int, void *, int), int n) {
  detail::aml_handler_impl.register_handler(f, n);
}

void aml_send(void *srcaddr, int type, int length, int node) {
  if (!g_config.enable_loopback && node == lci::get_rank_me()) {
    // self-send, no need to post
    detail::aml_handler_impl.invoke(type, node, srcaddr, length);
    return;
  }
  lci::status_t status;
  do {
    status = lci::post_am_x(node, srcaddr, length,
                            lci::COMP_NULL_EXPECT_DONE_OR_RETRY, detail::rcomp)
                 .tag(type)
                 .force_zcopy(true)();
    lci::progress();
  } while (status.is_retry());
  detail::total_num_sends[node] += 1;
}

int aml_my_pe(void) { return lci::get_rank_me(); }

int aml_n_pes(void) { return lci::get_rank_n(); }