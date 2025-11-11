#!/bin/bash
set -euo pipefail

global_step="(init)"
trap 'echo "Script failed at line $LINENO (while/after step: '$global_step') - in script $0"; exit 1' ERR

function step() {
	global_step="$*"
	printf "Step: $* \n"
}

function fatal() {
	printf "Error: $* \n"
	exit 1
}

function usage() {
	echo "$0 gitrev"
	echo "Build git revision with this hash"
}


if [ -z "${1+x}" ] ; then
	usage
	exit 1
fi

opt_rev="$1"
opt_logfn="$PWD/log.txt"

echo "Script will build for rev: $opt_rev"

step "clear log" && rm -f "$opt_logfn"
step "start log" && echo "New log $(date -u)" >> "$opt_logfn"

if [ -z "$opt_rev" ] ; then
	echo "No rev set." ; exit 1 ;
fi

function prog_info() {
	fn="$1"
	filetype_long=$(file "$fn" | sed 's/^[^:]*: //')
	filetype=${filetype_long:0:30}
	filehash=$(sha256sum "$fn" | cut -d' ' -f1 )
	echo "Result bin hash $filehash is [$filetype] in file $fn"
}

function build_normal() {
	echo "----------------------------------------------------------" >> "$opt_logfn"
	step "set clone" && dir_clone_alone=pay2exchange-core
	step "clean clone dir" && rm -rf "$dir_clone_alone"
	step "run git clone" && git clone https://github.com/pay2exchange/pay2exchange-core.git "$dir_clone_alone"
	step "cd into clone" && cd "$dir_clone_alone"
	step "run git checkout" && git checkout "$opt_rev"
	step "run git submodule update" && git submodule update --init --recursive

	step "Get stats about repo etc"

	git_here_rev=$(git rev-parse HEAD)
	echo "Building from rev $git_here_rev" >> "$opt_logfn"

	echo "git submodules: " >> "$opt_logfn"
	git submodule status --recursive >> "$opt_logfn"

	if git diff --quiet && git diff --cached --quiet; then
		echo "No local changes" >> $"opt_logfn"
	else
		echo "Local changes detected!"
		fatal "Local changes in the git clone" >> "$opt_logfn"
	fi

	build_summary="rev=${git_here_rev} using CXX=$CXX CC=$CC in PWD=${PWD}"
	echo "START: $build_summary" >> "$opt_logfn"

	start_time=$(date +%s)

	step "run the main compilation script" && /usr/bin/time -v bash ./dev.sh

	end_time=$(date +%s)
	elapsed=$((end_time - start_time))
	hh=$((elapsed / 3600))
	mm=$(((elapsed % 3600) / 60))
	ss=$((elapsed % 60))
	elapsed_str=$(printf "Elapsed time: %03d:%02d:%02d\n" $hh $mm $ss)

	echo "*** FINISH: ($elapsed_str = $elapsed s) $build_summary" >> "$opt_logfn"
	prog_info "use/programs/witness_node/witness_node" >> "$opt_logfn"
	prog_info "use/programs/cli_wallet/cli_wallet" >> "$opt_logfn"
	echo "----------------------------------------------------------" >> "$opt_logfn"
	echo "" >> "$opt_logfn"
}

(
	save_pwd="$PWD"
	source ~/use-clang && build_normal
	cd "$PWD"
)

(
	save_pwd="$PWD"
	source ~/use-clang-18 && build_normal
	cd "$PWD"
)

(
	save_pwd="$PWD"
	source ~/use-gcc && build_normal
	cd "$PWD"
)


