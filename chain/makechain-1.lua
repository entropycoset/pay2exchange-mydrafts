--!/usr/bin/env lua

local json = require("dkjson")
local lib_tablesort = require("lib_tablesort")

-- Expand tilde (~) in file paths to home directory
local function expand_path(path)
  if path:sub(1, 1) == "~" then
    local home = os.getenv("HOME")
    if not home then
      io.stderr:write("Error: HOME environment variable not set\n")
      os.exit(1)
    end
    if path:sub(1, 2) == "~/" then
      return home .. path:sub(2)
    elseif path == "~" then
      return home
    else
      return path  -- ~username format not supported
    end
  end
  return path
end

-- Check for file existence
local function file_exists(path)
  local expanded_path = expand_path(path)
  local f = io.open(expanded_path, "r")
  if f then f:close() return true end
  return false
end

-- Run external command and return stdout
local function run_cmd(cmd)
  io.stderr:write("Running cmd: " .. cmd .. "\n")
  local f = assert(io.popen(cmd, 'r'))
  local output = f:read('*a')
  f:close()
  io.stderr:write("Cmd output read: " .. output .. "\n")
  return output
end

-- Function to get current time + offset in ISO format
local function get_genesis_timestamp(offset_seconds)
  local current_time = os.time()
  local genesis_time = current_time + offset_seconds
  return os.date("!%Y-%m-%dT%H:%M:%S", genesis_time)
end

-- Help message
local function show_help()
  print([[Usage:
  lua script.lua dev_key_path seed_file num_witnesses -g input.json timestamp_offset_seconds > output.json
  lua script.lua dev_key_path seed_file num_witnesses -r input.json > output.json

  seed_file: Path to a file containing the seed text (10-40 characters) on one line
]])
  os.exit(1)
end

-- Args
local dev_key_path = arg[1]
local seed_file = arg[2]
local num = tonumber(arg[3])
local mode = arg[4]
local input_file = arg[5]
local timestamp_offset = mode == "-g" and tonumber(arg[6]) or nil

if not (dev_key_path and seed_file and num and mode and input_file) then
  show_help()
end

if mode == "-g" and not timestamp_offset then
  io.stderr:write("Error: timestamp_offset_seconds required for -g mode\n")
  show_help()
end

if not file_exists(dev_key_path) then
  io.stderr:write("Error: dev_get_key not found at " .. dev_key_path .. "\n")
  os.exit(1)
end

-- Read and validate seed from file
local expanded_seed_file = expand_path(seed_file)

if not file_exists(seed_file) then
  io.stderr:write("Error: Seed file not found at " .. seed_file .. "\n")
  os.exit(1)
end

local seed_file_handle = io.open(expanded_seed_file, "r")
if not seed_file_handle then
  io.stderr:write("Error: Cannot read seed file " .. seed_file .. "\n")
  os.exit(1)
end

local seed = seed_file_handle:read("*line")
seed_file_handle:close()

if not seed then
  io.stderr:write("Error: Seed file " .. seed_file .. " is empty or unreadable\n")
  os.exit(1)
end

-- Validate seed length (10-40 characters)
local seed_length = string.len(seed)
if seed_length < 10 or seed_length > 40 then
  io.stderr:write("Error: Seed text length is " .. seed_length .. " characters. Must be between 10 and 40 characters\n")
  os.exit(1)
end

-- Load JSON
local file = assert(io.open(input_file, "r"))
local input_data = file:read("*a")
file:close()

-- Key storage
local witness_data = {}
local priv_csv = {}

-- Generate keys
for i = 1, num do
  local wit = string.format("wit%02d", i)
  local active_label = string.format("%s-%s-active", seed, wit)
  local owner_label = string.format("%s-%s-owner", seed, wit)

  local active_output = run_cmd(dev_key_path .. " " .. seed .. " " .. active_label)
  local owner_output = run_cmd(dev_key_path .. " " .. seed .. " " .. owner_label)

  local active_pub = active_output:match('"public_key":"%s*(BTS[%w]+)"')
  local active_priv = active_output:match('"private_key":"%s*(5[%w]+)"')

  local owner_pub = owner_output:match('"public_key":"%s*(BTS[%w]+)"')
  local owner_priv = owner_output:match('"private_key":"%s*(5[%w]+)"')
  local owner_addr = owner_output:match('"address":"%s*(BTS[%w]+)"')

  if not (active_pub and active_priv and owner_pub and owner_priv and owner_addr) then
    io.stderr:write("Error parsing keys for " .. wit .. "\n")
    os.exit(1)
  end

  witness_data[wit] = {
    name = wit,
    active_pub = active_pub,
    active_priv = active_priv,
    owner_pub = owner_pub,
    owner_priv = owner_priv,
    owner_addr = owner_addr
  }

  table.insert(priv_csv, string.format("%s,%s,%s,%s", wit, owner_priv, active_priv, owner_addr))
