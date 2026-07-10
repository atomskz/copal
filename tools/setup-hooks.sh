#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Point git at the tracked hooks directory. Run once after cloning.
git config core.hooksPath .githooks
echo "copal: git hooks activated (core.hooksPath=.githooks)"
