#!/usr/bin/env lua
-- vibecode
-- script.lua
-- Usage: lua script.lua <BitShares public key>

-- try various crypto backends
local lib_openssl_digest_ok,        lib_openssl_digest    = pcall(require, "openssl.digest")  -- openssl.digest.new()
local lib_openssl_shortdigest_ok,   lib_openssl_shortdigest = pcall(require, "openssl")        -- openssl.digest.digest()
local lib_crypto_ok,                lib_crypto            = pcall(require, "crypto")         -- LuaCrypto

-- wrapper for SHA-256
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
    error("No SHA256 implementation found (openssl.digest, openssl.digest.digest or crypto)")
  end
end

-- wrapper for RIPEMD-160
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
    error("No RIPEMD160 implementation found (openssl.digest, openssl.digest.digest or crypto)")
  end
end

-- Base58 alphabet
local BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

-- decode Base58→bytes
local function base58_decode(str)
  local map = {}
  for i = 1, #BASE58_ALPHABET do
    map[BASE58_ALPHABET:sub(i, i)] = i - 1
  end
  local num = 0
  for ch in str:gmatch(".") do
    local v = map[ch]
    assert(v ~= nil, "Invalid Base58 char: " .. ch)
    num = num * 58 + v
  end
  local bytes = {}
  while num > 0 do
    table.insert(bytes, 1, string.char(num % 256))
    num = math.floor(num / 256)
  end
  -- leading‐zero support
  for i = 1, #str do
    if str:sub(i, i) == "1" then
      table.insert(bytes, 1, "\0")
    else
      break
    end
  end
  return table.concat(bytes)
end

-- Base58Check encode (version byte + payload → Base58Check)
local function base58check_encode(version, payload)
  local data     = string.char(version) .. payload
  local checksum = easy_sha256(easy_sha256(data)):sub(1, 4)
  local full     = data .. checksum

  local num = 0
  for i = 1, #full do
    num = num * 256 + full:byte(i)
  end
  local result = {}
  while num > 0 do
    local rem = num % 58
    num = math.floor(num / 58)
    table.insert(result, 1, BASE58_ALPHABET:sub(rem + 1, rem + 1))
  end
  -- leading zeros
  for i = 1, #full do
    if full:byte(i) == 0 then
      table.insert(result, 1, "1")
    else
      break
    end
  end
  return table.concat(result)
end

-- Base58Check decode (verifies checksum)
local function base58check_decode(str)
  local raw = base58_decode(str)
  assert(#raw > 4, "Invalid data length")
  local data, csum = raw:sub(1, -5), raw:sub(-4)
  local csum_here = easy_sha256(easy_sha256(data))
  local ok = (csum_here:sub(1, 4) == csum)	

  assert(ok, string.format("Bad checksum in [%s] got raw [%s] so the csum is [%s] but calculated checksum of data is [%s]", str,raw, csum, csum_here))
  return data
end

-- main
local pubkey_base58 = arg[1]
if not pubkey_base58 then
  io.stderr:write("Usage: lua script.lua <BTS public key>\n")
  os.exit(1)
end

-- decode version + key, strip version byte (0x25)
local decoded_bytes   = base58check_decode(pubkey_base58)
local compressed_pk   = decoded_bytes:sub(2)

-- RIPEMD-160 then Base58Check with version 56
local ripemd_hash = easy_ripemd160(compressed_pk)
local address     = base58check_encode(56, ripemd_hash)

print(address)

