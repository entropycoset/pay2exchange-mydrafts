#!/bin/bash
set -e  # Exit on any error

cfg_sub="$1"

chain_seeds_dir="$HOME/.devel/SECRET/pay2exchange/chain/make-ec2${cfg_sub}/"
chain_seeds_fn="$chain_seeds_dir/seed.txt"

save_tolocal_private_fn="$HOME/chain-p2e/SECRET/test-genesis-ec2/private.json"
save_tolocal_publicgenesis_fn="$HOME/chain-p2e/test-genesis-ec2.json"
save_togit_priv_fn="$HOME/chain-p2e/link-git/bitshares/pay2exchange-testnet/genesis.private.json"
save_togit_publicgenesis_fn="$HOME/chain-p2e/link-git/bitshares/pay2exchange-testnet/genesis.json"
save_togit_cwd="$HOME/chain-p2e/link-git/bitshares/pay2exchange-testnet/"

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
	echo "the secret seed to generate keys does exist now ($chain_seeds_dir)"
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

read -e -i "5" -p "how many witnesses? (odd number)> " opt_wit
read -e -i "300" -p "how many SECONDS delay? (you should start up all witnesses before this many seconds from now)> " opt_delay

echo "Running lua script to generate genesis..."
if ! lua ../makechain-1.lua ../../../pay2exchange-core/programs/genesis_util/get_dev_key ~/.devel/SECRET/pay2exchange/chain/make-ec2/seed.txt $opt_wit -g ./input2.json $opt_delay  > out.json; then
	echo "Error: Failed to generate genesis file" >&2
	exit 1
fi

echo "Genesis generation completed successfully. Output written to out.json"


chmod g-r,o-r "private.json"


echo ; echo "--- save the results ---"
set -x
cp -i "private.json" $save_tolocal_private_fn
cp -i "out.json" $save_tolocal_publicgenesis_fn

cp -i "private.json" "$save_togit_priv_fn"
cp -i "out.json" "$save_togit_publicgenesis_fn"
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

