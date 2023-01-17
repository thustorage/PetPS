#!/usr/bin/env bash
#
# Copyright 2018, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# utils/copy-source.sh -- copy source files (from HEAD) to 'path_to_dir/pmdk'
# directory whether in git repository or not.
#
# usage: ./copy-source.sh [path_to_dir] [srcversion]

set -e

DESTDIR="$1"
SRCVERSION=$2

if [ -d .git ]; then
	if [ -n "$(git status --porcelain)" ]; then
		echo "Error: Working directory is dirty: $(git status --porcelain)"
		exit 1
	fi
else
	echo "Warning: You are not in git repository, working directory might be dirty."
fi

mkdir -p "$DESTDIR"/pmdk
echo -n $SRCVERSION > "$DESTDIR"/pmdk/.version

if [ -d .git ]; then
	git archive HEAD | tar -x -C "$DESTDIR"/pmdk
else
	find . \
	-maxdepth 1 \
	-not -name $(basename "$DESTDIR") \
	-not -name . \
	-exec cp -r "{}" "$DESTDIR"/pmdk \;
fi
