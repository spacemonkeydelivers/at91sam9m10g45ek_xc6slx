# A tale about the driver, the program and shitty HW

> This is my rifle.
There are many like it, but this one is mine.
My rifle is my best friend. It is my life.
I must master it as I must master my life.
Without me, my rifle is useless.
Without my rifle, I am useless.
I must fire my rifle true.
I must shoot straighter than my enemy who is trying to kill me.
I must shoot him before he shoots me.
...

This repository contains utilities and drivers which are needed to perform some really weird stuff
with at91sam9m10g45ek in even more weird and obscure way. 

[The hall of... fame](https://www.linux.org.ru/forum/development/11922230)

## Environment

You should have a copy of linux kernel somewhere. To do any kernel or
userspace development you should setup your environment first.
`./scripts/prepar_env.sh`
will take care of this task:
```
./scripts/prepair_env.sh <PATH_TO_LINUX_KERNEL>
#this will create all the necessary symlinks
```

## The driver (which leaks)

To build the driver/kernel image one should use the following commands:

```
CROSS_COMPILE=<YOUR_TOOLCHAIN_PREFIX> ARCH=arm make sk_at91_xc6slx_dt_defconfig
CROSS_COMPILE=<YOUR_TOOLCHAIN_PREFIX> ARCH=arm LOADADDR=70008000 make uImage
#toolchain prefix may look like: "arm-at91-linux-gnueabi-" or something like that
```

## The program (which crashes)

To build the program your compiler should be aware of the kernel's sources location :

```
    gcc -isystem <PATH_TO_DIRECTORY_WITH_SOURCES> -march arm ...
```

## The HW (mailfunctioned)

TODO.
