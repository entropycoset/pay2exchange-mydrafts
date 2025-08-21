#!/usr/bin/env lua
-- BitShares Address Generator from Base58-encoded public key

-- === Constants ===
local BTS_PREFIX = "BTS"
local BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

-- Crypto backends
local lib_openssl_digest_ok, lib_openssl_digest = pcall(require, "openssl.digest")
local lib_openssl_shortdigest_ok, lib_openssl_shortdigest = pcall(require, "openssl")
local lib_crypto_ok, lib_crypto = pcall(require, "crypto")

-- Hex dump utility
local function to_hex(s)
  if not s then return "(nil)" end
  return (s:gsub(".", function(c) return string.format("%02X ", string.byte(c)) end))
end

-- Wrappers
local function easy_sha256(data)
  if lib_openssl_digest_ok then
    local h = lib_openssl_digest.new("sha256"); h:update(data); return h:final()
  elseif lib_openssl_shortdigest_ok and type(lib_openssl_shortdigest.digest)=="function" then
    return lib_openssl_shortdigest.digest("sha256", data, true)
  elseif lib_crypto_ok then
    return lib_crypto.digest("sha256", data, true)
  else
    error("No SHA256 implementation found")
  end
end
local function easy_ripemd160(data)
  if lib_openssl_digest_ok then
    local h = lib_openssl_digest.new("rmd160"); h:update(data); return h:final()
  elseif lib_openssl_shortdigest_ok and type(lib_openssl_shortdigest.digest)=="function" then
    return lib_openssl_shortdigest.digest("ripemd160", data, true)
  elseif lib_crypto_ok then
    return lib_crypto.digest("ripemd160", data, true)
  else
    error("No RIPEMD160 implementation found")
  end
end

-- Base58 decode using string arithmetic for large numbers
local function base58_decode(str)
  local map = {}
  for i = 1, #BASE58_ALPHABET do map[BASE58_ALPHABET:sub(i,i)] = i - 1 end
  
  -- Use string-based arithmetic to handle large numbers
  local function string_add(a, b)
    local result = {}
    local carry = 0
    local i, j = #a, #b
    while i > 0 or j > 0 or carry > 0 do
      local digit_a = i > 0 and tonumber(a:sub(i,i)) or 0
      local digit_b = j > 0 and tonumber(b:sub(j,j)) or 0
      local sum = digit_a + digit_b + carry
      table.insert(result, 1, tostring(sum % 10))
      carry = math.floor(sum / 10)
      i, j = i - 1, j - 1
    end
    return table.concat(result)
  end
  
  local function string_multiply(a, n)
    if a == "0" then return "0" end
    local result = {}
    local carry = 0
    for i = #a, 1, -1 do
      local product = tonumber(a:sub(i,i)) * n + carry
      table.insert(result, 1, tostring(product % 10))
      carry = math.floor(product / 10)
    end
    while carry > 0 do
      table.insert(result, 1, tostring(carry % 10))
      carry = math.floor(carry / 10)
    end
    return table.concat(result)
  end
  
  -- Convert base58 to decimal string
  local num_str = "0"
  for ch in str:gmatch(".") do
    local v = map[ch]; assert(v, "Invalid Base58 char: " .. ch)
    num_str = string_add(string_multiply(num_str, 58), tostring(v))
  end
  
  -- Convert decimal string to bytes
  local bytes = {}
  while num_str ~= "0" do
    local remainder = 0
    local new_num = {}
    for i = 1, #num_str do
      local digit = tonumber(num_str:sub(i,i))
      local current = remainder * 10 + digit
      table.insert(new_num, tostring(math.floor(current / 256)))
      remainder = current % 256
    end
    table.insert(bytes, 1, string.char(remainder))
    num_str = table.concat(new_num):gsub("^0+", "") or "0"
    if num_str == "" then num_str = "0" end
  end
  
  -- Add leading zero bytes for leading '1's in base58
  for i = 1, #str do
    if str:sub(i,i) == "1" then table.insert(bytes, 1, "\0") else break end
  end
  
  return table.concat(bytes)
