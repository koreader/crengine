.ONESHELL:
export SHELL := bash
.SHELLFLAGS := -eo pipefail -c

# C/C++ files. {{{

# Antiword.
define CPPFILES+=
thirdparty/antiword/asc85enc.c
thirdparty/antiword/blocklist.c
thirdparty/antiword/chartrans.c
thirdparty/antiword/datalist.c
thirdparty/antiword/depot.c
thirdparty/antiword/dib2eps.c
thirdparty/antiword/doclist.c
thirdparty/antiword/fail.c
thirdparty/antiword/finddata.c
thirdparty/antiword/findtext.c
thirdparty/antiword/fontlist.c
thirdparty/antiword/fonts.c
thirdparty/antiword/fonts_u.c
thirdparty/antiword/hdrftrlist.c
thirdparty/antiword/imgexam.c
thirdparty/antiword/listlist.c
thirdparty/antiword/misc.c
thirdparty/antiword/notes.c
thirdparty/antiword/options.c
thirdparty/antiword/out2window.c
thirdparty/antiword/pdf.c
thirdparty/antiword/pictlist.c
thirdparty/antiword/prop0.c
thirdparty/antiword/prop2.c
thirdparty/antiword/prop6.c
thirdparty/antiword/prop8.c
thirdparty/antiword/properties.c
thirdparty/antiword/propmod.c
thirdparty/antiword/rowlist.c
thirdparty/antiword/sectlist.c
thirdparty/antiword/stylelist.c
thirdparty/antiword/stylesheet.c
thirdparty/antiword/summary.c
thirdparty/antiword/tabstop.c
thirdparty/antiword/unix.c
thirdparty/antiword/utf8.c
thirdparty/antiword/word2text.c
thirdparty/antiword/worddos.c
thirdparty/antiword/wordlib.c
thirdparty/antiword/wordmac.c
thirdparty/antiword/wordole.c
thirdparty/antiword/wordwin.c
thirdparty/antiword/xmalloc.c
endef
# CHMLib.
define CPPFILES+=
thirdparty/chmlib/src/chm_lib.c
thirdparty/chmlib/src/lzx.c
endef
# CREngine.
define CPPFILES+=
crengine/qimagescale/qimagescale.cpp
crengine/src/chmfmt.cpp
crengine/src/cp_stats.cpp
crengine/src/cri18n.cpp
crengine/src/crtxtenc.cpp
crengine/src/docxfmt.cpp
crengine/src/epubfmt.cpp
crengine/src/fb3fmt.cpp
crengine/src/hist.cpp
crengine/src/hyphman.cpp
crengine/src/lstridmap.cpp
crengine/src/lvdocview.cpp
crengine/src/lvdrawbuf.cpp
crengine/src/lvfnt.cpp
crengine/src/lvfntman.cpp
crengine/src/lvimg.cpp
crengine/src/lvmemman.cpp
crengine/src/lvopc.cpp
crengine/src/lvpagesplitter.cpp
crengine/src/lvrend.cpp
crengine/src/lvstream.cpp
crengine/src/lvstring.cpp
crengine/src/lvstsheet.cpp
crengine/src/lvstyles.cpp
crengine/src/lvtextfm.cpp
crengine/src/lvtinydom.cpp
crengine/src/lvxml.cpp
crengine/src/mathml.cpp
crengine/src/odtfmt.cpp
crengine/src/odxutil.cpp
crengine/src/pdbfmt.cpp
crengine/src/props.cpp
crengine/src/rtfimp.cpp
crengine/src/textlang.cpp
crengine/src/txtselector.cpp
crengine/src/wordfmt.cpp
endef

# }}}

# CSS files. {{{

define CSS_FILES
cr3gui/data/epub.css
cr3gui/data/html5.css
cr3gui/data/fb2.css
endef

# }}}

# Hyphenation patterns. {{{

