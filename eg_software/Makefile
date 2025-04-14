ifneq (${KERNELRELEASE},)

# KERNELRELEASE defined: we are being compiled as part of the Kernel
        obj-m := vga_bird.o aud.o

else

# We are being compiled as a module: use the Kernel build system

	KERNEL_SOURCE := /usr/src/linux-headers-$(shell uname -r)
        PWD := $(shell pwd)

default: module test2

hello: hello.c
	gcc -o hello hello.c
test: test.c
	gcc -o test test.c interfaces.c interfaces.h
test2: test2_update.c
	gcc -o test2 test2_update.c interfaces.c interfaces.h
module:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} modules

clean:
	${MAKE} -C ${KERNEL_SOURCE} SUBDIRS=${PWD} clean
	${RM} hello test test2

TARFILES = Makefile README vga_bird.h vga_bird.c aud.c aud.h interfaces.c interfaces.h test.c hello.c 
TARFILE = lab3-sw.tar.gz
.PHONY : tar
tar : $(TARFILE)

$(TARFILE) : $(TARFILES)
	tar zcfC $(TARFILE) .. $(TARFILES:%=software/%)

endif 
