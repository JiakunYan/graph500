#include "aml.h"
#include "lci.hpp"
#include <mpi.h>
#include <cassert>

struct config_t {
  bool enable_loopback = false; // Enable loopback LCI message for self-send
  size_t agg_buffer_size = 8 * 1024; // Default aggregation buffer size
} g_config;

namespace detail {
uint64_t total_num_recv = 0;
std::vector<uint64_t> total_num_sends;
std::vector<uint64_t> alltoall_buffer;

class aml_comp_t : public lci::comp_impl_t {
public:
  aml_comp_t() : lci::comp_impl_t(lci::comp_attr_t()) {
    entries.resize(8); // Initial size, can be resized later
  }
  void register_handler(int n, aml_handler_t f, int size) {
    if (n < 0 || n >= entries.size()) {
      entries.resize(2 * n + 1);
    }
    entries[n].handler = f;
    entries[n].size = size;
  }
  void signal(lci::status_t status) override {
    // fprintf(stderr, "aml_handler_t::signal called with tag=%d rank=%d\n", status.tag, status.rank);
    int idx = status.tag;
    lci::buffer_t buffer = status.get_buffer();
    assert(buffer.size % entries[idx].size == 0);
    for (size_t i = 0; i < buffer.size; i += entries[idx].size) {
      invoke(idx, status.rank, static_cast<char*>(buffer.base) + i, buffer.size);
    }
    free(buffer.base);
    ++detail::total_num_recv;
  }
  void invoke(int idx, int rank, void *data, int length) {
    // fprintf(stderr, "aml_handler_t::invoke idx=%d rank=%d length=%d\n", idx,
    // rank, length); 
    // fprintf(stderr, " data=%lx\n", *(uint64_t*)data);
    entries[idx].handler(rank, data);
  }

private:
  struct entry_t {
    aml_handler_t handler;
    int size;
  };
  std::vector<entry_t> entries;
};

aml_comp_t aml_handler_impl;
lci::comp_t aml_handler;
lci::rcomp_t rcomp;

struct message_t {
  void *buffer;
  size_t size;
  int type;
  int rank;
  message_t() : buffer(nullptr), size(0) {}
  bool is_empty() const {
    return buffer == nullptr || size == 0;
  }
};

class agg_buffer_t {
public:
  agg_buffer_t() = default;
  // agg_buffer_t(const agg_buffer_t &other) = delete; // No copy
  ~agg_buffer_t() {
    if (m_size) {
      fprintf(stderr, "agg_buffer_t destructor called but size is not zero: %zu\n", m_size);
    }
    if (m_buffer) {
      free(m_buffer); // Free the buffer if it was allocated
    }
  }

  void initialize(size_t capacity) {
    if (m_buffer) {
      free(m_buffer);
    }
    m_capacity = capacity;
    m_buffer = malloc(capacity);
    m_size = 0; // Reset size
  }
  
  message_t append(void *srcaddr, size_t length) {
    message_t ret;
    if (m_size + length > m_capacity) {
      // Not enough space, allocate a new buffer
      ret.buffer = m_buffer;
      ret.size = m_size;
      m_buffer = malloc(m_capacity);
      m_size = 0;
    }
    memcpy(static_cast<char*>(m_buffer) + m_size, srcaddr, length);
    m_size += length;
    return ret;
  }

  message_t flush() {
    message_t ret;
    if (m_size > 0) {
      ret.buffer = m_buffer;
      ret.size = m_size;
      m_buffer = malloc(m_capacity); // Allocate a new buffer for next use
      m_size = 0;         // Reset size
    }
    return ret;
  }

  bool is_empty() const {
    return m_buffer == nullptr || m_size == 0;
  }
 private:
  void *m_buffer = nullptr;
  size_t m_capacity = 0; // Capacity of the buffer
  size_t m_size = 0; // Current size of the buffer
};

class agg_buffer_manager_t {
public:
  agg_buffer_manager_t(size_t capacity) : m_capacity(capacity) {
    agg_buffers.resize(1); // Initial size, can be resized later
  }

  void initialize(int idx) {
    if (idx >= agg_buffers.size()) {
      agg_buffers.resize(idx + 1);
    }
    if (agg_buffers[idx].empty()) {
      agg_buffers[idx].resize(lci::get_rank_n());
      for (auto &buffer : agg_buffers[idx]) {
        buffer.initialize(g_config.agg_buffer_size);
      }
    }
  }

  agg_buffer_t &get_buffer(int idx, int rank) {
    if (idx >= agg_buffers.size() || rank >= agg_buffers[idx].size()) {
      throw std::out_of_range("Index out of range in agg_buffer_manager_t");
    }
    return agg_buffers[idx][rank];
  }

