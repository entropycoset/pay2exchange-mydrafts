#!/usr/bin/env lua
-- BitShares Address Generator from Compressed Public Key

-- === Constants ===
local BTS_PREFIX = "BTS"

-- Load crypto backends
local lib_openssl_digest_ok, lib_openssl_digest = pcall(require, "openssl.digest")
local lib_openssl_shortdigest_ok, lib_openssl_shortdigest = pcall(require, "openssl")
local lib_crypto_ok, lib_crypto = pcall(require, "crypto")

-- Hex dump utility
local function to_hex(s)
  if s == nil then
    return "(nil)"
  end
  return (s:gsub(".", function(c)
    return string.format("%02X ", string.byte(c))
  end))
end

-- SHA256 wrapper
local function easy_sha256(data)
  if lib_openssl_digest_ok then
    local hasher = lib_openssl_digest.new("sha256")
    hasher:update(data)
    return hasher:final()
  elseif lib_openssl_shortdigest_ok and type(lib_openssl_shortdigest.digest) == "function" then
    return lib_openssl_shortdigest.digest("sha256", data, true)
  elseif lib_crypto_ok then
    return lib_crypto.digest("sha256", data, true)
  else
    error("No SHA256 implementation found")
  end
end

-- RIPEMD160 wrapper
local function easy_ripemd160(data)
  if lib_openssl_digest_ok then
    local hasher = lib_openssl_digest.new("rmd160")
    hasher:update(data)
    return hasher:final()
  elseif lib_openssl_shortdigest_ok and type(lib_openssl_shortdigest.digest) == "function" then
    return lib_openssl_shortdigest.digest("ripemd160", data, true)
  elseif lib_crypto_ok then
    return lib_crypto.digest("ripemd160", data, true)
  else
    error("No RIPEMD160 implementation found")
  end
end

-- Base58 alphabet
local BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

-- Base58 encode
local function base58_encode(data)
  local num = 0
  for i = 1, #data do
    num = num * 256 + data:byte(i)
  end
  local result = {}
  while num > 0 do
    local rem = num % 58
    num = math.floor(num / 58)
    table.insert(result, 1, BASE58_ALPHABET:sub(rem + 1, rem + 1))
  end
  -- leading zeros
  for i = 1, #data do
    if data:byte(i) == 0 then
      table.insert(result, 1, "1")
    else
      break
    end
  end
  return table.concat(result)
end

-- BitShares address generation
local function generate_bitshares_address(pubkey_bytes)
  assert(#pubkey_bytes == 33, "Expected 33-byte compressed public key")

  local ripemd = easy_ripemd160(pubkey_bytes)
  local checksum = ripemd:sub(1, 4)
  local address_bytes = pubkey_bytes .. checksum
  local base58_address = base58_encode(address_bytes)

  return BTS_PREFIX .. base58_address
end

-- Convert hex string to binary
local function hex_to_bytes(hex)
  print(hex)
  return (hex:gsub("..", function(cc)
    return string.char(tonumber(cc, 16))
  end))
end

-- === Main ===
local pubkey_arg = arg[1]
if not pubkey_arg then
  io.stderr:write("Usage: lua script.lua <compressed_pubkey_hex or BTS...>\n")
  os.exit(1)
end

-- If input starts with the BTS prefix, strip it
if pubkey_arg:sub(1, #BTS_PREFIX) == BTS_PREFIX then
  pubkey_arg = pubkey_arg:sub(#BTS_PREFIX + 1)
end

-- At this stage, pubkey_arg should be a hex string for the compressed public key
local pubkey_bytes = hex_to_bytes(pubkey_arg)
local address = generate_bitshares_address(pubkey_bytes)

print("BitShares Address:")
print(address)

