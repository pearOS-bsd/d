# the daemons of pearOS: see README.md.  `make darwin` (Mach-O daemons, llvm21 + ld64.lld), `make bsd WSBSD=../misc/wsbsd OUT=out` (GNUstep daemons)
OUT?=out
darwin:
	sh darwin/build.sh $(OUT)/darwin
bsd:
	WSBSD=$(WSBSD) COMPAT=$(COMPAT) sh bsd/build.sh $(OUT)/bsd
.PHONY: darwin bsd
