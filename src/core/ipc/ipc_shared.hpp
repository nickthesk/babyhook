/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/core/ipc/ipc_shared.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef CAT_IPC_SHARED_HPP
#define CAT_IPC_SHARED_HPP

#include "core/ipc/ipc_protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cat_ipc
{

class shared_memory
{
public:
  shared_memory() = default;

  shared_memory(const shared_memory&) = delete;
  auto operator=(const shared_memory&) -> shared_memory& = delete;

  shared_memory(shared_memory&& other) noexcept
  {
    move_from(other);
  }

  auto operator=(shared_memory&& other) noexcept -> shared_memory&
  {
    if (this != &other)
    {
      close();
      move_from(other);
    }

    return *this;
  }

  ~shared_memory()
  {
    close();
  }

  [[nodiscard]] static auto create_server(bool reset_existing) -> shared_memory
  {
    if (reset_existing)
    {
      force_unlink_shared_object();
    }

    auto memory = shared_memory{};
    for (auto attempt = 0; attempt < 5; ++attempt)
    {
      if (reset_existing)
      {
        force_unlink_shared_object();
      }

      memory.fd_ = shm_open(shared_memory_name, O_CREAT | O_EXCL | O_RDWR, 0666);
      if (memory.fd_ >= 0)
      {
        break;
      }

      if (errno != EEXIST)
      {
        throw std::runtime_error(std::string{"shm_open create failed: "} + std::strerror(errno));
      }

      force_unlink_shared_object();
      memory.fd_ = -1;
    }

    if (memory.fd_ < 0)
    {
      throw std::runtime_error("shm_open create failed: stale shared memory could not be removed");
    }

    (void)fchmod(memory.fd_, 0666);

    if (ftruncate(memory.fd_, static_cast<off_t>(sizeof(shared_state))) != 0)
    {
      throw std::runtime_error(std::string{"ftruncate failed: "} + std::strerror(errno));
    }

    memory.map();
    memory.owner_ = true;
    memory.initialize_state();
    return memory;
  }

  [[nodiscard]] static auto open_or_create_server(bool reset_existing) -> shared_memory
  {
    if (reset_existing)
    {
      return create_server(true);
    }

    try
    {
      return open_client();
    }
    catch (const std::exception&)
    {
      return create_server(true);
    }
  }

  [[nodiscard]] static auto open_client() -> shared_memory
  {
    auto memory = shared_memory{};
    memory.fd_ = shm_open(shared_memory_name, O_RDWR, 0666);
    if (memory.fd_ < 0)
    {
      throw std::runtime_error(std::string{"shm_open open failed: "} + std::strerror(errno));
    }

    memory.map();
    if (memory.state_->global_data.magic_number != cathook_magic_number)
    {
      throw std::runtime_error("catbot ipc protocol mismatch");
    }

    return memory;
  }

  void close()
  {
    if (state_ != nullptr)
    {
      munmap(state_, sizeof(shared_state));
      state_ = nullptr;
    }

    if (fd_ >= 0)
    {
      ::close(fd_);
      fd_ = -1;
    }
  }

  void unlink_if_owner()
  {
    if (owner_)
    {
      shm_unlink(shared_memory_name);
      owner_ = false;
    }
  }

  [[nodiscard]] auto state() const -> shared_state*
  {
    return state_;
  }

  [[nodiscard]] bool owns_valid_state() const
  {
    return state_ != nullptr && state_->global_data.magic_number == cathook_magic_number;
  }

  [[nodiscard]] bool maps_current_object() const
  {
    if (fd_ < 0 || state_ == nullptr)
    {
      return false;
    }

    struct stat mapped_stat {};
    if (fstat(fd_, &mapped_stat) != 0)
    {
      return false;
    }

    struct stat named_stat {};
    const auto named_path = std::string{"/dev/shm"} + shared_memory_name;
    if (stat(named_path.c_str(), &named_stat) != 0)
    {
      return false;
    }

    return mapped_stat.st_dev == named_stat.st_dev && mapped_stat.st_ino == named_stat.st_ino;
  }

private:
  static void force_unlink_shared_object()
  {
    shm_unlink(shared_memory_name);
    const auto fs_path = std::string{"/dev/shm"} + shared_memory_name;
    ::unlink(fs_path.c_str());
  }

  void map()
  {
    auto* ptr = mmap(nullptr, sizeof(shared_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr == MAP_FAILED)
    {
      throw std::runtime_error(std::string{"mmap failed: "} + std::strerror(errno));
    }

    state_ = static_cast<shared_state*>(ptr);
  }

  void initialize_state()
  {
    std::memset(state_, 0, sizeof(shared_state));

    auto attributes = pthread_mutexattr_t{};
    if (pthread_mutexattr_init(&attributes) != 0)
    {
      throw std::runtime_error("pthread_mutexattr_init failed");
    }

    if (pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED) != 0)
    {
      pthread_mutexattr_destroy(&attributes);
      throw std::runtime_error("pthread_mutexattr_setpshared failed");
    }

    if (pthread_mutex_init(&state_->mutex, &attributes) != 0)
    {
      pthread_mutexattr_destroy(&attributes);
      throw std::runtime_error("pthread_mutex_init failed");
    }

    pthread_mutexattr_destroy(&attributes);

    state_->global_data.magic_number = cathook_magic_number;
    for (auto& peer : state_->peer_data)
    {
      peer.free = true;
    }
  }

  void move_from(shared_memory& other) noexcept
  {
    fd_ = other.fd_;
    state_ = other.state_;
    owner_ = other.owner_;
    other.fd_ = -1;
    other.state_ = nullptr;
    other.owner_ = false;
  }

  int fd_ = -1;
  shared_state* state_ = nullptr;
  bool owner_ = false;
};

class scoped_lock
{
public:
  explicit scoped_lock(shared_state* state) : state_{state}
  {
    if (state_ != nullptr)
    {
      pthread_mutex_lock(&state_->mutex);
    }
  }

  scoped_lock(const scoped_lock&) = delete;
  auto operator=(const scoped_lock&) -> scoped_lock& = delete;

  ~scoped_lock()
  {
    if (state_ != nullptr)
    {
      pthread_mutex_unlock(&state_->mutex);
    }
  }

private:
  shared_state* state_ = nullptr;
};

class try_scoped_lock
{
public:
  explicit try_scoped_lock(shared_state* state) : state_{state}
  {
    locked_ = state_ != nullptr && pthread_mutex_trylock(&state_->mutex) == 0;
  }

  try_scoped_lock(const try_scoped_lock&) = delete;
  auto operator=(const try_scoped_lock&) -> try_scoped_lock& = delete;

  ~try_scoped_lock()
  {
    if (locked_)
    {
      pthread_mutex_unlock(&state_->mutex);
    }
  }

  [[nodiscard]] bool locked() const
  {
    return locked_;
  }

private:
  shared_state* state_ = nullptr;
  bool locked_ = false;
};

inline auto read_host_pid() -> pid_t
{
  std::ifstream status_file{"/proc/self/status"};
  std::string line{};
  while (std::getline(status_file, line))
  {
    if (line.compare(0, 6, "NSpid:") != 0)
    {
      continue;
    }

    // NSpid: <host_pid> [<ns1_pid> [<ns2_pid> ...]]
    // The first PID after the label is the host-visible PID.
    std::istringstream stream{line.substr(6)};
    pid_t host_pid = 0;
    if (stream >> host_pid && host_pid > 0)
    {
      return host_pid;
    }

    break;
  }

  return getpid();
}

inline auto now_seconds() -> std::time_t
{
  return std::time(nullptr);
}

inline auto read_process_start_time(pid_t pid) -> unsigned long
{
  std::ifstream stat_file{"/proc/" + std::to_string(pid) + "/stat"};
  std::string stat{};
  std::getline(stat_file, stat);
  const auto close_paren = stat.rfind(')');
  if (close_paren == std::string::npos || close_paren + 2 >= stat.size())
  {
    return 0;
  }

  std::istringstream stream{stat.substr(close_paren + 2)};
  std::string token{};
  for (int index = 0; stream >> token; ++index)
  {
    if (index == 19)
    {
      return static_cast<unsigned long>(std::strtoul(token.c_str(), nullptr, 10));
    }
  }

  return 0;
}

inline void copy_cstr(char* destination, std::size_t destination_size, std::string_view source)
{
  if (destination == nullptr || destination_size == 0)
  {
    return;
  }

  const auto copy_size = std::min(source.size(), destination_size - 1);
  std::memcpy(destination, source.data(), copy_size);
  destination[copy_size] = '\0';
}

inline auto peer_alive(const peer_data_s& peer, std::time_t now = now_seconds()) -> bool
{
  return !peer.free && peer.heartbeat != 0 && now - peer.heartbeat < peer_dead_seconds;
}

inline auto command_payload(shared_state* state, const command_s& command) -> const char*
{
  if (state == nullptr || command.payload_size == 0 || command.payload_offset >= command_pool_size)
  {
    return nullptr;
  }

  return reinterpret_cast<const char*>(state->pool + command.payload_offset);
}

inline auto queue_command(shared_state* state, int target_peer, unsigned int command_type, std::string_view data, int sender = -1) -> bool
{
  if (state == nullptr)
  {
    return false;
  }

  scoped_lock lock{state};
  auto& command = state->commands[++state->command_count % command_ring_size];
  std::memset(&command, 0, sizeof(command));

  command.command_number = static_cast<unsigned int>(state->command_count);
  command.target_peer = target_peer;
  command.sender = sender;
  command.cmd_type = command_type;

  if (data.size() < command_data_size)
  {
    std::memcpy(command.cmd_data, data.data(), data.size());
    command.cmd_data[data.size()] = '\0';
    return true;
  }

  const auto slot = command.command_number % command_ring_size;
  const auto payload_offset = slot * command_payload_size;
  const auto payload_size = std::min<std::size_t>(data.size() + 1, command_payload_size);
  std::memcpy(state->pool + payload_offset, data.data(), payload_size - 1);
  state->pool[payload_offset + payload_size - 1] = '\0';
  command.payload_offset = payload_offset;
  command.payload_size = static_cast<unsigned int>(payload_size);
  return true;
}

inline void sweep_dead_peers(shared_state* state)
{
  if (state == nullptr)
  {
    return;
  }

  scoped_lock lock{state};
  auto count = 0u;
  const auto now = now_seconds();
  for (auto& peer : state->peer_data)
  {
    if (!peer.free && !peer_alive(peer, now))
    {
      peer.free = true;
    }

    if (!peer.free)
    {
      ++count;
    }
  }

  state->peer_count = count;
}

inline auto find_peer_by_start_time(shared_state* state, unsigned long start_time) -> std::optional<int>
{
  if (state == nullptr || start_time == 0)
  {
    return std::nullopt;
  }

  scoped_lock lock{state};
  for (auto index = 0u; index < max_peers; ++index)
  {
    const auto& peer = state->peer_data[index];
    if (peer_alive(peer) && peer.starttime == start_time)
    {
      return static_cast<int>(index);
    }
  }

  return std::nullopt;
}

} // namespace cat_ipc

#endif
