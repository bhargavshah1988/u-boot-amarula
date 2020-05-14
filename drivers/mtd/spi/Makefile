# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

ifdef CONFIG_DM_SPI_FLASH
spi-nor-y += sf-uclass.o
else
spi-nor-y += sf-nodm.o
endif
spi-nor-y += spi-nor-ids.o

ifdef CONFIG_SPL_BUILD
obj-$(CONFIG_SPL_SPI_BOOT)	+= fsl_espi_spl.o
ifeq ($(CONFIG_SPL_SPI_FLASH_TINY),y)
spi-nor-y += spi-nor-tiny.o
else
spi-nor-y += spi-nor-core.o
endif
else
spi-nor-y += spi-nor-core.o
endif

obj-$(CONFIG_SPI_FLASH) += spi-nor.o
obj-$(CONFIG_SPI_FLASH_DATAFLASH) += sf_dataflash.o
obj-$(CONFIG_$(SPL_)SPI_FLASH_MTD) += sf_mtd.o
obj-$(CONFIG_SPI_FLASH_SANDBOX) += sandbox.o
