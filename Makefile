obj-m += pghole_scanner.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

test:
	sudo dmesg -C
	sudo insmod pghole_scanner.ko
	sudo rmmod pghole_scanner.ko
	sudo dmesg