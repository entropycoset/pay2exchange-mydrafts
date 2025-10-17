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

opt_rev="$1"
opt_logfn="$PWD/log.txt"

step "clear log" && rm -f "$opt_logfn"
step "start log" && echo "New log $(date -u)" >> "$opt_logfn"

function build_normal() {
	step "set clone" && dir_clone_alone=pay2exchange-core
	step "clean clone dir" && rm -rf "$dir_clone_alone"
	step "run git clone" && git clone git@github.com:pay2exchange/pay2exchange-core.git "$dir_clone_alone"
	step "cd into clone" && cd "$dir_clone_alone"
	step "run git checkout" && git checkout "$opt_rev"
	step "run git submodule update" && git submodule update --init --recursive

	step "Get stats about repo etc"

	git_here_rev=$(git rev-parse HEAD)

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
	step "run the main compilation script" && /usr/bin/time -v bash ./dev.sh
	echo "FINISH: $build_summary" >> "$opt_logfn"
}

(
	save_pwd="$PWD"
	source ~/use-clang && build_normal
	cd "$PWD"
)

(
	save_pwd="$PWD"
	source ~/use-clang18 && build_normal
	cd "$PWD"
)

(
	save_pwd="$PWD"
	source ~/use-gcc && build_normal
	cd "$PWD"
)