end

-- Replace placeholders in template (mode -r)
if mode == "-r" then
  for wit, data in pairs(witness_data) do
    input_data = input_data:gsub(wit .. "_owner_address", data.owner_addr)
    input_data = input_data:gsub(wit .. "_active_address", data.active_pub)
    input_data = input_data:gsub(wit .. "_signing_key", data.owner_pub)
  end
  io.write(input_data)

elseif mode == "-g" then
  local genesis = json.decode(input_data)
  local genesis_private = json.decode(input_data)
  
  -- Set the genesis timestamp
  genesis.initial_timestamp = get_genesis_timestamp(timestamp_offset)
  genesis_private.initial_timestamp = get_genesis_timestamp(timestamp_offset)

  local accounts = {}
  local balances = {}
  local committee = {}
  local witnesses = {}

  for wit, data in pairs(witness_data) do
    table.insert(accounts, {
      name = wit,
      owner_key = data.owner_pub,
      owner_key_full = {
        public_key = data.owner_pub,
        wif_priv_key = data.owner_priv
      },
      active_key = data.active_pub,
      active_key_full = {
        public_key = data.active_pub,
        wif_priv_key = data.active_priv
      },
      is_lifetime_member = true
    })

    table.insert(balances, {
      owner = data.owner_addr,
      asset_id = "1.3.0",
      amount = 100000
    })

    table.insert(committee, { owner_name = wit })

    table.insert(witnesses, {
      owner_name = wit,
      block_signing_key = data.owner_pub,
      block_signing_key_full = {
        public_key = data.owner_pub,
        wif_priv_key = data.owner_priv
      }
    })
  end

  -- Fill
  genesis.initial_accounts = {}
  genesis.initial_balances = {}
  genesis.initial_committee_candidates = {}
  genesis.initial_witness_candidates = {}
  genesis_private.initial_accounts = accounts
  genesis_private.initial_balances = balances
  genesis_private.initial_committee_candidates = committee
  genesis_private.initial_witness_candidates = witnesses

  -- Sorting
  local function sort_by_name(a, b)
    return tonumber(a.name:match("%d+")) < tonumber(b.name:match("%d+"))
  end

  local function sort_by_owner_name(a, b)
    return tonumber(a.owner_name:match("%d+")) < tonumber(b.owner_name:match("%d+"))
  end

  table.sort(accounts, sort_by_name)
  table.sort(balances, function(a, b) return a.owner < b.owner end)
  table.sort(committee, sort_by_owner_name)
  table.sort(witnesses, sort_by_owner_name)

  genesis.initial_accounts = {}
  genesis.initial_balances = {}
  genesis.initial_committee_candidates = {}
  genesis.initial_witness_candidates = {}

  for _, v in ipairs(accounts) do
    local vcopy = {}
    for k, val in pairs(v) do
      if not k:match("_full$") then vcopy[k] = val end
    end
    table.insert(genesis.initial_accounts, vcopy)
  end

  for _, v in ipairs(witnesses) do
    local vcopy = {}
    for k, val in pairs(v) do
      if not k:match("_full$") then vcopy[k] = val end
    end
    table.insert(genesis.initial_witness_candidates, vcopy)
  end

  genesis.initial_balances = balances
  genesis.initial_committee_candidates = committee

  -- Output both files
  print(lib_tablesort.encode_sorted(genesis))
  local priv_file = assert(io.open("private.json", "w"))
  priv_file:write(lib_tablesort.encode_sorted(genesis_private))
  priv_file:close()

else
  show_help()
end

-- Write private keys to CSV
local csv_file = assert(io.open("witness_keys.csv", "w"))
csv_file:write("name,owner_priv,active_priv,owner_addr\n")
for _, row in ipairs(priv_csv) do
  csv_file:write(row .. "\n")
end
csv_file:close()

