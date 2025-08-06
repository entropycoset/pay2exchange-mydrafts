#!/bin/bash
chain_seeds_dir="$HOME/.devel/SECRET/pay2exchange/chain/make-ec2/"
chain_seeds_fn="$chain_seeds_dir/seed.txt"

makeseed() {
	pwgen -s -0 20 > "$1"
}

if [ ! -f "$chain_seeds_fn" ]
then
	echo "the secret seed to generate keys is NOT EXISTING ($chain_seeds_dir) - we will GENERATE IT NOW. enter to continue, ctrl-C to abort"
	read _
	mkdir -p $chain_seeds_dir
	makeseed "$chain_seeds_fn"
else
	echo "the secret seed to generate keys does exist now ($chain_seeds_dir)"
	echo "override it? type 'over' to override it, or type 'k' like 'keep' to keep current one."

	read -p "Enter your reply: " reply
		if [ "$reply" = "over" ]; then
		    echo "will OVERRIDE..."
		    sleep 2
			makeseed "$chain_seeds_fn"
		elif [ "$reply" = "k" ]; then
			echo "keep old one."
		else
		    echo "Invalid input. Aborting."
		    exit 1
		fi

	
fi

lua ../makechain-1.lua  ../../../pay2exchange-core/programs/genesis_util/get_dev_key  ~/.devel/SECRET/pay2exchange/chain/make-ec2/seed.txt  5 -g ./input2.json 60 > out.json
