/*
 *  Copyright (c) 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: digest.cppm
 *  - CopyrightYear: 2026
 */
module;

#include <cstdint>
#if defined(_MSC_VER)
#define WINUXCMD_DIGEST_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define WINUXCMD_DIGEST_FORCEINLINE inline __attribute__((always_inline))
#else
#define WINUXCMD_DIGEST_FORCEINLINE inline
#endif

export module utils:digest;
import std;
import :console;
import :file_io;
import :native_path;

export namespace portable_digest {

enum class HashAlgorithm {
  Md5,
  Sha1,
  Sha224,
  Sha256,
  Sha384,
  Sha512,
  Blake2b,
  Sm3,
  // [GNU] cksum -a crc32b (coreutils 9.x): the reflected CRC-32 used by
  // zlib, rendered through the digest output machinery.
  Crc32b
};

struct PosixCksumResult {
  uint32_t checksum = 0;
  uint64_t bytes = 0;
};

namespace detail {

// [GNU] strerror-style reason for a failed input open(): "Is a directory"
// for a directory (EISDIR), "Not a directory" when any path component —
// including a trailing '/' on a regular file — is a non-directory
// (ENOTDIR), "Permission denied" for an existing file that cannot be
// opened (EACCES), otherwise "No such file or directory" (ENOENT).
auto open_failure_reason(std::string_view path) -> std::string {
  std::error_code ec;
  const std::filesystem::path fs_path = std::filesystem::u8path(path);
  if (std::filesystem::is_directory(fs_path, ec) && !ec) {
    return "Is a directory";
  }
  ec.clear();
  if (std::filesystem::exists(fs_path, ec) && !ec) {
    return "Permission denied";
  }
  // Walk the ancestors: the deepest existing component decides between
  // ENOTDIR (it is not a directory) and ENOENT.
  for (std::filesystem::path dir = fs_path.parent_path(); !dir.empty();
       dir = dir.parent_path()) {
    ec.clear();
    if (std::filesystem::exists(dir, ec) && !ec) {
      ec.clear();
      return std::filesystem::is_directory(dir, ec) && !ec
                 ? "No such file or directory"
                 : "Not a directory";
    }
    if (dir == dir.parent_path()) break;  // reached a filesystem root
  }
  return "No such file or directory";
}

auto input_open_error(std::string_view path) -> std::string {
  return std::string(path) + ": " + open_failure_reason(path);
}

auto to_hex(std::span<const uint8_t> bytes) -> std::string {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (uint8_t byte : bytes) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0x0F]);
  }
  return out;
}