  std::vector<message_t> flush_all() {
    std::vector<message_t> messages;
    messages.reserve(lci::get_rank_n()); // Reserve space for efficiency
    for (size_t idx = 0; idx < agg_buffers.size(); ++idx) {
      if (agg_buffers[idx].empty()) {
        continue; // Skip empty buffers
      }
      for (int i = 0; i < agg_buffers[idx].size(); ++i) {
        int rank = (lci::get_rank_me() + i) % lci::get_rank_n();
        agg_buffer_t &buffer = agg_buffers[idx][rank];
        message_t msg = buffer.flush();
        msg.type = idx;
        msg.rank = rank;
        if (!msg.is_empty()) {
          messages.push_back(msg);
        }
      }
    }
    return messages;
  }

private:
  std::vector<std::vector<agg_buffer_t>> agg_buffers;
  size_t m_capacity;
};
agg_buffer_manager_t *g_agg_buffer_manager;
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

  detail::g_agg_buffer_manager = new detail::agg_buffer_manager_t(g_config.agg_buffer_size);
  return 0;
}

void aml_finalize(void) {
  delete detail::g_agg_buffer_manager;
  lci::g_runtime_fina();
  MPI_Finalize();
  return;
}

void aml_barrier(void) {
  // flush all messages
  auto messages = detail::g_agg_buffer_manager->flush_all();
  lci::comp_t sync = lci::alloc_sync_x().threshold(messages.size())();
  while (true) {
    bool done = true;
    for (auto &msg : messages) {
      if (msg.is_empty()) {
        continue; // Skip empty messages
      }
      lci::status_t status = lci::post_am_x(msg.rank, msg.buffer, msg.size, sync, detail::rcomp)
                     .tag(msg.type)
                     .force_zcopy(true).allow_done(false)();
      if (status.is_posted()) {
        // fprintf(stderr, "aml_barrier: posted msg type=%d rank=%d size=%zu\n",
        //        msg.type, msg.rank, msg.size);
        msg.buffer = nullptr; // Mark as sent
        msg.size = 0; // Reset size
        detail::total_num_sends[msg.rank] += 1;
      } else {
        // retry
        lci::progress();
        done = false;
      }
    }
    if (done) {
      break; // All messages posted successfully
    }
  }
  // Exchange total_num_sends with all ranks
  lci::alltoall(detail::total_num_sends.data(), detail::alltoall_buffer.data(),
                sizeof(uint64_t));
  uint64_t total_num_expected = 0;
  for (const auto &num : detail::alltoall_buffer) {
    total_num_expected += num;
  }
  // fprintf(stderr, "aml_barrier: total_num_expected=%lu total_num_recv=%lu\n",
  //         total_num_expected, detail::total_num_recv);
  while (total_num_expected > detail::total_num_recv) {
    lci::progress();
  }
  // fprintf(stderr, "aml_barrier: all messages received, total_num_recv=%lu\n",
  //         detail::total_num_recv);
  // Reset the counters
  detail::total_num_recv = 0;
  for (auto &num : detail::total_num_sends) {
    num = 0;
  }
  // can be optimized away
  lci::sync_wait(sync, nullptr);
  lci::barrier();
}

void aml_register_handler(aml_handler_t f, int size, int n) {
  detail::aml_handler_impl.register_handler(n, f, size);
  detail::g_agg_buffer_manager->initialize(n);
}

void aml_send(void *srcaddr, int type, int length, int node) {
  if (!g_config.enable_loopback && node == lci::get_rank_me()) {
    // self-send, no need to post
    detail::aml_handler_impl.invoke(type, node, srcaddr, length);
    return;
  }
  auto &agg_buffer = detail::g_agg_buffer_manager->get_buffer(type, node);
  auto message = agg_buffer.append(srcaddr, length);
  if (message.is_empty()) {
    return;
  }
  // Need to actually send the message
  lci::status_t status;
  do {
    status = lci::post_am_x(node, message.buffer, message.size,
                            lci::COMP_NULL_EXPECT_DONE_OR_RETRY, detail::rcomp)
                 .tag(type)
                 .force_zcopy(true)();
    lci::progress();
  } while (status.is_retry());
  detail::total_num_sends[node] += 1;
  // fprintf(stderr, "aml_send: posted type=%d node=%d length=%d\n",
  //        type, node, length);
}

int aml_my_pe(void) { return lci::get_rank_me(); }

int aml_n_pes(void) { return lci::get_rank_n(); }