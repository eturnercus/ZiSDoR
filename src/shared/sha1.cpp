#include "sha1.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
	inline std::uint32_t rol(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
}

sha1::Hasher::Hasher() {
	h_[0] = 0x67452301u;
	h_[1] = 0xEFCDAB89u;
	h_[2] = 0x98BADCFEu;
	h_[3] = 0x10325476u;
	h_[4] = 0xC3D2E1F0u;
}

void sha1::Hasher::block(const std::uint8_t* p) {
	std::uint32_t w[80];
	for (int i = 0; i < 16; ++i) {
		w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
		       (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
	}
	for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
	for (int i = 0; i < 80; ++i) {
		std::uint32_t f, k;
		if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
		else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
		else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
		std::uint32_t t = rol(a, 5) + f + e + k + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = t;
	}
	h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
}

void sha1::Hasher::update(const void* data, std::size_t len) {
	const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
	total_ += len;
	if (bufLen_ > 0) {
		std::size_t take = std::min(len, sizeof(buf_) - bufLen_);
		std::memcpy(buf_ + bufLen_, p, take);
		bufLen_ += take; p += take; len -= take;
		if (bufLen_ == sizeof(buf_)) { block(buf_); bufLen_ = 0; }
	}
	while (len >= 64) { block(p); p += 64; len -= 64; }
	if (len > 0) { std::memcpy(buf_, p, len); bufLen_ = len; }
}

std::string sha1::Hasher::hexdigest() {
	std::uint64_t bits = total_ * 8;
	std::uint8_t pad = 0x80;
	update(&pad, 1);
	std::uint8_t zero = 0;
	while (bufLen_ != 56) update(&zero, 1);
	std::uint8_t len[8];
	for (int i = 0; i < 8; ++i) len[i] = std::uint8_t(bits >> (56 - 8 * i));
	update(len, 8);

	static const char* hex = "0123456789abcdef";
	std::string out;
	out.reserve(40);
	for (std::uint32_t v : h_) {
		for (int i = 28; i >= 0; i -= 4) out.push_back(hex[(v >> i) & 0xF]);
	}
	return out;
}

std::string sha1::ofString(const std::string& data) {
	Hasher h;
	h.update(data.data(), data.size());
	return h.hexdigest();
}

std::optional<std::string> sha1::ofFile(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	Hasher h;
	std::vector<char> buf(1 << 16);
	while (in) {
		in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
		std::streamsize n = in.gcount();
		if (n > 0) h.update(buf.data(), static_cast<std::size_t>(n));
	}
	if (in.bad()) return std::nullopt;
	return h.hexdigest();
}
