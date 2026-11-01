#pragma once

namespace sg {

/** Engine version, from the top-level CMake project. */
const char* versionString() noexcept;

/** Version of the JSON-lines control protocol (docs/protocol.md). */
inline constexpr int kProtocolVersion = 1;

} // namespace sg
