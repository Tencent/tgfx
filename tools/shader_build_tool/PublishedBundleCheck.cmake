# Checks one published shader bundle's toolchain ABI against the code's ExpectedToolchainABI.
#
# Data flow (the ABI has one source of truth; nothing here re-states the value):
#
#   PrecompiledBundleIdentity.h ──(regex parse)──▶ expected ABI (0x........)
#   resources/shaders/*.bin ──(u32 @ offset 16, little endian)──▶ bundle ABI
#                                    │
#                    mismatch ──▶ message(WARNING) at configure time
#
# The runtime loader already refuses a mismatched bundle, but it only gets the chance at draw
# time, silently disabling the precompiled path; the CI verify script (verify_published_bundles.sh)
# catches it after the push. This check surfaces the same drift at every local configure of a
# build that consumes the published bundles (TGFX_SHADER_BUNDLES_PREBUILT), before any code runs.
#
# The offset and width mirror BundleFormat::Offset::ToolchainABI; the static_asserts in
# BundleFormat.h pin that layout, and the CMake source list already treats the identity header
# as a rebuild input, so a bumped constant invalidates the bundles built from it too.

function(tgfx_check_published_bundle_abi bundle_path identity_header)
  file(READ "${identity_header}" identity_text)
  string(REGEX MATCH "ExpectedToolchainABI *= *0x([0-9A-Fa-f]+)" abi_line "${identity_text}")
  if (NOT abi_line)
    message(FATAL_ERROR "ExpectedToolchainABI not found in ${identity_header}")
  endif ()
  set(expected_hex "${CMAKE_MATCH_1}")
  string(TOUPPER "${expected_hex}" expected_hex)
  # Normalize to 8 hex digits (the constant is written zero padded, but the parse tolerates
  # a shorter spelling).
  string(LENGTH "${expected_hex}" expected_len)
  while (expected_len LESS 8)
    set(expected_hex "0${expected_hex}")
    string(LENGTH "${expected_hex}" expected_len)
  endwhile ()
  # The bundle stores the u32 little endian: compare against the byte-reversed hex digits.
  string(SUBSTRING "${expected_hex}" 6 2 byte0)
  string(SUBSTRING "${expected_hex}" 4 2 byte1)
  string(SUBSTRING "${expected_hex}" 2 2 byte2)
  string(SUBSTRING "${expected_hex}" 0 2 byte3)
  set(expected_le "${byte0}${byte1}${byte2}${byte3}")

  file(READ "${bundle_path}" bundle_abi_le OFFSET 16 LIMIT 4 HEX)
  string(TOUPPER "${bundle_abi_le}" bundle_abi_le)
  if (NOT bundle_abi_le STREQUAL expected_le)
    # Reverse the bundle's bytes for a big endian display value.
    string(SUBSTRING "${bundle_abi_le}" 6 2 found0)
    string(SUBSTRING "${bundle_abi_le}" 4 2 found1)
    string(SUBSTRING "${bundle_abi_le}" 2 2 found2)
    string(SUBSTRING "${bundle_abi_le}" 0 2 found3)
    set(found_hex "${found0}${found1}${found2}${found3}")
    message(WARNING
            "The published shader bundle ${bundle_path} is stale: its toolchain ABI is "
            "0x${found_hex} but the code expects 0x${expected_hex}. The runtime loader refuses "
            "the bundle, so this build would silently lose the precompiled shaders. Republish "
            "the bundles from a build that can generate them (tools/shader_build_tool/"
            "publish_bundles.sh or the tgfx_publish_shader_bundles target), then run "
            "tools/shader_build_tool/verify_published_bundles.sh and commit the result.")
  endif ()
endfunction()