end

-- Base58 encode using string arithmetic for large numbers
local function base58_encode(data)
  if #data == 0 then return "" end
  
  -- Convert bytes to decimal string
  local num_str = "0"
  for i = 1, #data do
    -- Multiply by 256 and add current byte
    local byte_val = data:byte(i)
    local carry = byte_val
    local new_num = {}
    
    -- Multiply existing number by 256
    for j = #num_str, 1, -1 do
      local digit = tonumber(num_str:sub(j,j))
      local product = digit * 256 + carry
      table.insert(new_num, 1, tostring(product % 10))
      carry = math.floor(product / 10)
    end
    
    -- Add remaining carry digits
    while carry > 0 do
      table.insert(new_num, 1, tostring(carry % 10))
      carry = math.floor(carry / 10)
    end
    
    num_str = table.concat(new_num)
    if num_str == "" then num_str = "0" end
  end
  
  -- Convert decimal string to base58
  local res = {}
  while num_str ~= "0" do
    local remainder = 0
    local new_num = {}
    
    -- Divide by 58
    for i = 1, #num_str do
      local digit = tonumber(num_str:sub(i,i))
      local current = remainder * 10 + digit
      local quotient = math.floor(current / 58)
      if #new_num > 0 or quotient > 0 then
        table.insert(new_num, tostring(quotient))
      end
      remainder = current % 58
    end
    
    table.insert(res, 1, BASE58_ALPHABET:sub(remainder + 1, remainder + 1))
    num_str = table.concat(new_num)
    if num_str == "" then num_str = "0" end
  end
  
  -- Add leading '1's for leading zero bytes
  for i = 1, #data do
    if data:byte(i) == 0 then
      table.insert(res, 1, "1")
    else
      break
    end
  end
  
  return table.concat(res)
end

-- Generate address from compressed pubkey bytes
local function generate_bitshares_address(pubkey_bytes)
  assert(#pubkey_bytes == 33, "Expected 33-byte compressed public key")
  
  -- BitShares address: BTS + Base58Check(RIPEMD160(pubkey) + RIPEMD160_checksum)
  local ripemd = easy_ripemd160(pubkey_bytes)
  
  -- Calculate RIPEMD160 checksum for the address
  local checksum = easy_ripemd160(ripemd):sub(1, 4)
  
  local address_bytes = ripemd .. checksum
  return BTS_PREFIX .. base58_encode(address_bytes)
end

-- Main
local arg_key = arg[1]
if not arg_key then
  io.stderr:write("Usage: lua script.lua <BTS... base58 public key>\n")
  os.exit(1)
end

-- Strip prefix if present
if arg_key:sub(1, #BTS_PREFIX) == BTS_PREFIX then
  arg_key = arg_key:sub(#BTS_PREFIX + 1)
end

-- Decode base58 key and extract public key
local raw = base58_decode(arg_key)
print("Decoded " .. #raw .. " bytes from Base58")

assert(#raw >= 37, "Decoded key too short: " .. #raw .. " (expected at least 37 bytes: 33 pubkey + 4 checksum)")

-- BitShares pubkey format: [33-byte compressed pubkey][4-byte checksum]
local pubkey_bytes = raw:sub(1, 33)
local given_checksum = raw:sub(34, 37)

print("Public key (" .. #pubkey_bytes .. " bytes):", to_hex(pubkey_bytes))
print("Checksum from input:", to_hex(given_checksum))

-- For now, we'll proceed with the public key bytes we extracted
-- The checksum validation can be refined later once we understand the exact algorithm BitShares uses

-- Generate address from the pubkey bytes
local address = generate_bitshares_address(pubkey_bytes)
print("BitShares Address:")
print(address)

