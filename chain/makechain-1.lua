#!/usr/bin/env lua

local json = require("dkjson")
local lib_tablesort=require("lib_tablesort")

-- Check for file existence
local function file_exists(path)
  local f = io.open(path, "r")
  if f then f:close() return true end
  return false
end

-- Run external command and return stdout
local function run_cmd(cmd)
  print("Running cmd: " .. cmd)				
  local f = assert(io.popen(cmd, 'r'))
  local output = f:read('*a')
  f:close()
  print("Cmd output read: " .. output)
  return output
end

-- Help message
local function show_help()
  print([[
Usage:
  lua script.lua dev_key_path seed num_witnesses -g input.json > output.json
  lua script.lua dev_key_path seed num_witnesses -r input.json > output.json

Options:
  -g    Generate JSON blocks (initial_accounts, initial_committee_candidates, etc.)
  -r    Replace placeholders in input JSON with generated keys
]])
  os.exit(1)
end

-- Args
local dev_key_path = arg[1]
local seed = arg[2]
local num = tonumber(arg[3])
local mode = arg[4]
local input_file = arg[5]

if not (dev_key_path and seed and num and mode and input_file) then
  show_help()
end

if not file_exists(dev_key_path) then
  io.stderr:write("Error: dev_get_key not found at " .. dev_key_path .. "\n")
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

  local active_pub = active_output:match("\"public_key\":\"%s*(BTS[%w]+)\"")																											
  local active_priv = active_output:match("\"private_key\":\"%s*(5[%w]+)\"")

  local owner_pub = owner_output:match("\"public_key\":\"%s*(BTS[%w]+)\"")
  local owner_priv = owner_output:match("\"private_key\":\"%s*(5[%w]+)\"")
  local owner_addr = owner_output:match("\"address\":\"%s*(BTS[%w]+)\"")

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

-- Generate sections (mode -g)
elseif mode == "-g" then
  local genesis = json.decode(input_data)

  -- Generate accounts
  local accounts = {}
  local balances = {}
  local committee = {}
  local witnesses = {}

  for wit, data in pairs(witness_data) do
    table.insert(accounts, {
      name = wit,
      owner_key = data.owner_pub,
      active_key = data.active_pub,
      is_lifetime_member = true
    })

    table.insert(balances, {
      owner = data.owner_addr,
      asset_id = "1.3.0", -- the native (core) token
      amount = 100000  -- 1000.00 assuming 5 decimals
    })

    table.insert(committee, {
      owner_name = wit
    })

    table.insert(witnesses, {
      owner_name = wit,
      block_signing_key = data.owner_pub
    })
  end

  genesis.initial_accounts = accounts
  genesis.initial_balances = balances
  genesis.initial_committee_candidates = committee
  genesis.initial_witness_candidates = witnesses

-- sort

-- Sort initial_accounts by name (e.g., wit01, wit02...)
table.sort(genesis.initial_accounts, function(a, b)
  local na = tonumber(a.name:match("%d+"))
  local nb = tonumber(b.name:match("%d+"))
  return na < nb
end)

-- Sort initial_balances by owner address (alphabetically)
table.sort(genesis.initial_balances, function(a, b)
  return a.owner < b.owner
end)

-- Sort initial_committee_candidates by owner_name (e.g., wit01, wit02...)
table.sort(genesis.initial_committee_candidates, function(a, b)
  local na = tonumber(a.owner_name:match("%d+"))
  local nb = tonumber(b.owner_name:match("%d+"))
  return na < nb
end)

-- Sort initial_witness_candidates by owner_name (e.g., wit01, wit02...)
table.sort(genesis.initial_witness_candidates, function(a, b)
  local na = tonumber(a.owner_name:match("%d+"))
  local nb = tonumber(b.owner_name:match("%d+"))
  return na < nb
end)

  
-- Encode once
local raw_json = json.encode(genesis, { indent = true })
-- Decode to reorder fields (canonical order)
local parsed = json.decode(raw_json)
-- Re-encode to canonical format
local canonical_json = json.encode(parsed, { indent = true })
	-- Output the final canonicalized JSON
print(canonical_json)

--  io.write(json.encode(genesis, { indent = true }))

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

