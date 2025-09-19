#!/bin/bash
set -e  # Exit on any error

cfg_name="ec3"
cfg_sub="$1" # subname?

chain_seeds_dir="$HOME/.devel/SECRET/pay2exchange/chain/make-${cfg_name}${cfg_sub}/"
chain_seeds_fn="$chain_seeds_dir/seed.txt"

save_tolocal_private_fn="$HOME/chain-p2e/SECRET/test-genesis-${cfg_name}/private.json"
save_tolocal_publicgenesis_fn="$HOME/chain-p2e/test-genesis-${cfg_name}.json"
save_togit_priv_fn="$HOME/chain-p2e/link-git/bitshares/pay2exchange-testnet/${cfg_name}-genesis.private.json"
save_togit_publicgenesis_fn="$HOME/chain-p2e/link-git/bitshares/pay2exchange-testnet/${cfg_name}-genesis.json"
save_togit_cwd="$HOME/chain-p2e/link-git/bitshares/pay2exchange-testnet/"

opt_generator_version=1

die() {
	echo "Error: " "$@"
	exit 1
}

makeseed() {
	if ! pwgen -s -0 20 > "$1"; then
		echo "Error: Failed to generate seed with pwgen" >&2
		exit 1
	fi
}

if [ ! -f "$chain_seeds_fn" ]; then
	echo "the secret seed to generate keys is NOT EXISTING ($chain_seeds_dir) - we will GENERATE IT NOW. enter to continue, ctrl-C to abort"
	read _
	if ! mkdir -p "$chain_seeds_dir"; then
		echo "Error: Failed to create directory $chain_seeds_dir" >&2
		exit 1
	fi
	makeseed "$chain_seeds_fn"
else
	echo "the secret seed to generate keys does exist now (in dir $chain_seeds_dir)"
	echo "override it? type 'over' to override it, or type 'k' like 'keep' to keep current one."

	read -e -i 'k' -p "k(eep) or over(ride) > " reply
	if [ "$reply" = "over" ]; then
		echo "will OVERRIDE..."
		sleep 1
		makeseed "$chain_seeds_fn"
	elif [ "$reply" = "k" ]; then
		echo "keep old one."
	else
		echo "Invalid input. Aborting."
		exit 1
	fi
fi

# Check if required files exist before running lua script
if [ ! -f "../makechain-1.lua" ]; then
	echo "Error: makechain-1.lua not found" >&2
	exit 1
fi

if [ ! -f "../../../pay2exchange-core/programs/genesis_util/get_dev_key" ]; then
	echo "Error: get_dev_key not found" >&2
	exit 1
fi

if [ ! -f "./input2.json" ]; then
	echo "Error: input2.json not found" >&2
	exit 1
fi

chain_seeds_cfg_witt_fn="$chain_seeds_dir/cfg-witt.txt"
if [[ -f $chain_seeds_cfg_witt_fn ]]; then
    read -r opt_wit < "$chain_seeds_cfg_witt_fn"
else
    opt_wit=5
fi

read -e -i "$opt_wit" -p "how many witnesses? (odd number)> " opt_wit
echo "$opt_wit" > "$chain_seeds_cfg_witt_fn"
echo "(ok, and saved this option for future in this chain - $chain_seeds_cfg_witt_fn)"

read -e -i "300" -p "how many SECONDS delay? (you should start up all witnesses before this many seconds from now)> " opt_delay

echo "Running lua script to generate genesis..."
if ! lua ../makechain-1.lua ../../../pay2exchange-core/programs/genesis_util/get_dev_key ~/.devel/SECRET/pay2exchange/chain/make-${cfg_name}/seed.txt $opt_wit -g ./input2.json $opt_delay $opt_generator_version  > out.json; then
	echo "Error: Failed to generate genesis file" >&2
	exit 1
fi

echo "Genesis generation completed successfully. Output written to out.json"


chmod g-r,o-r "private.json" || die "can not chmod"

copy_target_mkdir() {
    mkdir -p "$(dirname "$2")" || die "Can not make dir for copy target ($2)"
		cp "$1" "$2" || die "Can not copy into $2"
}

echo ; echo "--- save the results ---"
set -x
copy_target_mkdir "private.json" $save_tolocal_private_fn
copy_target_mkdir "out.json" $save_tolocal_publicgenesis_fn

copy_target_mkdir "private.json" "$save_togit_priv_fn"
copy_target_mkdir "out.json" "$save_togit_publicgenesis_fn"
(
	cd "$save_togit_cwd"
	git diff
	git remote -v
	set +x
	echo "GOOD to PUBLISH?"
	read -e -i '' -p "pusht the GENESIS into OUR GIT? (y/n)> " reply
	if [ "$reply" = "y" ]; then
		echo "Will push it TO GIT..."
		sleep 1
		git commit -a && git push
	else
		echo "NOT PUSHING"
		sleep 1
	fi
)

echo "ALl done then?"
file "$save_tolocal_private_fn"
file "$save_tolocal_publicgenesis_fn"
file "$save_togit_priv_fn"
file "$save_togit_publicgenesis_fn"

