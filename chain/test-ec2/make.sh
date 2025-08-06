#!/bin/bash
chain_seeds_dir="$HOME/.devel/SECRET/pay2exchange/chain/make-ec2/"
chain_seeds_fn="$chain_seeds_dir/seed.txt"

if [ ! -f "$chain_seeds_fn" ]
then
	echo "the secret seed to generate keys is not existing ($chain_seeds_dir) - we will GENERATE IT NOW. enter to continue, ctrl-C to abort"
	read _
	mkdir -p $chain_seeds_dir
	pwgen -s -0 20 > "$chain_seeds_fn"
fi

lua ../makechain-1.lua  ../../../pay2exchange-core/programs/genesis_util/get_dev_key  ~/.devel/SECRET/pay2exchange/chain/make-ec2/seed.txt  5 -g ./input2.json 60 > out.json