HYPH_DIR := cr3gui/data/hyph
HYPH_PATTERNS := $(wildcard $(HYPH_DIR)/*.pattern)
HYPH_LANGUAGES := $(HYPH_DIR)/languages.json

# }}}

# Helpers. {{{

ifneq (,$(or $(CLICOLOR_FORCE),$(MAKE_TERMOUT)))
export CLICOLOR_FORCE := 1
ANSI_GREEN := \033[32;1m
ANSI_RED   := \033[31;1m
ANSI_RESET := \033[0m\033[1G
else
ANSI_GREEN :=
ANSI_RED   :=
ANSI_RESET :=
endif

escape = $(shell printf "%q " $1)

ifneq (,$(CLICOLOR_FORCE))
define fake_tty
script --command '$(call escape,$1)' --quiet --return /dev/null
endef
else
define fake_tty
$1
endef
endif

define trace_info
printf '%b%s%b\n' '$(ANSI_GREEN)' $1 '$(ANSI_RESET)'
endef

define trace_warn
printf '%b%s%b\n' '$(ANSI_RED)' $1 '$(ANSI_RESET)'
endef

ifneq (,$(GITHUB_ACTIONS))
ci_startgroup := printf '::group::'
ci_endgroup := printf '::endgroup::\n'
ci_time := time
else
ci_startgroup := :
ci_endgroup := :
ci_time =
endif

define warn_on_error
if [ $1 -ne 0 ]; then $(call trace_warn,$2); fi
endef

# `$(call lint_rule,prefix,file,command)`
define lint_rule
$1: $1-$2
$2: $1-$2
$1-$2:
	@#!/bin/bash
	# Fix weird issue with `make --output-sync=…` & colors,
	# plus add a little bit of separation between checks.
	printf '%b\n' '$(ANSI_RESET)'
	$(ci_startgroup)
	$(call trace_info,'Running $1 on $2');
	code=0
	$3 || code=$$$$?
	$(ci_endgroup)
	$(call warn_on_error,$$$${code},'Running $1 on $2 failed with exit code: '$$$${code})
	exit $$$${code}
endef


# }}}

# Compilation flags. {{{

CPPFLAGS:=
# Features.
define CPPFLAGS+=
-DALLOW_KERNING=1
-DBUILD_LITE=0
-DCR3_PATCH=1
-DCR_EMULATE_GETTEXT
-DUSE_FONTCONFIG=0
-DUSE_FREETYPE=1
-DUSE_FRIBIDI=1
-DUSE_GIF=1
-DUSE_HARFBUZZ=1
-DUSE_LIBJPEG=1
-DUSE_LIBPNG=1
-DUSE_LIBUNIBREAK=1
-DUSE_LIBWEBP=1
-DUSE_NANOSVG=0
-DUSE_UTF8PROC=1
-DUSE_ZSTD=1
endef
# Check release build.
define CPPFLAGS+=
-DNDEBUG
endef
# Platform flags.
define CPPFLAGS+=
-D_LINUX=1
-DLINUX=1
-DLBOOK=0
-UANDROID
-UCR_POCKETBOOK
endef
define PACKAGES
freetype2
fribidi
harfbuzz
libjpeg
libunibreak
libutf8proc
endef
CPPFLAGS += $(shell pkg-config --cflags $(strip $(PACKAGES)))
define CPPFLAGS+=
-Ithirdparty/antiword
-Ithirdparty/chmlib/src
-Icrengine/include
endef

# }}}

# Clang-Tidy. {{{

define CLANG_TIDY_FLAGS
$(if $(CLICOLOR_FORCE),--use-color)
endef

define clang_tidy_rule
$(call lint_rule,clang-tidy,$1,$(ci_time) clang-tidy $(strip $(CLANG_TIDY_FLAGS) $1 -- $(if $(filter %.cpp,$1),--std=c++17) $(CPPFLAGS)))
endef

# }}}

# Cppcheck. {{{

define CPPCHECK_FLAGS+=
--check-level=exhaustive
--error-exitcode=2
--inline-suppr
--language=c++
--platform=unix64
--std=c++17
--template=gcc
endef
# For chmlib.
define CPPCHECK_FLAGS+=
-D'__x86_64__'
endef
# From libjpeg-turbo, used in lvimg.
# FIXME: why are we not getting those automatically via jpeglib.h → jmorecfg.h?
define CPPCHECK_FLAGS+=
-D'METHODDEF(type)=static type'
-D'GLOBAL(type)=type'
endef

define cppcheck_rule
$(call lint_rule,cppcheck,$1,$(call fake_tty,$(ci_time) cppcheck $(strip $(CPPCHECK_FLAGS) $(filter-out -pthread,$(CPPFLAGS))) $1))
endef

# }}}

# `langages.json`. {{{

define check_languages.json
	check() {
	  filesystem_patterns="$$$$(cd $(HYPH_DIR) && printf '%s\n' *.pattern | sort)"
	  languages_patterns="$$$$(jq --raw-output '.[] | .filename | select(startswith("@") == false)' $(HYPH_LANGUAGES) | sort)"
	  if ! diff="$$$$(diff --unified $(if $(CLICOLOR_FORCE),--color=always) --label='files in $(HYPH_DIR)' <(echo "$$$${filesystem_patterns}") --label='files in $(HYPH_LANGUAGES)' <(echo "$$$${languages_patterns}"))"; then
	  echo "$$$${diff}"
	  return 1;
	  fi
	}
	$(ci_time) check
endef

# }}}

# Stylelint. {{{

define STYLELINT_FLAGS
$(if $(CLICOLOR_FORCE),--color)
--formatter=$(if $(GITHUB_ACTIONS),github,unix)
endef

define stylelint_rule
$(call lint_rule,stylelint,$1,$(ci_time) npx stylelint $(strip $(STYLELINT_FLAGS) $1))
endef

# }}}

# XmlLint. {{{

define xmllint_rule
$(call lint_rule,xmllint,$1,$(ci_time) xmllint --output /dev/null $1)
endef

# }}}

CPPFILES := $(filter %.c %.cpp,$(sort $(CPPFILES) $(filter-out cppcheck-% clang-tidy-%,$(MAKECMDGOALS))))
CSS_FILES := $(filter %.css, $(sort $(CSS_FILES) $(filter-out stylelint-%,$(MAKECMDGOALS))))
HYPH_PATTERNS := $(filter %.pattern,$(sort $(HYPH_PATTERNS) $(filter-out xmllint-%,$(MAKECMDGOALS))))

lint: $(CPPFILES) $(CSS_FILES) $(HYPH_LANGUAGES) $(HYPH_PATTERNS)

cpp: $(CPPFILES)

hyph: $(HYPH_LANGUAGES) $(HYPH_PATTERNS)

style: $(CSS_FILES)

# Avoid "Nothing to be done for '…'"
# or "'…' is up to date messages".
$(CPPFILES) $(CSS_FILES) $(HYPH_PATTERNS):
	@:

.PHONY: $(HYPH_LANGUAGES)

$(foreach f,$(CPPFILES),\
	$(eval $(call clang_tidy_rule,$f))\
	$(eval $(call cppcheck_rule,$f))\
	)

$(foreach f,$(CSS_FILES),\
	$(eval $(call stylelint_rule,$f))\
	)

$(eval $(call lint_rule,check,$(HYPH_LANGUAGES),$(check_languages.json)))

$(foreach f,$(HYPH_PATTERNS),\
	$(eval $(call xmllint_rule,$f))\
	)

# vim: foldmethod=marker foldlevel=0