WINUXCMD_DIGEST_FORCEINLINE auto load_be32(const uint8_t* p) -> uint32_t {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

WINUXCMD_DIGEST_FORCEINLINE auto load_le32(const uint8_t* p) -> uint32_t {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

WINUXCMD_DIGEST_FORCEINLINE auto store_le32(uint8_t* p, uint32_t v) -> void {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

WINUXCMD_DIGEST_FORCEINLINE auto rotl32(uint32_t value, unsigned bits)
    -> uint32_t {
  return (value << bits) | (value >> (32 - bits));
}

WINUXCMD_DIGEST_FORCEINLINE auto load_be64(const uint8_t* p) -> uint64_t {
  return (static_cast<uint64_t>(p[0]) << 56) |
         (static_cast<uint64_t>(p[1]) << 48) |
         (static_cast<uint64_t>(p[2]) << 40) |
         (static_cast<uint64_t>(p[3]) << 32) |
         (static_cast<uint64_t>(p[4]) << 24) |
         (static_cast<uint64_t>(p[5]) << 16) |
         (static_cast<uint64_t>(p[6]) << 8) | static_cast<uint64_t>(p[7]);
}

WINUXCMD_DIGEST_FORCEINLINE auto load_le64(const uint8_t* p) -> uint64_t {
  return static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
         (static_cast<uint64_t>(p[2]) << 16) |
         (static_cast<uint64_t>(p[3]) << 24) |
         (static_cast<uint64_t>(p[4]) << 32) |
         (static_cast<uint64_t>(p[5]) << 40) |
         (static_cast<uint64_t>(p[6]) << 48) |
         (static_cast<uint64_t>(p[7]) << 56);
}

WINUXCMD_DIGEST_FORCEINLINE auto store_be32(uint8_t* p, uint32_t v) -> void {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

WINUXCMD_DIGEST_FORCEINLINE auto store_be64(uint8_t* p, uint64_t v) -> void {
  p[0] = static_cast<uint8_t>(v >> 56);
  p[1] = static_cast<uint8_t>(v >> 48);
  p[2] = static_cast<uint8_t>(v >> 40);
  p[3] = static_cast<uint8_t>(v >> 32);
  p[4] = static_cast<uint8_t>(v >> 24);
  p[5] = static_cast<uint8_t>(v >> 16);
  p[6] = static_cast<uint8_t>(v >> 8);
  p[7] = static_cast<uint8_t>(v);
}

WINUXCMD_DIGEST_FORCEINLINE auto store_le64(uint8_t* p, uint64_t v) -> void {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
  p[4] = static_cast<uint8_t>(v >> 32);
  p[5] = static_cast<uint8_t>(v >> 40);
  p[6] = static_cast<uint8_t>(v >> 48);
  p[7] = static_cast<uint8_t>(v >> 56);
}

WINUXCMD_DIGEST_FORCEINLINE auto rotr32(uint32_t value, unsigned bits)
    -> uint32_t {
  return (value >> bits) | (value << (32 - bits));
}

WINUXCMD_DIGEST_FORCEINLINE auto rotr64(uint64_t value, unsigned bits)
    -> uint64_t {
  return (value >> bits) | (value << (64 - bits));
}

class Sha256Family {
 public:
  Sha256Family(std::array<uint32_t, 8> init, size_t out_bytes)
      : state_(init), out_bytes_(out_bytes) {}

  auto update(std::span<const uint8_t> input) -> void {
    total_bytes_ += input.size();
    if (buffered_ != 0) {
      size_t take = std::min(input.size(), block_.size() - buffered_);
      std::copy_n(input.data(), take, block_.data() + buffered_);
      buffered_ += take;
      input = input.subspan(take);
      if (buffered_ == block_.size()) {
        compress(block_.data());
        buffered_ = 0;
      }
    }

    while (input.size() >= block_.size()) {
      compress(input.data());
      input = input.subspan(block_.size());
    }

    if (!input.empty()) {
      std::copy_n(input.data(), input.size(), block_.data());
      buffered_ = input.size();
    }
  }

  auto final() -> std::vector<uint8_t> {
    uint64_t bit_len = total_bytes_ * 8;
    block_[buffered_++] = 0x80;
    if (buffered_ > 56) {
      std::fill(block_.begin() + buffered_, block_.end(), 0);
      compress(block_.data());
      buffered_ = 0;
    }
    std::fill(block_.begin() + buffered_, block_.begin() + 56, 0);
    store_be64(block_.data() + 56, bit_len);
    compress(block_.data());

    std::array<uint8_t, 32> full{};
    for (size_t i = 0; i < state_.size(); ++i) {
      store_be32(full.data() + i * 4, state_[i]);
    }
    return {full.begin(),
            full.begin() + static_cast<std::ptrdiff_t>(out_bytes_)};
  }

 private:
  static constexpr std::array<uint32_t, 64> k{
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
      0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
      0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
      0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
      0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
      0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
      0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
      0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
      0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
      0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

  auto compress(const uint8_t* block) -> void {
    std::array<uint32_t, 64> w{};
    for (size_t i = 0; i < 16; ++i) {
      w[i] = load_be32(block + i * 4);
    }
    for (size_t i = 16; i < 64; ++i) {
      uint32_t s0 =
          rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 =
          rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t i = 0; i < 64; ++i) {
      uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + s1 + ch + k[i] + w[i];
      uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint32_t, 8> state_{};
  std::array<uint8_t, 64> block_{};
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
  size_t out_bytes_ = 32;
};

class Sha512Family {
 public:
  Sha512Family(std::array<uint64_t, 8> init, size_t out_bytes)
      : state_(init), out_bytes_(out_bytes) {}

  auto update(std::span<const uint8_t> input) -> void {
    uint64_t old_low = total_low_;
    total_low_ += static_cast<uint64_t>(input.size());
    if (total_low_ < old_low) {
      ++total_high_;
    }

    if (buffered_ != 0) {
      size_t take = std::min(input.size(), block_.size() - buffered_);
      std::copy_n(input.data(), take, block_.data() + buffered_);
      buffered_ += take;
      input = input.subspan(take);
      if (buffered_ == block_.size()) {
        compress(block_.data());
        buffered_ = 0;
      }
    }

    while (input.size() >= block_.size()) {
      compress(input.data());
      input = input.subspan(block_.size());
    }

    if (!input.empty()) {
      std::copy_n(input.data(), input.size(), block_.data());
      buffered_ = input.size();
    }
  }

  auto final() -> std::vector<uint8_t> {
    uint64_t bit_high = (total_high_ << 3) | (total_low_ >> 61);
    uint64_t bit_low = total_low_ << 3;

    block_[buffered_++] = 0x80;
    if (buffered_ > 112) {
      std::fill(block_.begin() + buffered_, block_.end(), 0);
      compress(block_.data());
      buffered_ = 0;
    }
    std::fill(block_.begin() + buffered_, block_.begin() + 112, 0);
    store_be64(block_.data() + 112, bit_high);
    store_be64(block_.data() + 120, bit_low);
    compress(block_.data());

    std::array<uint8_t, 64> full{};
    for (size_t i = 0; i < state_.size(); ++i) {
      store_be64(full.data() + i * 8, state_[i]);
    }
    return {full.begin(),
            full.begin() + static_cast<std::ptrdiff_t>(out_bytes_)};
  }

 private:
  static constexpr std::array<uint64_t, 80> k{
      0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
      0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
      0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
      0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
      0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
      0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
      0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
      0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
      0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
      0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
      0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
      0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
      0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
      0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
      0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
      0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
      0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
      0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
      0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
      0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
      0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
      0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
      0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
      0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
      0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
      0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
      0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

  auto compress(const uint8_t* block) -> void {
    std::array<uint64_t, 80> w{};
    for (size_t i = 0; i < 16; ++i) {
      w[i] = load_be64(block + i * 8);
    }
    for (size_t i = 16; i < 80; ++i) {
      uint64_t s0 =
          rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
      uint64_t s1 =
          rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint64_t a = state_[0];
    uint64_t b = state_[1];
    uint64_t c = state_[2];
    uint64_t d = state_[3];
    uint64_t e = state_[4];
    uint64_t f = state_[5];
    uint64_t g = state_[6];
    uint64_t h = state_[7];

    for (size_t i = 0; i < 80; ++i) {
      uint64_t s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
      uint64_t ch = (e & f) ^ ((~e) & g);
      uint64_t temp1 = h + s1 + ch + k[i] + w[i];
      uint64_t s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
      uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint64_t temp2 = s0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint64_t, 8> state_{};
  std::array<uint8_t, 128> block_{};
  size_t buffered_ = 0;
  uint64_t total_low_ = 0;
  uint64_t total_high_ = 0;
  size_t out_bytes_ = 64;
};

class Blake2b {
 public:
  explicit Blake2b(size_t out_bytes) : out_bytes_(out_bytes) {
    state_ = kIv;
    state_[0] ^= 0x01010000ULL | static_cast<uint64_t>(out_bytes_);
  }

  auto update(std::span<const uint8_t> input) -> void {
    if (buffered_ != 0) {
      size_t take = std::min(input.size(), block_.size() - buffered_);
      std::copy_n(input.data(), take, block_.data() + buffered_);
      buffered_ += take;
      input = input.subspan(take);
      if (buffered_ == block_.size() && !input.empty()) {
        increment_counter(block_.size());
        compress(block_.data());
        buffered_ = 0;
      }
    }

    while (input.size() > block_.size()) {
      increment_counter(block_.size());
      compress(input.data());
      input = input.subspan(block_.size());
    }

    if (!input.empty()) {
      std::copy_n(input.data(), input.size(), block_.data());
      buffered_ = input.size();
    }
  }

  auto final() -> std::vector<uint8_t> {
    increment_counter(buffered_);
    final_flags_[0] = UINT64_MAX;
    std::fill(block_.begin() + buffered_, block_.end(), 0);
    compress(block_.data());

    std::array<uint8_t, 64> full{};
    for (size_t i = 0; i < state_.size(); ++i) {
      store_le64(full.data() + i * 8, state_[i]);
    }
    return {full.begin(),
            full.begin() + static_cast<std::ptrdiff_t>(out_bytes_)};
  }

 private:
  static constexpr std::array<uint64_t, 8> kIv{
      0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
      0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
      0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

  auto increment_counter(size_t inc) -> void {
    uint64_t old = counter_[0];
    counter_[0] += static_cast<uint64_t>(inc);
    if (counter_[0] < old) {
      ++counter_[1];
    }
  }

  auto compress(const uint8_t* block) -> void {
    const uint64_t m0 = load_le64(block + 0);
    const uint64_t m1 = load_le64(block + 8);
    const uint64_t m2 = load_le64(block + 16);
    const uint64_t m3 = load_le64(block + 24);
    const uint64_t m4 = load_le64(block + 32);
    const uint64_t m5 = load_le64(block + 40);
    const uint64_t m6 = load_le64(block + 48);
    const uint64_t m7 = load_le64(block + 56);
    const uint64_t m8 = load_le64(block + 64);
    const uint64_t m9 = load_le64(block + 72);
    const uint64_t m10 = load_le64(block + 80);
    const uint64_t m11 = load_le64(block + 88);
    const uint64_t m12 = load_le64(block + 96);
    const uint64_t m13 = load_le64(block + 104);
    const uint64_t m14 = load_le64(block + 112);
    const uint64_t m15 = load_le64(block + 120);

    uint64_t v0 = state_[0];
    uint64_t v1 = state_[1];
    uint64_t v2 = state_[2];
    uint64_t v3 = state_[3];
    uint64_t v4 = state_[4];
    uint64_t v5 = state_[5];
    uint64_t v6 = state_[6];
    uint64_t v7 = state_[7];
    uint64_t v8 = kIv[0];
    uint64_t v9 = kIv[1];
    uint64_t v10 = kIv[2];
    uint64_t v11 = kIv[3];
    uint64_t v12 = kIv[4] ^ counter_[0];
    uint64_t v13 = kIv[5] ^ counter_[1];
    uint64_t v14 = kIv[6] ^ final_flags_[0];
    uint64_t v15 = kIv[7] ^ final_flags_[1];

#define WINUXCMD_B2G(a, b, c, d, x, y) \
  do {                                 \
    a = a + b + x;                     \
    d = rotr64(d ^ a, 32);             \
    c += d;                            \
    b = rotr64(b ^ c, 24);             \
    a = a + b + y;                     \
    d = rotr64(d ^ a, 16);             \
    c += d;                            \
    b = rotr64(b ^ c, 63);             \
  } while (false)
#define WINUXCMD_B2ROUND(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, \
                         s12, s13, s14, s15)                               \
  do {                                                                     \
    WINUXCMD_B2G(v0, v4, v8, v12, s0, s1);                                 \
    WINUXCMD_B2G(v1, v5, v9, v13, s2, s3);                                 \
    WINUXCMD_B2G(v2, v6, v10, v14, s4, s5);                                \
    WINUXCMD_B2G(v3, v7, v11, v15, s6, s7);                                \
    WINUXCMD_B2G(v0, v5, v10, v15, s8, s9);                                \
    WINUXCMD_B2G(v1, v6, v11, v12, s10, s11);                              \
    WINUXCMD_B2G(v2, v7, v8, v13, s12, s13);                               \
    WINUXCMD_B2G(v3, v4, v9, v14, s14, s15);                               \
  } while (false)

    WINUXCMD_B2ROUND(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13,
                     m14, m15);
    WINUXCMD_B2ROUND(m14, m10, m4, m8, m9, m15, m13, m6, m1, m12, m0, m2, m11,
                     m7, m5, m3);
    WINUXCMD_B2ROUND(m11, m8, m12, m0, m5, m2, m15, m13, m10, m14, m3, m6, m7,
                     m1, m9, m4);
    WINUXCMD_B2ROUND(m7, m9, m3, m1, m13, m12, m11, m14, m2, m6, m5, m10, m4,
                     m0, m15, m8);
    WINUXCMD_B2ROUND(m9, m0, m5, m7, m2, m4, m10, m15, m14, m1, m11, m12, m6,
                     m8, m3, m13);
    WINUXCMD_B2ROUND(m2, m12, m6, m10, m0, m11, m8, m3, m4, m13, m7, m5, m15,
                     m14, m1, m9);
    WINUXCMD_B2ROUND(m12, m5, m1, m15, m14, m13, m4, m10, m0, m7, m6, m3, m9,
                     m2, m8, m11);
    WINUXCMD_B2ROUND(m13, m11, m7, m14, m12, m1, m3, m9, m5, m0, m15, m4, m8,
                     m6, m2, m10);
    WINUXCMD_B2ROUND(m6, m15, m14, m9, m11, m3, m0, m8, m12, m2, m13, m7, m1,
                     m4, m10, m5);
    WINUXCMD_B2ROUND(m10, m2, m8, m4, m7, m6, m1, m5, m15, m11, m9, m14, m3,
                     m12, m13, m0);
    WINUXCMD_B2ROUND(m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13,
                     m14, m15);
    WINUXCMD_B2ROUND(m14, m10, m4, m8, m9, m15, m13, m6, m1, m12, m0, m2, m11,
                     m7, m5, m3);

#undef WINUXCMD_B2ROUND
#undef WINUXCMD_B2G

    state_[0] ^= v0 ^ v8;
    state_[1] ^= v1 ^ v9;
    state_[2] ^= v2 ^ v10;
    state_[3] ^= v3 ^ v11;
    state_[4] ^= v4 ^ v12;
    state_[5] ^= v5 ^ v13;
    state_[6] ^= v6 ^ v14;
    state_[7] ^= v7 ^ v15;
  }

  std::array<uint64_t, 8> state_{};
  std::array<uint64_t, 2> counter_{};
  std::array<uint64_t, 2> final_flags_{};
  std::array<uint8_t, 128> block_{};
  size_t buffered_ = 0;
  size_t out_bytes_ = 64;
};

class Md5 {
 public:
  Md5() = default;

  auto update(std::span<const uint8_t> input) -> void {
    total_bytes_ += input.size();
    if (buffered_ != 0) {
      size_t take = std::min(input.size(), block_.size() - buffered_);
      std::copy_n(input.data(), take, block_.data() + buffered_);
      buffered_ += take;
      input = input.subspan(take);
      if (buffered_ == block_.size()) {
        compress(block_.data());
        buffered_ = 0;
      }
    }

    while (input.size() >= block_.size()) {
      compress(input.data());
      input = input.subspan(block_.size());
    }

    if (!input.empty()) {
      std::copy_n(input.data(), input.size(), block_.data());
      buffered_ = input.size();
    }
  }

  auto final() -> std::vector<uint8_t> {
    uint64_t bit_len = total_bytes_ * 8;
    block_[buffered_++] = 0x80;
    if (buffered_ > 56) {
      std::fill(block_.begin() + buffered_, block_.end(), 0);
      compress(block_.data());
      buffered_ = 0;
    }
    std::fill(block_.begin() + buffered_, block_.begin() + 56, 0);
    store_le64(block_.data() + 56, bit_len);
    compress(block_.data());

    std::array<uint8_t, 16> out{};
    for (size_t i = 0; i < state_.size(); ++i) {
      store_le32(out.data() + i * 4, state_[i]);
    }
    return {out.begin(), out.end()};
  }

 private:
  static auto round_constants() -> const std::array<uint32_t, 64>& {
    static const auto table = [] {
      std::array<uint32_t, 64> generated{};
      for (size_t i = 0; i < generated.size(); ++i) {
        generated[i] = static_cast<uint32_t>(
            std::fabs(std::sin(static_cast<double>(i) + 1.0)) * 4294967296.0);
      }
      return generated;
    }();
    return table;
  }

  auto compress(const uint8_t* block) -> void {
    static constexpr std::array<uint8_t, 64> shifts{
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

    std::array<uint32_t, 16> m{};
    for (size_t i = 0; i < m.size(); ++i) {
      m[i] = load_le32(block + i * 4);
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];

    const auto& k = round_constants();
    for (size_t i = 0; i < 64; ++i) {
      uint32_t f;
      size_t g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      uint32_t tmp = d;
      d = c;
      c = b;
      b = b + rotl32(a + f + k[i] + m[g], shifts[i]);
      a = tmp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
  }

  std::array<uint32_t, 4> state_{0x67452301U, 0xefcdab89U, 0x98badcfeU,
                                 0x10325476U};
  std::array<uint8_t, 64> block_{};
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

class Sha1 {
 public:
  Sha1() = default;

  auto update(std::span<const uint8_t> input) -> void {
    total_bytes_ += input.size();
    if (buffered_ != 0) {
      size_t take = std::min(input.size(), block_.size() - buffered_);
      std::copy_n(input.data(), take, block_.data() + buffered_);
      buffered_ += take;
      input = input.subspan(take);
      if (buffered_ == block_.size()) {
        compress(block_.data());
        buffered_ = 0;
      }
    }

    while (input.size() >= block_.size()) {
      compress(input.data());
      input = input.subspan(block_.size());
    }

    if (!input.empty()) {
      std::copy_n(input.data(), input.size(), block_.data());
      buffered_ = input.size();
    }
  }

  auto final() -> std::vector<uint8_t> {
    uint64_t bit_len = total_bytes_ * 8;
    block_[buffered_++] = 0x80;
    if (buffered_ > 56) {
      std::fill(block_.begin() + buffered_, block_.end(), 0);
      compress(block_.data());
      buffered_ = 0;
    }
    std::fill(block_.begin() + buffered_, block_.begin() + 56, 0);
    store_be64(block_.data() + 56, bit_len);
    compress(block_.data());

    std::array<uint8_t, 20> out{};
    for (size_t i = 0; i < state_.size(); ++i) {
      store_be32(out.data() + i * 4, state_[i]);
    }
    return {out.begin(), out.end()};
  }

 private:
  auto compress(const uint8_t* block) -> void {
    std::array<uint32_t, 80> w{};
    for (size_t i = 0; i < 16; ++i) {
      w[i] = load_be32(block + i * 4);
    }
    for (size_t i = 16; i < 80; ++i) {
      w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];

    for (size_t i = 0; i < 80; ++i) {
      uint32_t f;
      uint32_t k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999U;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1U;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdcU;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6U;
      }
      uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotl32(b, 30);
      b = a;
      a = tmp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
  }

  std::array<uint32_t, 5> state_{0x67452301U, 0xefcdab89U, 0x98badcfeU,
                                 0x10325476U, 0xc3d2e1f0U};
  std::array<uint8_t, 64> block_{};
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

// SM3 (GB/T 32905-2016), as used by GNU cksum -a sm3.
class Sm3 {
 public:
  Sm3() = default;

  auto update(std::span<const uint8_t> input) -> void {
    total_bytes_ += input.size();
    if (buffered_ != 0) {
      size_t take = std::min(input.size(), block_.size() - buffered_);
      std::copy_n(input.data(), take, block_.data() + buffered_);
      buffered_ += take;
      input = input.subspan(take);
      if (buffered_ == block_.size()) {
        compress(block_.data());
        buffered_ = 0;
      }
    }

    while (input.size() >= block_.size()) {
      compress(input.data());
      input = input.subspan(block_.size());
    }

    if (!input.empty()) {
      std::copy_n(input.data(), input.size(), block_.data());
      buffered_ = input.size();
    }
  }

  auto final() -> std::vector<uint8_t> {
    uint64_t bit_len = total_bytes_ * 8;
    block_[buffered_++] = 0x80;
    if (buffered_ > 56) {
      std::fill(block_.begin() + buffered_, block_.end(), 0);
      compress(block_.data());
      buffered_ = 0;
    }
    std::fill(block_.begin() + buffered_, block_.begin() + 56, 0);
    store_be64(block_.data() + 56, bit_len);
    compress(block_.data());

    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < state_.size(); ++i) {
      store_be32(out.data() + i * 4, state_[i]);
    }
    return {out.begin(), out.end()};
  }

 private:
  static auto p0(uint32_t x) -> uint32_t {
    return x ^ rotl32(x, 9) ^ rotl32(x, 17);
  }
  static auto p1(uint32_t x) -> uint32_t {
    return x ^ rotl32(x, 15) ^ rotl32(x, 23);
  }

  auto compress(const uint8_t* block) -> void {
    std::array<uint32_t, 68> w{};
    std::array<uint32_t, 64> w1{};
    for (size_t i = 0; i < 16; ++i) {
      w[i] = load_be32(block + i * 4);
    }
    for (size_t j = 16; j < 68; ++j) {
      w[j] = p1(w[j - 16] ^ w[j - 9] ^ rotl32(w[j - 3], 15)) ^
             rotl32(w[j - 13], 7) ^ w[j - 6];
    }
    for (size_t j = 0; j < 64; ++j) {
      w1[j] = w[j] ^ w[j + 4];
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t j = 0; j < 64; ++j) {
      const uint32_t tj = j < 16 ? 0x79cc4519U : 0x7a879d8aU;
      const uint32_t ss1 = rotl32(
          rotl32(a, 12) + e + rotl32(tj, static_cast<unsigned>(j % 32)), 7);
      const uint32_t ss2 = ss1 ^ rotl32(a, 12);
      const uint32_t ff = j < 16 ? (a ^ b ^ c) : ((a & b) | (a & c) | (b & c));
      const uint32_t gg = j < 16 ? (e ^ f ^ g) : ((e & f) | (~e & g));
      const uint32_t tt1 = ff + d + ss2 + w1[j];
      const uint32_t tt2 = gg + h + ss1 + w[j];
      d = c;
      c = rotl32(b, 9);
      b = a;
      a = tt1;
      h = g;
      g = rotl32(f, 19);
      f = e;
      e = p0(tt2);
    }

    state_[0] ^= a;
    state_[1] ^= b;
    state_[2] ^= c;
    state_[3] ^= d;
    state_[4] ^= e;
    state_[5] ^= f;
    state_[6] ^= g;
    state_[7] ^= h;
  }

  std::array<uint32_t, 8> state_{0x7380166fU, 0x4914b2b9U, 0x172442d7U,
                                 0xda8a0600U, 0xa96f30bcU, 0x163138aaU,
                                 0xe38dee4dU, 0xb0fb0e4eU};
  std::array<uint8_t, 64> block_{};
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

template <typename Hasher>
auto feed_stream(std::istream& in, Hasher& hasher) -> bool {
  std::array<char, 65536> buffer{};
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::streamsize read = in.gcount();
    if (read > 0) {
      auto* bytes = reinterpret_cast<const uint8_t*>(buffer.data());
      hasher.update(std::span<const uint8_t>(bytes, static_cast<size_t>(read)));
    }
  }
  return !in.bad();
}

template <typename Hasher>
auto hash_stream_to_hex(std::istream& in, Hasher& hasher)
    -> std::expected<std::string, std::string> {
  if (!feed_stream(in, hasher)) {
    return std::unexpected("error reading input");
  }
  auto digest = hasher.final();
  return to_hex(digest);
}

auto make_sha224() -> Sha256Family {
  return Sha256Family({0xc1059ed8U, 0x367cd507U, 0x3070dd17U, 0xf70e5939U,
                       0xffc00b31U, 0x68581511U, 0x64f98fa7U, 0xbefa4fa4U},
                      28);
}
auto make_sha256() -> Sha256Family {
  return Sha256Family({0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                       0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U},
                      32);
}

auto make_sha384() -> Sha512Family {
  return Sha512Family(
      {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL,
       0x152fecd8f70e5939ULL, 0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
       0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL},
      48);
}

auto make_sha512() -> Sha512Family {
  return Sha512Family(
      {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
       0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
       0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL},
      64);
}

auto cksum_table() -> const std::array<uint32_t, 256>& {
  static const auto table = [] {
    constexpr uint32_t polynomial = 0x04C11DB7U;
    std::array<uint32_t, 256> generated{};
    for (size_t i = 0; i < generated.size(); ++i) {
      uint32_t rem = static_cast<uint32_t>(i) << 24;
      for (int bit = 0; bit < 8; ++bit) {
        rem = (rem & 0x80000000U) ? ((rem << 1) ^ polynomial) : (rem << 1);
      }
      generated[i] = rem;
    }
    return generated;
  }();
  return table;
}

auto cksum_slice8_tables() -> const std::array<std::array<uint32_t, 256>, 8>& {
  static const auto tables = [] {
    std::array<std::array<uint32_t, 256>, 8> generated{};
    generated[0] = cksum_table();
    for (size_t i = 0; i < 256; ++i) {
      uint32_t crc = generated[0][i];
      for (size_t offset = 1; offset < generated.size(); ++offset) {
        crc = (crc << 8) ^ generated[0][(crc >> 24) & 0xFFU];
        generated[offset][i] = crc;
      }
    }
    return generated;
  }();
  return tables;
}

}  // namespace detail

auto hmac_sha256_stream_hex(std::span<const uint8_t> key, std::istream& input)
    -> std::expected<std::string, std::string> {
  std::array<uint8_t, 64> key_block{};
  if (key.size() > key_block.size()) {
    auto key_hasher = detail::make_sha256();
    key_hasher.update(key);
    auto key_digest = key_hasher.final();
    std::copy(key_digest.begin(), key_digest.end(), key_block.begin());
  } else {
    std::copy(key.begin(), key.end(), key_block.begin());
  }

  std::array<uint8_t, 64> ipad{};
  std::array<uint8_t, 64> opad{};
  for (size_t i = 0; i < key_block.size(); ++i) {
    ipad[i] = key_block[i] ^ 0x36U;
    opad[i] = key_block[i] ^ 0x5cU;
  }

  auto inner = detail::make_sha256();
  inner.update(ipad);
  if (!detail::feed_stream(input, inner)) {
    return std::unexpected("error reading input");
  }
  auto inner_digest = inner.final();

  auto outer = detail::make_sha256();
  outer.update(opad);
  outer.update(inner_digest);
  return detail::to_hex(outer.final());
}
auto hmac_sha256_stream_hex(std::string_view key, std::istream& input)
    -> std::expected<std::string, std::string> {
  auto* key_bytes = reinterpret_cast<const uint8_t*>(key.data());
  return hmac_sha256_stream_hex(std::span<const uint8_t>(key_bytes, key.size()),
                                input);
}
auto hash_file_hex(HashAlgorithm algorithm, const std::string& filename,
                   bool text_mode = false, size_t digest_bytes = 0)
    -> std::expected<std::string, std::string> {
  std::istream* input = &std::cin;
  std::ifstream file;
  if (!filename.empty() && filename != "-") {
    // Route through open_binary_file so /dev/stdin-family operands bind the
    // real fd 0 (closed stdin reports an error instead of blocking on
    // CONIN$, and pipes/hashable data flow through, #1091).
    file = file_io::open_binary_file(filename);
    if (!file) {
      return std::unexpected(detail::input_open_error(filename));
    }
    input = &file;
  } else if (file_io::stdin_is_bad()) {
    // [GNU] `md5sum <&-` reports "-: Bad file descriptor" rather than
    // hashing an empty stream (#1091).
    return std::unexpected(std::string(filename.empty() ? "-" : filename) +
                           ": Bad file descriptor");
  }

  switch (algorithm) {
    case HashAlgorithm::Crc32b: {
      // [GNU] crc32b_sum_stream: reflected CRC-32 (poly 0xEDB88320),
      // init/final 0xFFFFFFFF, digest is the 4 bytes big-endian.
      constexpr uint32_t kCrc32bPoly = 0xEDB88320u;
      uint32_t crc = 0xFFFFFFFFu;
      std::array<char, 8192> buffer{};
      uint64_t length = 0;
      while (input->read(buffer.data(), buffer.size()) || input->gcount() > 0) {
        const std::streamsize got = input->gcount();
        length += static_cast<uint64_t>(got);
        for (std::streamsize i = 0; i < got; ++i) {
          crc ^= static_cast<uint32_t>(
              static_cast<unsigned char>(buffer[static_cast<size_t>(i)]));
          for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0u - (crc & 1u) & kCrc32bPoly);
          }
        }
      }
      crc ^= 0xFFFFFFFFu;
      const std::array<uint8_t, 4> digest = {
          static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
          static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
      return detail::to_hex(std::span<const uint8_t>(digest));
    }
    case HashAlgorithm::Md5: {
      auto hasher = detail::Md5();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Sha1: {
      auto hasher = detail::Sha1();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Sm3: {
      auto hasher = detail::Sm3();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Sha224: {
      auto hasher = detail::make_sha224();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Sha256: {
      auto hasher = detail::make_sha256();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Sha384: {
      auto hasher = detail::make_sha384();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Sha512: {
      auto hasher = detail::make_sha512();
      return detail::hash_stream_to_hex(*input, hasher);
    }
    case HashAlgorithm::Blake2b: {
      size_t out_bytes = digest_bytes == 0 ? 64 : digest_bytes;
      if (out_bytes == 0 || out_bytes > 64) {
        return std::unexpected("invalid BLAKE2b digest length");
      }
      auto hasher = detail::Blake2b(out_bytes);
      return detail::hash_stream_to_hex(*input, hasher);
    }
  }

  return std::unexpected("unsupported digest algorithm");
}

class PosixCksumAccumulator {
 public:
  auto update(std::span<const uint8_t> data) -> void {
    const auto& tables = detail::cksum_slice8_tables();
    bytes_ += data.size();

    while (data.size() >= 8) {
      uint32_t first = (static_cast<uint32_t>(data[0]) << 24) |
                       (static_cast<uint32_t>(data[1]) << 16) |
                       (static_cast<uint32_t>(data[2]) << 8) |
                       static_cast<uint32_t>(data[3]);
      uint32_t second = (static_cast<uint32_t>(data[4]) << 24) |
                        (static_cast<uint32_t>(data[5]) << 16) |
                        (static_cast<uint32_t>(data[6]) << 8) |
                        static_cast<uint32_t>(data[7]);
      crc_ ^= first;
      crc_ = tables[7][(crc_ >> 24) & 0xFFU] ^ tables[6][(crc_ >> 16) & 0xFFU] ^
             tables[5][(crc_ >> 8) & 0xFFU] ^ tables[4][crc_ & 0xFFU] ^
             tables[3][(second >> 24) & 0xFFU] ^
             tables[2][(second >> 16) & 0xFFU] ^
             tables[1][(second >> 8) & 0xFFU] ^ tables[0][second & 0xFFU];
      data = data.subspan(8);
    }

    const auto& table = detail::cksum_table();
    for (uint8_t byte : data) {
      crc_ = (crc_ << 8) ^ table[((crc_ >> 24) ^ byte) & 0xFFU];
    }
  }

  [[nodiscard]] auto final() const -> PosixCksumResult {
    const auto& table = detail::cksum_table();
    uint32_t crc = crc_;
    for (uint64_t length = bytes_; length != 0; length >>= 8) {
      crc = (crc << 8) ^ table[((crc >> 24) ^ length) & 0xFFU];
    }
    return PosixCksumResult{~crc & 0xFFFFFFFFU, bytes_};
  }

 private:
  uint32_t crc_ = 0;
  uint64_t bytes_ = 0;
};

auto posix_cksum(std::span<const uint8_t> data) -> uint32_t {
  PosixCksumAccumulator accumulator;
  accumulator.update(data);
  return accumulator.final().checksum;
}

auto posix_cksum_stream(std::istream& in)
    -> std::expected<PosixCksumResult, std::string> {
  PosixCksumAccumulator accumulator;
  std::array<char, 65536> buffer{};
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::streamsize read = in.gcount();
    if (read > 0) {
      auto* bytes = reinterpret_cast<const uint8_t*>(buffer.data());
      accumulator.update(
          std::span<const uint8_t>(bytes, static_cast<size_t>(read)));
    }
  }
  if (in.bad()) return std::unexpected("error reading input");
  return accumulator.final();
}

// [GNU] strerror-style reason for a failed input open(), for commands
// that print "cmd: FILE: <reason>" diagnostics.  See
// detail::open_failure_reason for the rules.
auto open_error_reason(std::string_view path) -> std::string {
  return detail::open_failure_reason(path);
}

// ===== GNU-compatible "--check" verification (src/cksum.c digest_check) =====
//
// Shared by the *sum commands.  Mirrors GNU semantics:
//   * '#' comments and blank lines are skipped; trailing '\r' is stripped.
//   * split_3(): leading blanks skipped, optional '\' escape prefix, BSD
//     "TAG (file) = hex" and OpenSSL "TAG(file)= hex" forms, otherwise the
//     untagged "hex [' '|'*']file" form.  The untagged form is remembered per
//     invocation (bsd_reversed) and mixing it with the single-blank form in
//     one direction is rejected.
//   * Per-line results go to stdout ("name: OK"/"FAILED"/"FAILED open or
//     read"), diagnostics to stderr, and a properly-formatted file produces
//     up to three WARNING summary lines (improperly formatted / could not be
//     read / did NOT match).
struct SumCheckFlags {
  bool status_only = false;
  bool quiet = false;
  bool warn = false;
  bool strict = false;
  bool ignore_missing = false;
};

struct SumCheckLine {
  std::string expected_hash;
  std::string filename;
  size_t digest_bytes = 0;  // detected digest length (bytes) for this line
};

namespace detail {

auto sum_is_blank(char c) -> bool { return c == ' ' || c == '\t'; }

// GNU quotef(): print a file name verbatim unless it needs quoting.
auto sum_quotef(std::string_view name) -> std::string {
  if (name.empty()) return "''";
  const bool simple = std::ranges::all_of(name, [](unsigned char c) {
    return c > 0x20 && c != '\'' && c != '\\' && c != '"' && c != '|' &&
           c != '&' && c != ';' && c != '<' && c != '>' && c != '(' &&
           c != ')' && c != '$' && c != '`' && c != '*' && c != '?' &&
           c != '[' && c != '#' && c != '~' && c != '=' && c != '!' &&
           c != '^' && c != '{' && c != '}';
  });
  if (simple) return std::string(name);
  std::string out = "'";
  for (char c : name) {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

// GNU filename_unescape(): translate \\, \n and \r escapes introduced when
// the check line started with '\'.  Returns nullopt on a bad escape.
auto sum_unescape_filename(std::string_view s) -> std::optional<std::string> {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\') {
      out.push_back(s[i]);
      continue;
    }
    if (++i >= s.size()) return std::nullopt;
    switch (s[i]) {
      case '\\':
        out.push_back('\\');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      default:
        return std::nullopt;
    }
  }
  return out;
}

auto sum_open_reason(std::string_view path) -> std::string {
  return open_failure_reason(path);
}

auto sum_all_hex(std::string_view s) -> bool {
  return !s.empty() && std::ranges::all_of(s, [](unsigned char c) {
    return std::isxdigit(c) != 0;
  });
}

// GNU bsd_split_3(): S is the part after "TAG ("; find the last ')', take
// everything before it as the file name, then require blanks, '=', blanks
// and a valid digest.
auto sum_bsd_split(std::string_view s, size_t digest_hex_bytes,
                   bool escaped_filename) -> std::optional<SumCheckLine> {
  if (s.empty()) return std::nullopt;

  size_t i = s.size() - 1;
  while (i > 0 && s[i] != ')') --i;
  if (s[i] != ')') return std::nullopt;

  std::string_view filename = s.substr(0, i);
  ++i;
  while (i < s.size() && sum_is_blank(s[i])) ++i;
  if (i >= s.size() || s[i] != '=') return std::nullopt;
  ++i;
  while (i < s.size() && sum_is_blank(s[i])) ++i;

  std::string_view digest = s.substr(i);
  const size_t hex_len =
      digest_hex_bytes != 0 ? digest_hex_bytes : digest.size();
  if (digest.size() != hex_len || !sum_all_hex(digest)) return std::nullopt;
  if (digest_hex_bytes == 0 &&
      (digest.size() % 2 != 0 || digest.size() > 128)) {
    return std::nullopt;
  }

  if (escaped_filename) {
    auto unescaped = sum_unescape_filename(filename);
    if (!unescaped) return std::nullopt;
    return SumCheckLine{std::string(digest), *unescaped, digest.size() / 2};
  }
  return SumCheckLine{std::string(digest), std::string(filename),
                      digest.size() / 2};
}

}  // namespace detail

// GNU split_3() for a fixed-length hex digest command.  Pass
// digest_hex_bytes == 0 to auto-detect any even hex length <= 128 (the b2sum
// behaviour).  bsd_reversed is GNU's format-consistency latch (-1 initial,
// then 0 or 1) shared across all check files of one invocation.
inline auto sum_parse_check_line(std::string_view line,
                                 std::string_view algo_tag,
                                 size_t digest_hex_bytes, int& bsd_reversed)
    -> std::optional<SumCheckLine> {
  size_t i = 0;
  while (i < line.size() && detail::sum_is_blank(line[i])) ++i;

  bool escaped_filename = false;
  if (i < line.size() && line[i] == '\\') {
    ++i;
    escaped_filename = true;
  }

  // BSD/OpenSSL tagged form: "TAG (file) = hex" or "TAG(file)= hex".
  const size_t parse_offset = i;
  if (line.substr(i).starts_with(algo_tag)) {
    size_t j = i + algo_tag.size();
    size_t tag_hex_bytes = digest_hex_bytes;
    // "TAG-<bits>" length suffix (GNU supports it for blake2/sha2 tags).
    if (digest_hex_bytes == 0 && j < line.size() && line[j] == '-') {
      ++j;
      const size_t num_start = j;
      while (j < line.size() && std::isdigit(line[j])) ++j;
      if (j == num_start) return std::nullopt;
      long bits = 0;
      std::from_chars(line.data() + num_start, line.data() + j, bits);
      if (bits <= 0 || bits % 8 != 0 || bits > 512) return std::nullopt;
      tag_hex_bytes = static_cast<size_t>(bits / 4);
    }
    if (j < line.size() && line[j] == ' ') ++j;
    if (j < line.size() && line[j] == '(') {
      return detail::sum_bsd_split(line.substr(j + 1), tag_hex_bytes,
                                   escaped_filename);
    }
    i = parse_offset;
  }

  // Untagged form needs at least "<hex> <file>".
  const size_t min_len = (digest_hex_bytes != 0 ? digest_hex_bytes : 2) + 2;
  if (line.size() - i < min_len) return std::nullopt;

  const size_t digest_start = i;
  while (i < line.size() && !detail::sum_is_blank(line[i])) ++i;
  if (i == line.size()) return std::nullopt;  // digest must be followed by ws

  const std::string_view digest = line.substr(digest_start, i - digest_start);
  if (digest_hex_bytes != 0) {
    if (digest.size() != digest_hex_bytes || !detail::sum_all_hex(digest)) {
      return std::nullopt;
    }
  } else if (digest.size() < 2 || digest.size() % 2 != 0 ||
             digest.size() > 128 || !detail::sum_all_hex(digest)) {
    return std::nullopt;
  }
  ++i;  // consume the single separator blank

  std::string_view filename;
  if (line.size() - i <= 1 ||
      (i < line.size() && line[i] != ' ' && line[i] != '*')) {
    // BSD-reversed form: separator was the only blank.
    if (bsd_reversed == 0) return std::nullopt;
    bsd_reversed = 1;
    filename = line.substr(i);
  } else if (bsd_reversed != 1) {
    bsd_reversed = 0;
    ++i;  // consume the ' ' or '*' mode marker
    filename = line.substr(i);
  } else {
    filename = line.substr(i);
  }

  if (escaped_filename) {
    auto unescaped = detail::sum_unescape_filename(filename);
    if (!unescaped) return std::nullopt;
    return SumCheckLine{std::string(digest), *unescaped, digest.size() / 2};
  }
  return SumCheckLine{std::string(digest), std::string(filename),
                      digest.size() / 2};
}

// GNU digest_check(): verify every properly formatted line of one check
// file.  hash_fn(filename, digest_bytes) must return the hex digest or an
// error when the file cannot be opened/read.
template <typename HashFn>
auto sum_digest_check(const std::string& program, const std::string& algo_tag,
                      size_t digest_hex_bytes, const SumCheckFlags& flags,
                      const std::string& checkfile_name, int& bsd_reversed,
                      HashFn&& hash_fn) -> bool {
  intmax_t n_misformatted_lines = 0;
  intmax_t n_mismatched_checksums = 0;
  intmax_t n_open_or_read_failures = 0;
  bool properly_formatted_lines = false;
  bool matched_checksums = false;

  const bool is_stdin = checkfile_name == "-";
  std::string display_name = checkfile_name;
  std::istream* input = &std::cin;
  std::ifstream checkfile;
  if (is_stdin) {
    display_name = "standard input";
  } else {
    checkfile.open(native_path::normalize_api_operand(checkfile_name));
    if (!checkfile) {
      std::error_code ec;
      // [GNU] fopen() succeeds on a directory and the first getline() then
      // fails: GNU reports "<file>: read error", not "Is a directory".
      if (std::filesystem::is_directory(std::filesystem::u8path(checkfile_name),
                                        ec) &&
          !ec) {
        safeErrorPrintLn(program + ": " + detail::sum_quotef(checkfile_name) +
                         ": read error");
      } else {
        safeErrorPrintLn(program + ": " + detail::sum_quotef(checkfile_name) +
                         ": " + detail::sum_open_reason(checkfile_name));
      }
      return false;
    }
    input = &checkfile;
  }

  std::string line;
  intmax_t line_number = 0;
  while (std::getline(*input, line)) {
    ++line_number;
    if (!line.empty() && line.front() == '#') continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    auto parsed =
        sum_parse_check_line(line, algo_tag, digest_hex_bytes, bsd_reversed);
    if (!parsed || (is_stdin && parsed->filename == "-")) {
      ++n_misformatted_lines;
      if (flags.warn) {
        safeErrorPrintLn(program + ": " + detail::sum_quotef(display_name) +
                         ": " + std::to_string(line_number) +
                         ": improperly formatted " + algo_tag +
                         " checksum line");
      }
      continue;
    }

    properly_formatted_lines = true;

    bool missing = false;
    if (flags.ignore_missing && parsed->filename != "-") {
      std::error_code ec;
      missing = !std::filesystem::exists(
          std::filesystem::u8path(parsed->filename), ec);
    }

    if (!missing) {
      auto hash_result = hash_fn(parsed->filename, parsed->digest_bytes);
      if (!hash_result) {
        ++n_open_or_read_failures;
        safeErrorPrintLn(program + ": " + detail::sum_quotef(parsed->filename) +
                         ": " + detail::sum_open_reason(parsed->filename));
        if (!flags.status_only) {
          safePrintLn(detail::sum_quotef(parsed->filename) +
                      ": FAILED open or read");
        }
        continue;
      }

      const bool match =
          hash_result->size() == parsed->expected_hash.size() &&
          std::equal(hash_result->begin(), hash_result->end(),
                     parsed->expected_hash.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                     });
      if (match) {
        matched_checksums = true;
      } else {
        ++n_mismatched_checksums;
      }

      if (!flags.status_only) {
        if (!match || !flags.quiet) {
          safePrintLn(detail::sum_quotef(parsed->filename) +
                      (match ? ": OK" : ": FAILED"));
        }
      }
    }
  }

  if (!properly_formatted_lines) {
    // [GNU] this diagnostic does not include the algorithm name.
    safeErrorPrintLn(program + ": " + detail::sum_quotef(display_name) +
                     ": no properly formatted checksum lines found");
  } else if (!flags.status_only) {
    if (n_misformatted_lines != 0) {
      safeErrorPrintLn(
          program + ": WARNING: " + std::to_string(n_misformatted_lines) +
          (n_misformatted_lines == 1 ? " line is improperly formatted"
                                     : " lines are improperly formatted"));
    }
    if (n_open_or_read_failures != 0) {
      safeErrorPrintLn(
          program + ": WARNING: " + std::to_string(n_open_or_read_failures) +
          (n_open_or_read_failures == 1 ? " listed file could not be read"
                                        : " listed files could not be read"));
    }
    if (n_mismatched_checksums != 0) {
      safeErrorPrintLn(
          program + ": WARNING: " + std::to_string(n_mismatched_checksums) +
          (n_mismatched_checksums == 1 ? " computed checksum did NOT match"
                                       : " computed checksums did NOT match"));
    }
    if (flags.ignore_missing && !matched_checksums) {
      safeErrorPrintLn(program + ": " + detail::sum_quotef(display_name) +
                       ": no file was verified");
    }
  }

  return properly_formatted_lines && matched_checksums &&
         n_mismatched_checksums == 0 && n_open_or_read_failures == 0 &&
         (!flags.strict || n_misformatted_lines == 0);
}

// Verify all check files (GNU loops over every operand; "-" or no operand
// means standard input).  Returns the process exit code.
template <typename HashFn>
auto sum_check_main(const std::string& program, const std::string& algo_tag,
                    size_t digest_hex_bytes, const SumCheckFlags& flags,
                    std::span<const std::string> checkfiles, HashFn&& hash_fn)
    -> int {
  int bsd_reversed = -1;
  bool ok = true;
  for (const auto& checkfile : checkfiles) {
    ok = sum_digest_check(program, algo_tag, digest_hex_bytes, flags, checkfile,
                          bsd_reversed, hash_fn) &&
         ok;
  }
  return ok ? 0 : 1;
}

}  // namespace portable_digest
