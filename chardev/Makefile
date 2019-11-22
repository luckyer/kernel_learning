KVERS = $(shell uname -r)
#Kernel Modules
obj-m += hello.o

build:kernel_modules
kernel_modules:
	make -C /lib/modules/$(KVERS)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KVERS)/build M=$(PWD) clean
