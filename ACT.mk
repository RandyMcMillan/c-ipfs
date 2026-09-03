ACT ?= act
ACT_CI_WORKFLOW ?= .github/workflows/ci.yml
ACT_KUBO_WORKFLOW ?= .github/workflows/kubo-interop.yml
ACT_WORKFLOW ?= $(ACT_CI_WORKFLOW)
ACT_IMAGE ?= ghcr.io/catthehacker/ubuntu:act-latest
ACT_CONTAINER_ARCH ?= linux/amd64
ACT_JOB ?= build
ACT_EXTRA_FLAGS ?=
ACT_BIND ?= false
ACT_DRYRUN ?= false
ACT_LIST ?= false
ACT_PRIVILEGED ?= false
ACT_QUIET ?= false
ACT_REBUILD ?= false
ACT_REUSE ?= false
ACT_WATCH ?= false
ACT_CLANG_BUILD ?= false
ACT_SHELL ?= bash
ACT_SHELL_WORKDIR ?= /workspace

ACT_PLATFORM_OVERRIDES := -P ubuntu-latest=$(ACT_IMAGE) -P macos-latest=$(ACT_IMAGE)

ifeq ($(strip $(ACT_CONTAINER_ARCH)),)
ACT_ARCH_FLAG :=
else
ACT_ARCH_FLAG := --container-architecture $(ACT_CONTAINER_ARCH)
endif

ACT_BASE_FLAGS := -v $(ACT_PLATFORM_OVERRIDES) $(ACT_ARCH_FLAG)
ACT_WORKFLOW_FLAGS := -W $(ACT_WORKFLOW)
ACT_OPTIONAL_FLAGS :=
ifeq ($(ACT_BIND),true)
ACT_OPTIONAL_FLAGS += --bind
ACT_EXTRA_FLAGS += --env BIND=true
endif
ifeq ($(ACT_DRYRUN),true)
ACT_OPTIONAL_FLAGS += --dryrun
endif
ifeq ($(ACT_LIST),true)
ACT_OPTIONAL_FLAGS += --list
endif
ifeq ($(ACT_PRIVILEGED),true)
ACT_OPTIONAL_FLAGS += --privileged
endif
ifeq ($(ACT_QUIET),true)
ACT_OPTIONAL_FLAGS += --quiet
endif
ifeq ($(ACT_REBUILD),true)
ACT_OPTIONAL_FLAGS += --rebuild
endif
ifeq ($(ACT_REUSE),true)
ACT_OPTIONAL_FLAGS += --reuse
ACT_EXTRA_FLAGS += --env REUSE=true
endif
ifeq ($(ACT_WATCH),true)
ACT_OPTIONAL_FLAGS += --watch
endif
ifeq ($(ACT_CLANG_BUILD),true)
ACT_OPTIONAL_FLAGS += --env CLANG_BUILD=true
endif
ACT_RUN_FLAGS := $(ACT_BASE_FLAGS) $(ACT_WORKFLOW_FLAGS) $(ACT_OPTIONAL_FLAGS) $(ACT_EXTRA_FLAGS)

.PHONY: act-help act-build act-kubo-interop act-job act-list act-watch act-shell act-build-ubuntu act-build-macos act-kubo

ifeq ($(firstword $(MAKEFILE_LIST)),$(lastword $(MAKEFILE_LIST)))
act-help:
	@printf '%s\n' "act helper targets"
	@printf '%s\n' ""
	@printf '%s\n' "  make -f ACT.mk act-build         Run the CI build job locally"
	@printf '%s\n' "  make -f ACT.mk act-kubo-interop  Run the Kubo interop job locally"
	@printf '%s\n' "  make -f ACT.mk act-list          List workflows and jobs"
	@printf '%s\n' "  make -f ACT.mk act-watch         Watch repo changes and rerun the workflow"
	@printf '%s\n' "  make -f ACT.mk act-shell         Open a shell in the act runner image"
	@printf '%s\n' "  make -f ACT.mk act-job ACT_JOB=x Run any workflow job by name"
	@printf '%s\n' "  make -f ACT.mk act-kubo          Alias for act-kubo-interop"
	@printf '%s\n' ""
	@printf '%s\n' "Useful overrides:"
	@printf '%s\n' "  ACT_IMAGE=ghcr.io/catthehacker/ubuntu:act-latest"
	@printf '%s\n' "  ACT_CONTAINER_ARCH=linux/amd64"
	@printf '%s\n' "  ACT_JOB=build"
	@printf '%s\n' "  ACT_REUSE=true ACT_REBUILD=true ACT_QUIET=true ACT_PRIVILEGED=true"
	@printf '%s\n' "  ACT_LIST=true ACT_WATCH=true ACT_BIND=true ACT_DRYRUN=true"
	@printf '%s\n' "  ACT_CLANG_BUILD=true      Also run the optional clang build"
	@printf '%s\n' "  ACT_SHELL=bash            Shell to launch inside the runner image"
endif

act-build:
	$(ACT) $(ACT_RUN_FLAGS) -j build

act-kubo-interop:
	$(ACT) $(ACT_BASE_FLAGS) -W $(ACT_KUBO_WORKFLOW) $(ACT_OPTIONAL_FLAGS) $(ACT_EXTRA_FLAGS) -j kubo-interop

act-kubo: act-kubo-interop

act-job:
	$(ACT) $(ACT_RUN_FLAGS) -j $(ACT_JOB)

act-list:
	$(ACT) $(filter-out --list,$(ACT_RUN_FLAGS)) --list

act-watch:
	$(ACT) $(filter-out --watch,$(ACT_RUN_FLAGS)) --watch

act-shell:
	docker run --rm -it --platform $(ACT_CONTAINER_ARCH) \
		-v "$(CURDIR):$(ACT_SHELL_WORKDIR)" \
		-w "$(ACT_SHELL_WORKDIR)" \
		--entrypoint $(ACT_SHELL) \
		$(ACT_IMAGE)

# Convenience aliases for the current workflow jobs.
act-build-ubuntu: act-build
act-build-macos: act-build
