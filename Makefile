# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# ns3-swarm-core: the org's standard targets (swarm-routing/.github#11). Every org
# repo has these same target names, with this repo's own commands behind them;
# `org-sync check` verifies the names are present, never the recipes. Add
# repo-only targets at the end. `make help` lists everything with a `## ` note.

NS3_IMAGE ?= ghcr.io/swarm-routing/ns3-dev:latest-debug
PLATFORM  ?=
# container kind: the image's Dockerfile and its build arguments
DOCKERFILE ?= Dockerfile
BUILD_ARGS ?=
# The org tooling (check-headers.py, org-sync): a shallow clone, refreshed to ORG_REF.
ORG_DIR   ?= $(HOME)/.cache/swarm-routing/dotgithub
ORG_REF   ?= main

.DEFAULT_GOAL := help
.PHONY: help build test lint format check clean shell ci headers org-sync-check org-tools core-test

help: ## list the targets
	@awk 'BEGIN{FS=":.*## "} /^[a-zA-Z0-9_-]+:.*## /{printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

build: ## build what this repo ships
	docker run --rm -v "$(CURDIR):/src/swarm-core:ro" $(NS3_IMAGE) ns3-build-contrib swarm-core=/src/swarm-core

test: ## the fast tests CI runs on every PR
	docker run --rm -v "$(CURDIR):/src/swarm-core:ro" $(NS3_IMAGE) ns3-build-contrib --strict-warnings --test swarm-core=/src/swarm-core

lint: ## static checks, no changes
	git ls-files "*.h" "*.cc" "*.cpp" "*.hpp" | xargs -r clang-format --dry-run --Werror

format: ## apply the formatters
	git ls-files "*.h" "*.cc" "*.cpp" "*.hpp" | xargs -r clang-format -i

clean: ## remove build outputs
	rm -rf build

shell: ## an interactive shell in the dev image with this repo at /src
	@command -v docker >/dev/null || { echo "make shell: docker not found (Docker Desktop, Colima, or docker.io)" >&2; exit 127; }
	docker run --rm -it -v "$(CURDIR):/src" -w /src $(if $(PLATFORM),--platform $(PLATFORM)) $(NS3_IMAGE) bash

org-tools:  # the same templates CI checks against: refreshed to ORG_REF every time
	@[ -d "$(ORG_DIR)/.git" ] || { git init -q "$(ORG_DIR)" && git -C "$(ORG_DIR)" remote add origin https://github.com/swarm-routing/.github; }
	@git -C "$(ORG_DIR)" fetch -q --depth 1 origin $(ORG_REF) && git -C "$(ORG_DIR)" checkout -q --detach FETCH_HEAD

headers: org-tools ## licence headers (org rule 1)
	python3 -I "$(ORG_DIR)/tools/checks/check-headers.py" --dual core/ --dual tools/ --core core/ --exclude '^core/bench/' .

org-sync-check: org-tools ## org-managed files match the org templates
	python3 -I "$(ORG_DIR)/tools/org-sync/org_sync.py" check .

check: lint test headers org-sync-check ## everything CI gates on

ci: check ## exactly what CI runs

core-test: ## core/ alone, no ns-3: plain CMake + ctest with CI's strict warnings
	cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core --output-on-failure
